// control_channel_test — exercise the ControlChannel request/reply/event
// machinery + transport over an in-process loopback, without spawning a
// subprocess. The "host" (server) and "sandbox" (client) ControlChannels are
// wired together through the POSIX socketpair handoff (createServerSide →
// sandboxFd → connectClientSideFd), so the framing, the poll()-driven I/O
// thread, the pending-promise map, and peer-close detection all run for real.
//
// POSIX-only: it uses connectClientSideFd / sandboxFd (the Windows transport
// is a named pipe re-opened by name and is covered by its own path).

#include <juce_core/juce_core.h>

#include "../../src/audio/Sandbox/Protocol.h"
#include "../../src/audio/Sandbox/ControlChannel.h"

#include <atomic>
#include <chrono>
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
#define REQUIRE(cond) \
    do { if (!(cond)) { check(false, #cond, __FILE__, __LINE__); return; } } while (0)

// Spin-wait up to timeoutMs for a predicate to hold. Keeps the tests free of
// fixed sleeps that would be either flaky or slow.
template <typename Pred>
bool waitFor(Pred p, int timeoutMs = 2000)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return p();
}

// A connected host+sandbox ControlChannel pair over a socketpair. The sandbox
// side installs an echo request handler; the host side records events +
// disconnects.
struct ChannelPair
{
    ControlChannel host;     // server
    ControlChannel sandbox;  // client
    juce::String err;
    bool ok = false;

    std::atomic<int> hostEventCount{0};
    juce::String lastEvent;
    std::atomic<int> sandboxRequestCount{0};
    std::atomic<bool> hostDisconnected{false};
    juce::String hostDisconnectReason;

    ChannelPair()
    {
        juce::String unusedName;
        if (!host.createServerSide(unusedName, err)) return;
        if (!sandbox.connectClientSideFd(host.sandboxFd(), err)) return;

        // Sandbox echoes "ping" args back; rejects anything else; counts
        // fire-and-forget "noop". Must be installed before start().
        sandbox.setRequestHandler([this](int id, const juce::String& op,
                                         const juce::var& args)
        {
            ++sandboxRequestCount;
            if (id < 0) return;                 // fire-and-forget, no reply
            if (op == "ping") sandbox.sendReply(id, true, args);
            else              sandbox.sendReply(id, false, {}, "unknown op");
        });

        const bool sb = sandbox.start(
            /*onEvent*/ [](const juce::String&, const juce::var&) {},
            /*onDisconnect*/ [](const juce::String&) {});
        const bool hb = host.start(
            [this](const juce::String& ev, const juce::var&)
            {
                lastEvent = ev;
                ++hostEventCount;
            },
            [this](const juce::String& reason)
            {
                hostDisconnectReason = reason;
                hostDisconnected.store(true);
            });
        ok = sb && hb;
        if (!ok)
            err = "start failed: host=" + host.getLastStartError()
                + " sandbox=" + sandbox.getLastStartError();
    }

    ~ChannelPair()
    {
        // Stop both channels (joining their I/O threads) BEFORE the recording
        // members below are destroyed — the I/O threads' onEvent/onDisconnect
        // callbacks capture `this` and write those members. Stop the host
        // first: host.stop() clears `alive`, so a teardown-triggered failWith
        // becomes a no-op and never touches our fields. This mirrors the real
        // SandboxedProcessor::teardown ordering invariant.
        host.stop();
        sandbox.stop();
    }
};

void testRequestReply()
{
    std::printf("test: request/reply round-trip (echo)\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    juce::DynamicObject::Ptr argObj(new juce::DynamicObject());
    argObj->setProperty("n", 42);
    argObj->setProperty("s", "hello");
    juce::String reqErr;
    juce::var result = pair.host.request("ping", juce::var(argObj.get()),
                                         /*timeoutMs*/ 2000, &reqErr);
    CHECK(reqErr.isEmpty());
    CHECK(result.isObject());
    CHECK((int)result.getProperty("n", -1) == 42);
    CHECK(result.getProperty("s", "").toString() == "hello");
}

void testRequestError()
{
    std::printf("test: request to unknown op returns error\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    juce::String reqErr;
    juce::var result = pair.host.request("nope", juce::var(), 2000, &reqErr);
    CHECK(result.isVoid());
    CHECK(reqErr == "unknown op");
}

void testEvent()
{
    std::printf("test: sandbox-originated event reaches host\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    juce::DynamicObject::Ptr data(new juce::DynamicObject());
    data->setProperty("pluginName", "TestPlug");
    CHECK(pair.sandbox.sendEvent(event::kReady, juce::var(data.get())));

    CHECK(waitFor([&] { return pair.hostEventCount.load() >= 1; }));
    CHECK(pair.lastEvent == juce::String(event::kReady));
}

void testPostNoReply()
{
    std::printf("test: fire-and-forget reaches the sandbox handler\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    CHECK(pair.host.postNoReply("noop", juce::var()));
    CHECK(waitFor([&] { return pair.sandboxRequestCount.load() >= 1; }));
}

void testManyRequests()
{
    std::printf("test: 500 sequential requests, all matched\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    int okCount = 0;
    for (int i = 0; i < 500; ++i)
    {
        juce::DynamicObject::Ptr a(new juce::DynamicObject());
        a->setProperty("n", i);
        juce::String e;
        juce::var r = pair.host.request("ping", juce::var(a.get()), 2000, &e);
        if (e.isEmpty() && (int)r.getProperty("n", -1) == i) ++okCount;
    }
    CHECK(okCount == 500);
}

void testPeerClosedDetected()
{
    std::printf("test: sandbox close → host sees peer-closed disconnect\n");
    ChannelPair pair;
    REQUIRE(pair.ok);

    // Tear down the sandbox end; the host I/O thread should read EOF and
    // classify it as a clean peer close (not a read error).
    pair.sandbox.stop();
    CHECK(waitFor([&] { return pair.hostDisconnected.load(); }));
    CHECK(pair.hostDisconnectReason == ControlChannel::kReasonPeerClosed);
}

} // namespace

int main()
{
    std::printf("=== control_channel_test ===\n");
    testRequestReply();
    testRequestError();
    testEvent();
    testPostNoReply();
    testManyRequests();
    testPeerClosedDetected();
    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
