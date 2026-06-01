// spawn_smoke_test — end-to-end across a real process boundary: posix_spawn a
// child (spawn_smoke_child), hand it the sandbox end of a control socketpair by
// fd inheritance, and drive it over ControlChannel. Validates the pieces the
// in-process loopback tests can't: SubprocessHandle::startPosix, fd
// inheritance, exit-code detection (clean + crash), and SIGPIPE suppression on
// a write to a dead peer.
//
// SPAWN_CHILD_PATH is injected by CMake as the absolute path to the child exe.

#include <juce_core/juce_core.h>

#include "../../src/audio/Sandbox/Protocol.h"
#include "../../src/audio/Sandbox/ControlChannel.h"
#include "../../src/audio/Sandbox/SubprocessHandle.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace slopsmith::sandbox;

namespace {

int g_failed = 0;
int g_passed = 0;
void check(bool c, const char* what, const char* file, int line)
{
    if (c) { ++g_passed; return; }
    ++g_failed;
    std::fprintf(stderr, "  FAIL: %s  (%s:%d)\n", what, file, line);
}
#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)
#define REQUIRE(cond) \
    do { if (!(cond)) { check(false, #cond, __FILE__, __LINE__); return; } } while (0)

template <typename Pred>
bool waitFor(Pred p, int timeoutMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return p();
}

#ifndef SPAWN_CHILD_PATH
 #error "SPAWN_CHILD_PATH must be defined by the build (path to spawn_smoke_child)."
#endif

// childFd 3 is the first fd past stdin/stdout/stderr; the child reads the
// number from argv.
constexpr int kChildControlFd = 3;

// Bring up the host control channel and spawn a child wired to it, in the
// required order: createServerSide (makes the socketpair) → host.start (begins
// reading) → startPosix (dup2()s the sandbox fd into the child) → closeSandboxFd
// (drop our copy so the host sees EOF on child death). Returns false on
// failure. `onExit` records the child's exit code.
bool spawnChild(ControlChannel& host, SubprocessHandle& sub,
                ControlChannel::EventCallback onEvent,
                std::function<void(const juce::String&)> onDisconnect,
                std::atomic<int>& exitCode, std::atomic<bool>& exited)
{
    juce::String unusedName, err;
    if (!host.createServerSide(unusedName, err)) return false;
    if (!host.start(std::move(onEvent), std::move(onDisconnect)))
    {
        std::fprintf(stderr, "  spawnChild: host.start failed: %s\n",
                     host.getLastStartError().toRawUTF8());
        return false;
    }

    juce::StringArray args;
    args.add("--control-fd");
    args.add(juce::String(kChildControlFd));

    std::vector<SubprocessHandle::InheritedFd> inherited{
        { kChildControlFd, host.sandboxFd() }
    };
    const bool ok = sub.startPosix(SPAWN_CHILD_PATH, args, inherited,
        [&](int code) { exitCode.store(code); exited.store(true); }, err);
    // The host has its own end; close our copy of the child's end so the host
    // observes EOF when the child dies (otherwise crash detection never fires).
    host.closeSandboxFd();
    if (!ok)
        std::fprintf(stderr, "  spawnChild: startPosix failed: %s\n",
                     err.toRawUTF8());
    return ok;
}

void testSpawnHandshakeAndCleanExit()
{
    std::printf("test: spawn → ready handshake → ping round-trip → clean exit\n");
    ControlChannel host;
    SubprocessHandle sub;
    std::atomic<int> exitCode{-999};
    std::atomic<bool> exited{false};
    std::atomic<bool> gotReady{false};

    REQUIRE(spawnChild(host, sub,
        [&](const juce::String& ev, const juce::var&)
        { if (ev == juce::String(event::kReady)) gotReady.store(true); },
        [](const juce::String&) {},
        exitCode, exited));

    // fd inheritance + child connect + event delivery across the process line.
    CHECK(waitFor([&] { return gotReady.load(); }));

    // Bidirectional round-trip over the inherited socket.
    juce::DynamicObject::Ptr a(new juce::DynamicObject());
    a->setProperty("n", 7);
    juce::String e;
    juce::var r = host.request("ping", juce::var(a.get()), 3000, &e);
    CHECK(e.isEmpty());
    CHECK((int)r.getProperty("n", -1) == 7);

    // Ask the child to exit cleanly; watcher should report code 0.
    host.request("exit", juce::var(), 3000, &e);
    CHECK(waitFor([&] { return exited.load(); }));
    CHECK(exitCode.load() == 0);

    host.stop();
}

void testCrashDetection()
{
    std::printf("test: child abort() → watcher reports a non-zero exit\n");
    ControlChannel host;
    SubprocessHandle sub;
    std::atomic<int> exitCode{-999};
    std::atomic<bool> exited{false};
    std::atomic<bool> disconnected{false};

    REQUIRE(spawnChild(host, sub,
        [](const juce::String&, const juce::var&) {},
        [&](const juce::String&) { disconnected.store(true); },
        exitCode, exited));

    // Fire-and-forget abort: the child crashes without replying.
    host.postNoReply("abort", juce::var());

    CHECK(waitFor([&] { return exited.load(); }));
    CHECK(exitCode.load() != 0);                 // SIGABRT → 128 + 6 = 134
    // The host's I/O thread should also see the pipe drop.
    CHECK(waitFor([&] { return disconnected.load(); }));

    host.stop();
}

void testWriteToDeadPeerNoSigpipe()
{
    std::printf("test: writing to a dead child returns false, no SIGPIPE\n");
    ControlChannel host;
    SubprocessHandle sub;
    std::atomic<int> exitCode{-999};
    std::atomic<bool> exited{false};

    REQUIRE(spawnChild(host, sub,
        [](const juce::String&, const juce::var&) {},
        [](const juce::String&) {},
        exitCode, exited));

    // Kill the child and wait for the exit to be observed.
    host.request("exit", juce::var(), 3000);
    CHECK(waitFor([&] { return exited.load(); }));
    // Give the host I/O thread a moment to mark the channel not-alive.
    waitFor([&] { return !host.isAlive(); }, 2000);

    // Writing now must fail gracefully — NOT raise SIGPIPE and kill us. If
    // SIGPIPE weren't suppressed this process would have died before here.
    const bool posted = host.postNoReply("ping", juce::var());
    CHECK(!posted);
    std::printf("  (survived the write to a dead peer; SIGPIPE suppressed)\n");

    host.stop();
}

} // namespace

int main()
{
    std::printf("=== spawn_smoke_test ===\n");
    testSpawnHandshakeAndCleanExit();
    testCrashDetection();
    testWriteToDeadPeerNoSigpipe();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
