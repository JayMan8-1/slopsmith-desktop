# Tests for the sandbox IPC path. The audio-ring loopback is cross-platform;
# the control-channel + posix_spawn smoke tests are POSIX-only (see
# tests/sandbox/CMakeLists.txt). Built by default alongside the addon — opt-out
# with -DSLOPSMITH_BUILD_TESTS=OFF.
#
# Run from the build directory: `ctest --output-on-failure -C Release`.

option(SLOPSMITH_BUILD_TESTS "Build sandbox unit tests" ON)

if(NOT SLOPSMITH_BUILD_TESTS)
    return()
endif()

# Pure-helper tests (no JUCE / no platform deps) build everywhere.
add_subdirectory(audio_sanitize)

# Note: enable_testing() lives in the top-level CMakeLists.txt — calling it
# only here would register tests in build/tests/CTestTestfile.cmake but
# `ctest` from the root build directory wouldn't discover them.
add_subdirectory(sandbox)
