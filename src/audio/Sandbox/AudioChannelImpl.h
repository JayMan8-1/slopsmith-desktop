// AudioChannel::Impl — private OS-handle wrapper, shared between the
// platform-neutral ring logic (AudioChannel_shared.cpp) and the per-platform
// create/open/close + doorbell signalling (AudioChannel_{win,posix}.cpp).
//
// This header is internal to the AudioChannel translation units; it is NOT
// part of the public AudioChannel.h surface (it would otherwise drag
// <windows.h> / POSIX fd semantics into every includer of AudioChannel.h).
//
// The ring methods touch only the neutral mapped-region pointers plus the two
// platform doorbell primitives (`signalEvent` / `waitEvent`); everything
// genuinely OS-specific lives behind those two calls + the create/open/close
// trio so the lock-free ring algorithm is written once.

#pragma once

#include "AudioChannel.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include <cstddef>

namespace slopsmith::sandbox {

struct AudioChannel::Impl
{
    // Neutral mapped-region pointers, set by createHostSide / openSandboxSide
    // and cleared by close(). The ring algorithm in AudioChannel_shared.cpp
    // uses only these.
    void*           view       = nullptr;
    AudioShmHeader* header     = nullptr;
    float*          inputRing  = nullptr;   // host writes, sandbox reads
    float*          outputRing = nullptr;   // sandbox writes, host reads
    MidiQueue*      midiQueues = nullptr;   // [maxBlocks], one per input slot

#if JUCE_WINDOWS
    HANDLE mapping      = nullptr;
    HANDLE evtToHost    = nullptr;          // sandbox→host (output ready)
    HANDLE evtToSandbox = nullptr;          // host→sandbox (input ready)
#else
    // Anonymous shm object: shm_open()'d, ftruncate()'d, then immediately
    // shm_unlink()'d so it has no lingering name to leak on a crash; the fd
    // keeps the object alive and is dup2()'d into the sandbox child.
    int    shmFd       = -1;
    size_t mappedBytes = 0;                 // for munmap on close()

    // Our end of the bidirectional doorbell socketpair. Because a socketpair
    // delivers each side only what the *other* side wrote, one fd per process
    // multiplexes both cross-process directions: writing wakes the peer's
    // waitEvent, reading drains the peer's wakes. Non-blocking; SO_NOSIGPIPE /
    // MSG_NOSIGNAL keep a dead peer from killing us with SIGPIPE.
    int    evtFd       = -1;

    // Same-process self-wake pipe. signalSandboxWake() must break OUR OWN
    // waiter (the audio worker's popInputBlock) without data — but a write to
    // `evtFd` goes to the *peer*, not back to us, so it can't self-wake (and
    // worse, would spuriously wake the peer). The self-pipe is the POSIX analog
    // of the Win32 auto-reset event's self-signal: waitEvent polls evtFd AND
    // selfWake[0]; wakeSelf() writes selfWake[1].
    int    selfWake[2] = { -1, -1 };
#endif

    // Doorbell. signalEvent wakes the consumer of `isOutputRing`; waitEvent
    // blocks the consumer until the producer signals (or timeoutMs elapses,
    // returning false). On POSIX `isOutputRing` is irrelevant — the single
    // socketpair fd carries both directions — but the parameter keeps the
    // Windows two-event mapping expressible. Both are no-ops / immediate
    // failures once close() has run.
    void signalEvent(bool isOutputRing);
    bool waitEvent(bool isOutputRing, int timeoutMs);

    // Break OUR OWN waitEvent (the audio worker's popInputBlock) without
    // publishing a block — used by the sandbox-side pause/drain + shutdown.
    // Windows: SetEvent(evtToSandbox) (auto-reset events self-signal fine).
    // POSIX: write the self-pipe (a socketpair self-write would hit the peer).
    void wakeSelf();
};

} // namespace slopsmith::sandbox
