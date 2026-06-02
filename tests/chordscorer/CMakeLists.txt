# Standalone benchmark for the harmonic-comb verifier (ChordScorer +
# OnsetDetector) — the path the live app uses for guitar and bass scoring.
# Not part of the addon build; build explicitly to measure / tune it. Unlike
# mlnd_bench this needs NO ONNX Runtime — it links only JUCE.
#
#   cmake -B build
#   cmake --build build
#   ./build/cs_bench <di-take.wav> <chart.txt> bass 4
#   ./build/cs_bench <di-take.wav> <chart.txt> guitar 6   # regression guard
#
# cs_bench replays a DI recording against a known chart so ChordScorer /
# OnsetDetector parameter changes can be measured (recall / timing).

cmake_minimum_required(VERSION 3.22)
project(cs_bench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_subdirectory("${REPO_ROOT}/JUCE" juce_build EXCLUDE_FROM_ALL)

add_executable(cs_bench
    bench.cpp
    "${REPO_ROOT}/src/audio/ChordScorer.cpp"
    "${REPO_ROOT}/src/audio/OnsetDetector.cpp")
target_include_directories(cs_bench PRIVATE "${REPO_ROOT}/src/audio")
target_compile_definitions(cs_bench PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)
target_link_libraries(cs_bench PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp)
