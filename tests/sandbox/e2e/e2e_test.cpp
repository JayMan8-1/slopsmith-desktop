// e2e: drive a real SandboxedProcessor (host side) that spawns the real
// slopsmith-vst-host child, which loads the passthrough VST3 and processes
// audio over the shm ring. Proves the whole POSIX runtime: posix_spawn + fd
// inheritance + ready handshake + prepare + audio round-trip + state + shutdown.
//
//   argv[1] = path to slopsmith-vst-host
//   argv[2] = path to SlopPassThrough.vst3
#include "Sandbox/SandboxedProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace slopsmith::sandbox;

static int g_pass = 0, g_fail = 0;
static void check(bool c, const char* what, int line)
{
    if (c) { ++g_pass; return; }
    ++g_fail; std::fprintf(stderr, "  FAIL: %s (line %d)\n", what, line);
}
#define CHECK(c) check((c), #c, __LINE__)

static bool allClose(const juce::AudioBuffer<float>& b, float v)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs(b.getSample(ch, i) - v) > 1.0e-4f) return false;
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: e2e_test <vst-host> <plugin.vst3>\n"); return 2; }

    SandboxedProcessor::SpawnConfig cfg;
    cfg.pluginPath     = juce::String::fromUTF8(argv[2]);
    cfg.pluginName     = "PassThrough";
    cfg.sandboxExePath = juce::String::fromUTF8(argv[1]);
    cfg.audio.sampleRate      = 48000;
    cfg.audio.maxBlockSamples = 256;
    cfg.audio.maxChannels     = 2;
    cfg.audio.maxBlocks       = 4;
    cfg.spawnTimeoutMs        = 20000;

    std::printf("=== sandbox e2e: spawn → process → state → shutdown ===\n");
    juce::String err;
    auto sb = SandboxedProcessor::spawn(cfg, err);
    CHECK(sb != nullptr);
    if (!sb) { std::fprintf(stderr, "spawn failed: %s\n", err.toRawUTF8()); return 1; }
    CHECK(sb->isAlive());

    sb->prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;

    // Pace at one block period (256 samples @ 48 kHz ≈ 5.33 ms, rounded up to
    // 6 ms) so the host doesn't outrun the sandbox worker — a faster cadence
    // would let the host's pop legitimately time out and read silence.
    constexpr int kBlockPeriodMs = 6;

    // A single constant level feeds both the warm-up and the steady-state loop.
    // The sandbox is two independent rings (input, output), so it promises
    // *bounded latency*, NOT exact per-block phase: if any block's round-trip
    // overruns the pop timeout, the host inserts silence and moves on while the
    // worker still produces that block's output, which shifts every later read
    // one slot late. A distinct-per-block probe would then read the *previous*
    // block's (valid, non-silent) output and flag it as a spurious mismatch —
    // observed as a flaky "200 misvalued" on loaded CI runners. A constant
    // level is phase-invariant: a lagged read still equals 2×kLevel (correct),
    // a timed-out block is still silence (dropout), and a genuine scaling bug
    // still produces a wrong value. In-phase slot correctness with distinct
    // markers is covered by the deterministic standalone ring unit test.
    constexpr float kLevel = 0.3f;

    // Warm-up: a plugin's first few processBlock calls (VST3 activation,
    // allocation, first-touch) can exceed one block period, so the sandbox
    // inserts silence for those by design. Discard a warm-up burst so the
    // steady-state assertions aren't measuring cold start.
    for (int n = 0; n < 40; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i) buf.setSample(ch, i, kLevel);
        sb->processBlock(buf, midi);
        std::this_thread::sleep_for(std::chrono::milliseconds(kBlockPeriodMs));
    }

    // Steady state. Each delivered block MUST be exactly 2×kLevel (a real
    // scaling bug surfaces as a wrong non-zero value → `misvalued`, which must
    // be zero). A block that times out under load is returned as silence by
    // SandboxedProcessor (by design) → counted as a dropout, tolerated in small
    // numbers since a shared CI runner can stall a single round-trip past the
    // pop timeout even when the runtime is correct.
    int correct = 0, dropouts = 0, misvalued = 0;
    constexpr int kBlocks = 200;
    for (int n = 0; n < kBlocks; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i) buf.setSample(ch, i, kLevel);
        sb->processBlock(buf, midi);
        if      (allClose(buf, kLevel * 2.0f)) ++correct;
        else if (allClose(buf, 0.0f))          ++dropouts;   // timed-out → silence
        else                                   ++misvalued;  // wrong value → real bug
        std::this_thread::sleep_for(std::chrono::milliseconds(kBlockPeriodMs));
    }
    std::printf("  steady-state: %d correct, %d dropouts, %d misvalued (of %d)\n",
                correct, dropouts, misvalued, kBlocks);
    CHECK(misvalued == 0);                     // every delivered block is exact
    CHECK(correct >= kBlocks * 9 / 10);        // overwhelmingly delivered (tolerate CI jitter)

    // State round-trip: child returns the plugin's getStateInformation blob.
    juce::MemoryBlock state;
    sb->getStateInformation(state);
    CHECK(state.getSize() > 0);
    sb->setStateInformation(state.getData(), (int)state.getSize());
    CHECK(sb->isAlive());   // setState shouldn't have torn the sandbox down

   #if JUCE_MAC
    // Editor open/close protocol (macOS): the child becomes a foreground app
    // and opens a floating NSWindow. Proves the kOpenEditor round-trip +
    // editorOpen tracking + kCloseEditor. Visual focus/DPI is the one thing a
    // headless runner can't verify (manual on a real Mac).
    CHECK(sb->hasEditor());
    const bool opened = sb->requestOpenEditor();
    CHECK(opened);
    CHECK(sb->isEditorOpen());
    sb->requestCloseEditor();
    CHECK(!sb->isEditorOpen());
    CHECK(sb->isAlive());   // open/close must not crash the child
   #elif JUCE_LINUX
    // Linux: the sandbox doesn't advertise an editor (JUCE VST3 editor hosting
    // on X11 is too fragile — see SandboxedProcessor::hasEditor). Audio + state
    // are covered above. (Scoped to JUCE_LINUX, not a bare #else: on Windows
    // hasEditor() returns the cached value, so a `!hasEditor()` assertion would
    // be wrong there — this harness is POSIX-only via CMake regardless.)
    CHECK(! sb->hasEditor());
   #endif

    sb.reset();   // destructor → shutdown op → SIGTERM ladder; must not hang

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
