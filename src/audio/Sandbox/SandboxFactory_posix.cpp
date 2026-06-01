// Sandbox factory — POSIX (macOS + Linux) resolveSandboxExe(). The routing
// policy lives in SandboxFactory_shared.cpp.
//
// Replaces the old SandboxFactory_stub.cpp: macOS/Linux now route VST3 plugins
// through the out-of-process sandbox (slopsmith-vst-host) like Windows.

#include "SandboxedProcessor.h"
#include "../VSTTrace.h"

#include <juce_core/juce_core.h>
#include <cstdlib>   // getenv
#include <dlfcn.h>   // dladdr

namespace slopsmith::sandbox {

juce::File resolveSandboxExe()
{
    // Locate the directory of the addon shared object via dladdr on this
    // function's address — juce::File::currentExecutableFile returns the host
    // process (Electron / node), not the .node, when we're loaded as an addon.
    // (The Windows backend uses GetModuleHandleEx for the same reason.)
    juce::File addonDir;
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&resolveSandboxExe), &info) != 0
        && info.dli_fname != nullptr)
    {
        addonDir = juce::File(juce::String::fromUTF8(info.dli_fname))
                       .getParentDirectory();
    }

    if (addonDir.exists())
    {
        auto candidate = addonDir.getChildFile("slopsmith-vst-host");
        if (candidate.existsAsFile()) return candidate;
    }

    // Explicit dev override — opt-in via SLOPSMITH_DEV_SANDBOX_PATH (matches the
    // Windows backend; fail closed in production rather than probing the CWD).
    if (const char* env = ::getenv("SLOPSMITH_DEV_SANDBOX_PATH"))
    {
        const juce::File explicitPath{ juce::String::fromUTF8(env) };
        if (explicitPath.existsAsFile()) return explicitPath;
    }

    return {};
}

} // namespace slopsmith::sandbox
