# Standalone bootstrap for the sandbox IPC tests — JUCE only, no cmake-js /
# node-addon-api / ONNX. Lets a Linux-only developer (and the lightweight
# `sandbox.yml` CI job) build + run the loopback / control / spawn tests
# without configuring the whole native addon.
#
# Usage (from the repo root):
#   cmake -S tests/sandbox/standalone -B build/sandbox -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/sandbox
#   ctest --test-dir build/sandbox --output-on-failure
#
# Sanitized variants:
#   cmake -S tests/sandbox/standalone -B build/sandbox-tsan -DSLOPSMITH_SANITIZE=thread
cmake_minimum_required(VERSION 3.22)
project(slopsmith_sandbox_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This file lives at tests/sandbox/standalone — the repo root is three up.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

if(NOT EXISTS "${REPO_ROOT}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE submodule not found at ${REPO_ROOT}/JUCE. "
                        "Run: git submodule update --init --recursive")
endif()
add_subdirectory("${REPO_ROOT}/JUCE" juce_build)

enable_testing()
# Reuse the single source-of-truth test definitions.
add_subdirectory("${REPO_ROOT}/tests/sandbox" sandbox_tests)
