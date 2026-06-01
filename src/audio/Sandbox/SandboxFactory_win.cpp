// Sandbox factory — Windows resolveSandboxExe(). The routing policy
// (shouldSandbox / tryLoadSandboxed / the crash blocklist) is platform-neutral
// and lives in SandboxFactory_shared.cpp.

#include "SandboxedProcessor.h"
#include "../VSTTrace.h"

#include <juce_core/juce_core.h>
#include <vector>     // dynamic buffer for the module-path + env-var lookups
#include <windows.h>  // GetModuleHandleExW / GetModuleFileNameW

namespace slopsmith::sandbox {

juce::File resolveSandboxExe()
{
    // Locate the directory of the .node DLL via GetModuleHandleEx with this
    // function's address — juce::File::currentExecutableFile returns the host's
    // exe (node.exe / electron.exe) when we're loaded as an addon, which points
    // at the wrong directory entirely.
    juce::File addonDir;
    HMODULE selfModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                              | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&resolveSandboxExe),
                           &selfModule)
        && selfModule != nullptr)
    {
        // Grow the buffer until GetModuleFileNameW returns < capacity. MAX_PATH
        // is the floor for back-compat; long-paths-enabled installs / deep dev
        // trees can blow past it. Cap at 32K (Windows long-path ceiling).
        std::vector<wchar_t> buf(MAX_PATH + 1);
        for (;;)
        {
            const DWORD n = GetModuleFileNameW(selfModule, buf.data(),
                                               (DWORD)buf.size());
            if (n > 0 && n < buf.size())
            {
                addonDir = juce::File(juce::String(buf.data())).getParentDirectory();
                break;
            }
            if (buf.size() >= 32768) break;
            buf.resize(buf.size() * 2);
        }
    }

    if (addonDir.exists())
    {
        auto candidate = addonDir.getChildFile("slopsmith-vst-host.exe");
        if (candidate.existsAsFile()) return candidate;
    }

    // Explicit dev override — opt-in via SLOPSMITH_DEV_SANDBOX_PATH. The
    // previous implicit CWD probe was a search-path attack vector; fail closed
    // in production, require the env var for dev workflows.
    const wchar_t* kVar = L"SLOPSMITH_DEV_SANDBOX_PATH";
    const DWORD probe = GetEnvironmentVariableW(kVar, nullptr, 0);
    if (probe > 0)
    {
        std::vector<wchar_t> buf(probe);
        const DWORD got = GetEnvironmentVariableW(kVar, buf.data(), probe);
        if (got > 0 && got < probe)
        {
            const juce::File explicitPath{ juce::String(buf.data()) };
            if (explicitPath.existsAsFile()) return explicitPath;
        }
        else
        {
            VST_TRACE("SandboxFactory: SLOPSMITH_DEV_SANDBOX_PATH read race "
                      "(probe=%lu, got=%lu)",
                      (unsigned long)probe, (unsigned long)got);
        }
    }

    return {};
}

} // namespace slopsmith::sandbox
