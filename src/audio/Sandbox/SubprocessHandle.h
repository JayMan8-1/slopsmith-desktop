// SubprocessHandle — owns a slopsmith-vst-host.exe subprocess and observes its
// lifetime. Spawn via `start()`; the destructor performs a graceful close
// (terminate-after-timeout) if the process is still running.

#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace slopsmith::sandbox {

class SubprocessHandle
{
public:
    SubprocessHandle();
    ~SubprocessHandle();

    // Spawn the subprocess. `args` are the command-line arguments (the exe
    // path is implicit; callers pass it in `exePath`). The exit watcher
    // thread reports unexpected exits via `onExit`. Windows: inherits nothing
    // (the sandbox connects to all IPC objects by name). On POSIX use
    // startPosix — the IPC objects are fd-passed, not named.
    bool start(const juce::String& exePath,
               const juce::StringArray& args,
               std::function<void(int exitCode)> onExit,
               juce::String& errorOut);

   #if ! JUCE_WINDOWS
    // One fd to hand to the child: dup2(hostFd → childFd) in the spawned
    // process, with childFd left non-close-on-exec so it survives the exec.
    // The child learns its fd numbers from argv. hostFd stays owned by the
    // caller (posix_spawn dup2()s a copy; close yours afterwards).
    struct InheritedFd { int childFd; int hostFd; };

    // POSIX spawn with explicit fd inheritance. On macOS POSIX_SPAWN_CLOEXEC_
    // DEFAULT makes every fd close-on-exec by default so ONLY the dup2()'d fds
    // below reach the child (the analog of Windows bInheritHandles=FALSE); on
    // Linux that flag is absent, so the caller must keep its other fds
    // CLOEXEC (the channels do). Uses posix_spawn — never a bare fork —
    // because the host has touched CoreAudio/Obj-C and fork-without-exec is
    // unsafe there.
    bool startPosix(const juce::String& exePath,
                    const juce::StringArray& args,
                    const std::vector<InheritedFd>& inherited,
                    std::function<void(int exitCode)> onExit,
                    juce::String& errorOut);
   #endif

    // Escalating graceful close. Windows: post WM_QUIT, then TerminateProcess
    // after `timeoutMs`. POSIX: SIGTERM, then SIGKILL after `timeoutMs`.
    // (Callers should send the `shutdown` control op first for a clean exit;
    // this is the backstop.)
    void shutdown(int timeoutMs);

    bool isRunning() const noexcept { return running.load(std::memory_order_acquire); }
    // Windows DWORD is unsigned 32-bit; storing as int would silently
    // narrow a high PID into a negative value when surfaced via pid().
    uint32_t pid() const noexcept { return cachedPid; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> running{false};
    uint32_t cachedPid = 0;
    std::function<void(int)> onExitCb;
    std::thread watcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubprocessHandle)
};

} // namespace slopsmith::sandbox
