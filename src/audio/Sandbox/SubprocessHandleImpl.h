// SubprocessHandle::Impl — private OS process-handle wrapper, shared between
// SubprocessHandle_{win,posix}.cpp. Internal to those TUs; not part of the
// public header.

#pragma once

#include "SubprocessHandle.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <sys/types.h>   // pid_t
#endif

namespace slopsmith::sandbox {

struct SubprocessHandle::Impl
{
#if JUCE_WINDOWS
    PROCESS_INFORMATION pi{};
#else
    pid_t pid = -1;
#endif
};

} // namespace slopsmith::sandbox
