// spawn_smoke_child — the child half of spawn_smoke_test. Stands in for the
// real slopsmith-vst-host (Slice 2) with the bare minimum: adopt the inherited
// control-socket fd, answer a couple of control ops, and exit. Exercises the
// SubprocessHandle POSIX spawn + fd-inheritance + ControlChannel handshake
// end-to-end across a real process boundary.
//
//   --control-fd N   the dup2()'d socketpair end the parent passed us
//
// Ops it understands (host → child):
//   ping  → echo args back               (round-trip proof)
//   exit  → reply ok, then exit(0)        (clean-shutdown proof)
//   abort → std::abort()                  (crash-detection proof; no reply)

#include <juce_core/juce_core.h>

#include "../../src/audio/Sandbox/Protocol.h"
#include "../../src/audio/Sandbox/ControlChannel.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace slopsmith::sandbox;

int main(int argc, char** argv)
{
    int controlFd = -1;
    for (int i = 1; i < argc - 1; ++i)
        if (juce::String(argv[i]) == "--control-fd")
            controlFd = juce::String(argv[i + 1]).getIntValue();

    if (controlFd < 0)
    {
        std::fprintf(stderr, "spawn_smoke_child: missing --control-fd\n");
        return 2;
    }

    ControlChannel ctl;
    juce::String err;
    if (!ctl.connectClientSideFd(controlFd, err))
    {
        std::fprintf(stderr, "spawn_smoke_child: connect failed: %s\n",
                     err.toRawUTF8());
        return 3;
    }

    std::atomic<bool> quit{false};
    ctl.setRequestHandler([&](int id, const juce::String& op,
                              const juce::var& args)
    {
        if (op == "ping")        { if (id >= 0) ctl.sendReply(id, true, args); }
        else if (op == "exit")   { if (id >= 0) ctl.sendReply(id, true, {});
                                   quit.store(true); }
        else if (op == "abort")  { std::abort(); }       // crash on purpose
        else                     { if (id >= 0) ctl.sendReply(id, false, {},
                                                              "unknown op"); }
    });

    if (!ctl.start(/*onEvent*/ [](const juce::String&, const juce::var&) {},
                   /*onDisconnect*/ [&](const juce::String&) { quit.store(true); }))
    {
        std::fprintf(stderr, "spawn_smoke_child: start failed: %s\n",
                     ctl.getLastStartError().toRawUTF8());
        return 4;
    }

    // Announce readiness, then run until told to exit / the parent drops the
    // pipe. The 30 s safety deadline keeps a buggy test from leaving a zombie.
    ctl.sendEvent(event::kReady, juce::var());

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(30);
    while (!quit.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    ctl.stop();
    return 0;
}
