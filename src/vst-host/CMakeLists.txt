# Out-of-process VST helpers (optional targets; parent CMakeLists only
# add_subdirectory() this folder on Windows and macOS).
#
#   Windows — slopsmith-vst-host.exe: sandboxed VST3 host + --scan-plugin
#   macOS   — slopsmith-vst-scan: VST3-only --scan-plugin probe (no sandbox yet)
#   Linux   — not used; plugin scan/load stay in slopsmith_audio.node for now
#
# Binaries are statically linked with JUCE so they can sit next to the addon.

if(NOT WIN32 AND NOT APPLE)
    return()
endif()

# Shared JUCE wiring for both helpers: include path, link libs, compile defs,
# and the per-config output dir so multi-config generators (VS / Xcode) don't
# append a second $<CONFIG> suffix.
function(slopsmith_configure_vst_helper target app_name)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../audio"
    )

    target_link_libraries(${target} PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
    )

    target_compile_definitions(${target} PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        # AU off: slopsmith_audio.node also omits JUCE_PLUGINHOST_AU (see
        # src/audio/CMakeLists.txt). On macOS, AU hosting from the Electron
        # native-addon thread crashes inside AppKit/JUCE MessageManager.
        JUCE_PLUGINHOST_AU=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_REPORT_APP_USAGE=0
        JUCE_APPLICATION_NAME_STRING="${app_name}"
        JUCE_APPLICATION_VERSION_STRING="0.1.0"
    )

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/Release"
        OUTPUT_NAME "${target}"
    )
endfunction()

if(WIN32)
    add_executable(slopsmith-vst-host WIN32
        main.cpp
        ../audio/VSTHost.cpp
        ../audio/Sandbox/Protocol.cpp
        ../audio/Sandbox/ControlChannel.cpp
        ../audio/Sandbox/AudioChannel.cpp
    )
    slopsmith_configure_vst_helper(slopsmith-vst-host SlopsmithVstHost)

    if(MSVC)
        # /EHa for SEH-aware catch in VSTHost.cpp (matches src/audio/CMakeLists.txt).
        target_compile_options(slopsmith-vst-host PRIVATE /EHa)
        # Emit a PDB for Release builds so sandbox-child crash dumps can be
        # symbolised — see the matching block in src/audio/CMakeLists.txt for the
        # /Zi + /DEBUG:FULL + /OPT rationale.
        target_compile_options(slopsmith-vst-host PRIVATE $<$<CONFIG:Release>:/Zi>)
        # One generator expression per flag — see src/audio/CMakeLists.txt.
        target_link_options(slopsmith-vst-host PRIVATE
            $<$<CONFIG:Release>:/DEBUG:FULL>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>)
        # `add_executable(... WIN32 ...)` already implies /SUBSYSTEM:WINDOWS and
        # the matching entry point (WinMainCRTStartup, or wWinMainCRTStartup if
        # the toolchain ever picks up _UNICODE). No need to pin them explicitly.
    endif()
endif()

if(APPLE)
    add_executable(slopsmith-vst-scan
        VSTScan.cpp
        ../audio/VSTHost.cpp
    )
    slopsmith_configure_vst_helper(slopsmith-vst-scan SlopsmithVstScan)

    target_link_libraries(slopsmith-vst-scan PRIVATE
        "-framework CoreAudio"
        "-framework CoreMIDI"
        "-framework AudioUnit"
        "-framework AudioToolbox"
        "-framework CoreAudioKit"
        "-framework CoreFoundation"
        "-framework Accelerate"
    )
endif()
