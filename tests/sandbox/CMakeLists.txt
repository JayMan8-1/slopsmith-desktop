# Sandbox IPC unit tests. Cross-platform: the audio-ring loopback runs
# everywhere; the control-channel loopback and the posix_spawn smoke test are
# POSIX-only (they use the fd-passed socketpair transport). Standalone console
# exes, exit 0 on pass.
#
# Source paths are CMAKE_CURRENT_SOURCE_DIR-relative so this file works both
# when add_subdirectory()'d from the main build (root CMakeLists → tests/) and
# from the JUCE-only standalone harness (tests/sandbox/standalone/) the CI job
# uses — neither needs cmake-js / node-addon-api / ONNX to build.
#
# Optional sanitizer: -DSLOPSMITH_SANITIZE=address|thread applies the sanitizer
# to these targets only (the CI job builds plain + ASan + TSan variants). TSan
# on the threaded audio loopback is the highest-value artifact here — it is
# what validates the arm64 release/acquire memory ordering a Linux-only dev
# can't otherwise exercise.

if(NOT TARGET juce::juce_core OR NOT TARGET juce::juce_audio_basics)
    message(FATAL_ERROR "sandbox tests: JUCE targets not in scope; this dir "
                        "must be configured after JUCE is added.")
endif()

set(SANDBOX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio/Sandbox")
set(AUDIO_INC   "${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio")

set(SLOPSMITH_SANITIZE "" CACHE STRING
    "Sanitizer for sandbox tests: empty, address, or thread")

# Per-target shared config: include dir, JUCE-headless defs, Release output
# dir, optional sanitizer.
function(slopsmith_configure_sandbox_test target)
    target_include_directories(${target} PRIVATE "${AUDIO_INC}")
    target_compile_definitions(${target} PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/Release"
        OUTPUT_NAME "${target}")
    if(SLOPSMITH_SANITIZE)
        # -fsanitize= is GCC/Clang syntax; MSVC uses a different mechanism and
        # would choke on these flags. The sanitizer runs are POSIX-only CI jobs,
        # so fail loudly rather than emit broken flags on an unsupported toolchain.
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR
                "SLOPSMITH_SANITIZE=${SLOPSMITH_SANITIZE} requires a GCC/Clang "
                "compiler (got ${CMAKE_CXX_COMPILER_ID}); unset it on this toolchain.")
        endif()
        target_compile_options(${target} PRIVATE
            -fsanitize=${SLOPSMITH_SANITIZE} -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=${SLOPSMITH_SANITIZE})
    endif()
endfunction()

# Platform split of the channel backends.
if(WIN32)
    set(AUDIO_CHANNEL_SRC "${SANDBOX_DIR}/AudioChannel_shared.cpp"
                          "${SANDBOX_DIR}/AudioChannel_win.cpp")
else()
    set(AUDIO_CHANNEL_SRC "${SANDBOX_DIR}/AudioChannel_shared.cpp"
                          "${SANDBOX_DIR}/AudioChannel_posix.cpp")
endif()

# --- audio ring loopback (all platforms) ---------------------------------
add_executable(audio_channel_midi_test
    audio_channel_midi_test.cpp
    ${AUDIO_CHANNEL_SRC}
    "${SANDBOX_DIR}/Protocol.cpp")
target_link_libraries(audio_channel_midi_test PRIVATE
    juce::juce_audio_basics juce::juce_core)
slopsmith_configure_sandbox_test(audio_channel_midi_test)
add_test(NAME audio_channel_midi_test
         COMMAND audio_channel_midi_test
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:audio_channel_midi_test>")

# --- control channel + spawn smoke (POSIX only) --------------------------
# The Windows control transport is a named pipe re-opened by name and has no
# in-process fd-handoff path; these loopbacks use connectClientSideFd /
# startPosix, so they are POSIX-only. The Windows transport is covered on the
# Windows CI build of the addon + vst-host.
if(NOT WIN32)
    set(CONTROL_SRC "${SANDBOX_DIR}/ControlChannel_shared.cpp"
                    "${SANDBOX_DIR}/ControlChannel_posix.cpp"
                    "${SANDBOX_DIR}/Protocol.cpp")

    add_executable(control_channel_test
        control_channel_test.cpp ${CONTROL_SRC})
    target_link_libraries(control_channel_test PRIVATE juce::juce_core)
    slopsmith_configure_sandbox_test(control_channel_test)
    add_test(NAME control_channel_test
             COMMAND control_channel_test
             WORKING_DIRECTORY "$<TARGET_FILE_DIR:control_channel_test>")

    # Child helper for the spawn smoke test (not a test itself).
    add_executable(spawn_smoke_child
        spawn_smoke_child.cpp ${CONTROL_SRC})
    target_link_libraries(spawn_smoke_child PRIVATE juce::juce_core)
    slopsmith_configure_sandbox_test(spawn_smoke_child)

    add_executable(spawn_smoke_test
        spawn_smoke_test.cpp
        "${SANDBOX_DIR}/SubprocessHandle_posix.cpp"
        ${CONTROL_SRC})
    target_link_libraries(spawn_smoke_test PRIVATE juce::juce_core)
    slopsmith_configure_sandbox_test(spawn_smoke_test)
    target_compile_definitions(spawn_smoke_test PRIVATE
        SPAWN_CHILD_PATH="$<TARGET_FILE:spawn_smoke_child>")
    add_dependencies(spawn_smoke_test spawn_smoke_child)
    add_test(NAME spawn_smoke_test
             COMMAND spawn_smoke_test
             WORKING_DIRECTORY "$<TARGET_FILE_DIR:spawn_smoke_test>")
endif()
