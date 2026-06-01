// audio_channel_midi_test — exercise pushInputBlock / popInputBlock + the
// global midiOverflows counter without spawning a subprocess.
//
// Closes the v2/v3 review-thread concern that the inline-MIDI path had no
// automated coverage (the existing GR6 smoke driver only pushes empty
// MidiBuffers). Both ends of an AudioChannel are opened in the same
// process — createHostSide on one instance, openSandboxSide on a second
// instance using the same Names — so we don't need a real spawn.
//
// Win32-only for the same reason AudioChannel.cpp is.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../../src/audio/Sandbox/Protocol.h"
#include "../../src/audio/Sandbox/AudioChannel.h"

#include <atomic>
#include <cstdio>
#include <thread>

using namespace slopsmith::sandbox;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool cond, const char* what, const char* file, int line)
{
    if (cond) { ++g_passed; return; }
    ++g_failed;
    std::fprintf(stderr, "  FAIL: %s  (%s:%d)\n", what, file, line);
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

// REQUIRE = fatal CHECK: bails the current test on failure so a busted
// setup precondition (e.g., HeaderPeek failing to open the mapping) doesn't
// cascade into a NULL deref + a barrage of misleading follow-on failures.
// Use for everything that subsequent test lines dereference / depend on.
#define REQUIRE(cond) \
    do { if (!(cond)) { check(false, #cond, __FILE__, __LINE__); return; } } while (0)

// Helper: open a fresh host+sandbox AudioChannel pair with a given dims, run
// a callback against both ends, then tear down. The pair is unique per call
// (suffix-randomised mapping name) so concurrent test runs don't collide.
struct ChannelPair
{
    AudioChannel host;
    AudioChannel sandbox;
    AudioChannel::Names names;
    AudioDimensions dims;
    juce::String err;
    bool ok = false;

    explicit ChannelPair(const AudioDimensions& d) : dims(d)
    {
        ok = host.createHostSide(dims, names, err);
        if (!ok)
        {
            std::fprintf(stderr, "  ChannelPair: createHostSide failed: %s\n",
                         err.toRawUTF8());
            return;
        }
        ok = sandbox.openSandboxSide(names, err);
        if (!ok)
        {
            std::fprintf(stderr, "  ChannelPair: openSandboxSide failed: %s\n",
                         err.toRawUTF8());
            // host's named mapping + events are released by AudioChannel's
            // destructor when this ChannelPair goes out of scope (sandbox
            // first, then host, per reverse-declaration-order rules).
            // Names are randomised per ChannelPair so an aborted construct
            // doesn't leak into a subsequent test in the same run.
        }
    }
};

void testRoundtripSmallBuffer()
{
    std::printf("test: roundtrip small MidiBuffer (count, frames, bytes)\n");
    AudioDimensions dims;            // defaults: 4 blocks × 1024 samples × 2 ch
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 256);
    srcAudio.clear();
    juce::MidiBuffer midi;
    // 3 events at distinct frames — Note On, CC, Note Off.
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 64), 64);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 200);

    const uint64_t overflowsBefore = pair.host.diagMidiOverflows();

    REQUIRE(pair.host.pushInputBlock(srcAudio, midi, 256));

    juce::AudioBuffer<float> dstAudio((int)dims.maxChannels, 256);
    juce::MidiBuffer drained;
    REQUIRE(pair.sandbox.popInputBlock(dstAudio, drained, 256, /*timeoutMs*/ 1000));

    int n = 0;
    int frames[3] = {-1, -1, -1};
    juce::uint8 firstByte[3] = {0, 0, 0};
    for (const auto& meta : drained)
    {
        if (n < 3) { frames[n] = meta.samplePosition;
                     firstByte[n] = meta.getMessage().getRawData()[0]; }
        ++n;
    }
    CHECK(n == 3);
    CHECK(frames[0] == 0);
    CHECK(frames[1] == 64);
    CHECK(frames[2] == 200);
    // Note On status nibble = 0x90, CC = 0xB0, Note Off = 0x80.
    CHECK((firstByte[0] & 0xF0) == 0x90);
    CHECK((firstByte[1] & 0xF0) == 0xB0);
    CHECK((firstByte[2] & 0xF0) == 0x80);

    // No overflows expected on the happy path.
    const uint64_t overflowsAfter = pair.host.diagMidiOverflows();
    CHECK(overflowsAfter == overflowsBefore);
}

void testSysExBumpsOverflow()
{
    std::printf("test: SysEx-sized event drops + bumps midiOverflows\n");
    AudioDimensions dims;
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 256);
    srcAudio.clear();
    juce::MidiBuffer midi;
    // SysEx — JUCE wraps the payload with F0/F7 framing, so a 3-byte
    // payload becomes a 5-byte raw message (> kMidiEventMaxBytes = 4),
    // which pushInputBlock should drop and bump midiOverflows.
    const juce::uint8 sysexPayload[] = { 0x7E, 0x7F, 0x06 };
    midi.addEvent(juce::MidiMessage::createSysExMessage(sysexPayload, 3), 32);
    // Plus a normal CC event at frame 100 — should round-trip.
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, 64), 100);

    const uint64_t overflowsBefore = pair.host.diagMidiOverflows();

    REQUIRE(pair.host.pushInputBlock(srcAudio, midi, 256));

    juce::AudioBuffer<float> dstAudio((int)dims.maxChannels, 256);
    juce::MidiBuffer drained;
    REQUIRE(pair.sandbox.popInputBlock(dstAudio, drained, 256, 1000));

    int n = 0;
    for ([[maybe_unused]] const auto& meta : drained) ++n;
    CHECK(n == 1);  // SysEx dropped, CC survives.

    const uint64_t overflowsAfter = pair.host.diagMidiOverflows();
    CHECK(overflowsAfter == overflowsBefore + 1);
}

void testOverCapBumpsOverflow()
{
    std::printf("test: events past kMidiEventsPerSlot drop + bump overflows\n");
    AudioDimensions dims;
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 256);
    srcAudio.clear();
    juce::MidiBuffer midi;
    // Push kMidiEventsPerSlot + 8 events — the trailing 8 should be dropped.
    constexpr int kExtra = 8;
    const int total = (int)kMidiEventsPerSlot + kExtra;
    for (int i = 0; i < total; ++i)
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, i & 0x7F), i % 256);

    const uint64_t overflowsBefore = pair.host.diagMidiOverflows();

    REQUIRE(pair.host.pushInputBlock(srcAudio, midi, 256));

    juce::AudioBuffer<float> dstAudio((int)dims.maxChannels, 256);
    juce::MidiBuffer drained;
    REQUIRE(pair.sandbox.popInputBlock(dstAudio, drained, 256, 1000));

    int n = 0;
    for ([[maybe_unused]] const auto& meta : drained) ++n;
    CHECK(n == (int)kMidiEventsPerSlot);

    const uint64_t overflowsAfter = pair.host.diagMidiOverflows();
    CHECK(overflowsAfter == overflowsBefore + (uint64_t)kExtra);
}

void testFramePastSamplesDropped()
{
    std::printf("test: events past block samples drop + bump overflows\n");
    AudioDimensions dims;
    dims.maxBlockSamples = 128;
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 128);
    srcAudio.clear();
    juce::MidiBuffer midi;
    // Caller passes numSamples=128 (within cap). Events at frames >= 128
    // should DROP rather than clamp into the audible portion (which would
    // silently re-time them, the worse failure mode).
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 50);   // in-range
    midi.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 127);  // last in-range frame
    midi.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 128);  // out-of-range (= samples)
    midi.addEvent(juce::MidiMessage::noteOn(1, 63, (juce::uint8)100), 200);  // out-of-range

    const uint64_t overflowsBefore = pair.host.diagMidiOverflows();

    REQUIRE(pair.host.pushInputBlock(srcAudio, midi, 128));

    juce::AudioBuffer<float> dstAudio((int)dims.maxChannels, 128);
    juce::MidiBuffer drained;
    REQUIRE(pair.sandbox.popInputBlock(dstAudio, drained, 128, 1000));

    int n = 0;
    int lastFrame = -1;
    for (const auto& meta : drained) { ++n; lastFrame = meta.samplePosition; }
    CHECK(n == 2);                    // events at 50 and 127
    CHECK(lastFrame == 127);          // 128 and 200 dropped, NOT clamped to 127

    const uint64_t overflowsAfter = pair.host.diagMidiOverflows();
    CHECK(overflowsAfter == overflowsBefore + 2);
}

void testNumSamplesOverCapRejected()
{
    std::printf("test: numSamples > maxSamples rejected up front\n");
    AudioDimensions dims;
    dims.maxBlockSamples = 128;
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 256);
    srcAudio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 50);

    // Caller passes numSamples=256 but spawn cap is 128. Old behavior was
    // silently truncate audio + drop MIDI in [128, 256). New behavior:
    // return false up front so the misuse is visible to the caller. No
    // shm counter is bumped (caller misuse is a distinct class from
    // real-dropout / ring-full, and dropouts/xruns are reserved for
    // those — see the comment in pushInputBlock).
    CHECK(! pair.host.pushInputBlock(srcAudio, midi, 256));
}

void testSlotReuseAcrossWraparound()
{
    // Push/pop more blocks than the ring has slots so each slot is used
    // multiple times. Catches a regression in the "count is always
    // overwritten on push" invariant — if pushInputBlock ever skipped the
    // count store on a slot whose prior cycle had MIDI events, the next
    // pop would replay those stale events against the fresh audio.
    std::printf("test: slot reuse across ring wrap-around (no MIDI leakage)\n");
    AudioDimensions dims;
    // Pin maxBlocks explicitly: the modulus-coprime reasoning below depends
    // on it. If AudioDimensions{}'s default ever changes, this test would
    // silently stop exercising the slot-reuse-with-different-counts property.
    constexpr uint32_t kRingSize = 4;
    dims.maxBlocks = kRingSize;
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    juce::AudioBuffer<float> srcAudio((int)dims.maxChannels, 256);
    srcAudio.clear();
    juce::AudioBuffer<float> dstAudio((int)dims.maxChannels, 256);

    // Run enough cycles for every slot to be reused multiple times.
    // 3*maxBlocks + 2 = 14 cycles with maxBlocks=4 means each slot is hit
    // 3 or 4 times.
    const int kCycles = 3 * (int)dims.maxBlocks + 2;

    // Vary the MIDI count per block so a leaked stale count from a prior
    // cycle on the SAME slot would show up as a wrong-count assertion.
    // Modulus must be COPRIME with maxBlocks (4) — using `i % 4` would
    // make each slot see the same count on every wrap (defeating the
    // test). 5 is coprime with 4: slot 0 across cycles 0/4/8/12 sees
    // counts 0/4/3/2, so a stale count from the prior visit would mismatch.
    constexpr int kEventCountModulus = 5;
    // Real coprimality check (not just oddness — those happen to coincide for
    // kRingSize=4 because 4 = 2², but a future bump to e.g. 6 would let
    // odd-but-not-coprime values like 9 silently slip through and defeat the
    // stale-count detection).
    constexpr auto gcd = [](int a, int b)
    {
        while (b != 0) { a %= b; auto t = a; a = b; b = t; }
        return a;
    };
    static_assert(gcd((int)kRingSize, kEventCountModulus) == 1,
                  "kEventCountModulus must stay coprime with kRingSize — "
                  "otherwise each ring slot sees the same MIDI-event count "
                  "on every wrap and the stale-count regression test "
                  "becomes trivially-passing.");
    for (int i = 0; i < kCycles; ++i)
    {
        juce::MidiBuffer midi;
        const int eventCount = i % kEventCountModulus;   // 0, 1, 2, 3, 4, 0, 1, ...
        for (int e = 0; e < eventCount; ++e)
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, e * 16),
                          e * 32);

        REQUIRE(pair.host.pushInputBlock(srcAudio, midi, 256));

        juce::MidiBuffer drained;
        REQUIRE(pair.sandbox.popInputBlock(dstAudio, drained, 256, 1000));

        int n = 0;
        for ([[maybe_unused]] const auto& meta : drained) ++n;
        CHECK(n == eventCount);
    }
}

void testThreadedProducerConsumer()
{
    // Cross-thread loopback: a producer thread pushes ordered blocks while a
    // consumer thread drains them, both blocking on the real doorbell
    // (Win32 auto-reset events / POSIX socketpair). This is the case the
    // single-threaded tests above can't cover — the producer/consumer
    // happens-before edge runs through the shared atomic write index plus the
    // doorbell wake, and is what ThreadSanitizer actually inspects. Each block
    // carries a unique audio marker + a varying MIDI count so a torn handoff,
    // a dropped/duplicated block, or stale-slot MIDI would surface as a
    // mismatch rather than passing silently.
    std::printf("test: threaded producer/consumer over the doorbell\n");
    AudioDimensions dims;                 // 4 blocks × 1024 samples × 2 ch
    ChannelPair pair{dims};
    REQUIRE(pair.ok);

    constexpr int kBlocks = 4000;
    const int samples = 256;
    std::atomic<bool> producerOk{true};
    std::atomic<int>  mismatches{0};

    std::thread producer([&]
    {
        juce::AudioBuffer<float> src((int)dims.maxChannels, samples);
        for (int i = 0; i < kBlocks; ++i)
        {
            // Unique per-block marker in sample 0 of every channel.
            src.clear();
            for (int ch = 0; ch < (int)dims.maxChannels; ++ch)
                src.setSample(ch, 0, (float)i);

            juce::MidiBuffer midi;
            const int eventCount = i % 7;     // 0..6 events, < kMidiEventsPerSlot
            for (int e = 0; e < eventCount; ++e)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, e & 0x7F),
                              e);             // frames 0..5 < samples

            // The host audio thread would drop on a full ring (xrun); this
            // test wants lossless ordering, so spin-retry until the consumer
            // frees a slot. yield() keeps it from starving the consumer.
            int spins = 0;
            while (!pair.host.pushInputBlock(src, midi, samples))
            {
                std::this_thread::yield();
                if (++spins > 50'000'000) { producerOk.store(false); return; }
            }
        }
    });

    juce::AudioBuffer<float> dst((int)dims.maxChannels, samples);
    for (int i = 0; i < kBlocks; ++i)
    {
        juce::MidiBuffer drained;
        // popInputBlock returns false on a coalesced / spurious doorbell wake
        // (it rechecks the ring index, finds nothing new yet, and returns) —
        // that is NOT a lost block, just "try again", exactly as the real
        // runAudioThread loops. Retry until the real block arrives; the
        // doorbell byte is sticky (socket-buffered) so there is no lost-wakeup
        // window. A genuine stall (producer died) trips the bounded retry cap.
        bool got = false;
        for (int tries = 0; tries < 2'000'000 && !got; ++tries)
        {
            drained.clear();
            got = pair.sandbox.popInputBlock(dst, drained, samples, 5000);
            if (!got) std::this_thread::yield();
        }
        if (!got) { ++mismatches; break; }
        if (dst.getSample(0, 0) != (float)i) ++mismatches;   // ordering / torn handoff
        int n = 0;
        for ([[maybe_unused]] const auto& meta : drained) ++n;
        if (n != i % 7) ++mismatches;                         // stale-slot MIDI
    }

    producer.join();
    CHECK(producerOk.load());
    CHECK(mismatches.load() == 0);
    // xruns are EXPECTED here: the spin-retry producer deliberately hammers a
    // full ring (the real host audio thread would drop instead), so xruns
    // climbing just means back-pressure worked — not asserted. What matters is
    // that every block arrived exactly once, in order, with its MIDI intact.
}

} // namespace

int main()
{
    std::printf("=== audio_channel_midi_test ===\n");
    testRoundtripSmallBuffer();
    testSysExBumpsOverflow();
    testOverCapBumpsOverflow();
    testFramePastSamplesDropped();
    testNumSamplesOverCapRejected();
    testSlotReuseAcrossWraparound();
    testThreadedProducerConsumer();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
