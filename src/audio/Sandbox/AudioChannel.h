// AudioChannel — lock-free audio shared-memory ring between host and sandbox.
//
// Layout described in Protocol.h (AudioShmHeader). One mapping per sandbox;
// the host creates it before spawning the subprocess and passes the mapping
// name on the command line.
//
// Threading: the host's audio thread calls `pushInputBlock()` (publishes a
// block of audio + the per-block MIDI queue together) and `popBlock(true,…)`
// (drains the matching processed-output block). The sandbox's audio thread
// runs the mirror: `popInputBlock()` → plugin->processBlock → `pushBlock(true,
// …)`. Both sides block on the partner's OS event with a short timeout, so
// dropouts are detectable. `signalSandboxWake()` lets the host break the
// sandbox out of its popInputBlock wait without publishing a real block —
// used by the audio-thread pause/drain protocol around non-realtime control
// ops (kPrepare / kSetBlockSize / kGetState / kSetState).

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

#include "Protocol.h"

namespace slopsmith::sandbox {

class AudioChannel
{
public:
    // Handoff from the host side to the sandbox side. On Windows these are
    // kernel-object names the sandbox re-opens by name. On POSIX the audio
    // path is fd-passed instead (no named auto-reset event exists on macOS
    // that is also crash-safe — see AudioChannel_posix.cpp), so the string
    // names are unused and the integer fds below carry the handoff.
    struct Names
    {
        juce::String shm;       // file-mapping object name (Windows)
        juce::String evtToHost; // sandbox→host (output ready) (Windows)
        juce::String evtToSandbox; // host→sandbox (input ready) (Windows)

       #if ! JUCE_WINDOWS
        // POSIX handoff. `shmFd` is a dup of the anonymous shared-memory fd;
        // `sandboxAudioFd` is the sandbox's end of the bidirectional doorbell
        // socketpair. createHostSide fills both with fds the *sandbox* side
        // consumes: for an in-process loopback the sandbox AudioChannel takes
        // them directly via openSandboxSide; for a real spawn the host
        // dup2()s them into the child (SubprocessHandle) and then closes its
        // copies. openSandboxSide takes ownership and closes them in close().
        int shmFd = -1;
        int sandboxAudioFd = -1;
       #endif
    };

    AudioChannel();
    ~AudioChannel();

    // Host side: create the shm + both events, return the names for passing to
    // the subprocess.
    bool createHostSide(const AudioDimensions& dims, Names& namesOut,
                        juce::String& errorOut);

    // Sandbox side: open existing shm + events by name.
    bool openSandboxSide(const Names& names, juce::String& errorOut);

    // Whichever side we are: copy a block of audio in (host: input → sandbox;
    // sandbox: processed output → host). Returns false if the ring is full.
    //
    // For the INPUT direction, callers MUST use pushInputBlock() — pushBlock
    // does not touch the slot's MidiQueue, so a direct pushBlock(false, ...)
    // would leave whatever MIDI count was in the slot from a prior
    // pushInputBlock and the next popInputBlock would replay those stale
    // events against fresh audio. Today the only input producer is
    // SandboxedProcessor::processBlock and it always goes through
    // pushInputBlock; this overload exists for the OUTPUT direction
    // (sandbox → host audio, no MIDI carried back).
    bool pushBlock(bool isOutputRing, const juce::AudioBuffer<float>& src,
                   int numSamples);

    // Mirror of pushBlock: drain one block out. Returns false on timeout.
    bool popBlock(bool isOutputRing, juce::AudioBuffer<float>& dst,
                  int numSamples, int timeoutMs);

    // Host-side input push that bundles per-block MIDI into the upcoming
    // slot's MidiQueue. Events past kMidiEventsPerSlot (or larger than
    // kMidiEventMaxBytes, e.g. SysEx) bump the queue's overflow counter and
    // are dropped. The audio thread never blocks; lossy MIDI is the
    // documented v2 policy.
    bool pushInputBlock(const juce::AudioBuffer<float>& src,
                        const juce::MidiBuffer& midi,
                        int numSamples);

    // Sandbox-side input pop that drains the matching MidiQueue into `midi`.
    // The MIDI queue is read before the read-index is advanced so the slot
    // stays owned by the sandbox until both audio and MIDI are consumed.
    bool popInputBlock(juce::AudioBuffer<float>& dst,
                       juce::MidiBuffer& midi,
                       int numSamples, int timeoutMs);

    // Wake the sandbox audio thread out of its popInputBlock wait without
    // pushing a real block. Used by the host-side audio-thread pause/drain
    // protocol so non-realtime control ops don't have to wait the full
    // popInputBlock timeout for the audio worker to notice the pause flag.
    // Sandbox-side: also called on shutdown to break the loop's WaitFor.
    void signalSandboxWake();

    const AudioDimensions& dims() const noexcept { return cachedDims; }

    // Test/diagnostic readers for the shared header's cumulative counters.
    // Safe to call from either side (both map the same object); return 0 when
    // the channel isn't mapped. Used by the loopback test in place of
    // re-opening the shm by name (which POSIX anonymous shm can't do).
    uint64_t diagMidiOverflows() const noexcept;
    uint64_t diagXruns() const noexcept;
    uint64_t diagDropouts() const noexcept;

    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    AudioDimensions cachedDims;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioChannel)
};

} // namespace slopsmith::sandbox
