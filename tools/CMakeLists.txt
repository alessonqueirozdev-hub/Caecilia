# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# tools : offline, host-side, JUCE-free command-line utilities.
#
# These executables CONSUME the pure caecilia_core static library (for the
# shared vocabulary: PipeId, Footage, versionString, ...) but are NOT part of
# it. They form the off-line analysis toolchain that produces the
# SpectralModel / sample-set metadata; nothing here ever runs on the audio
# thread, so std::vector, <fstream> and iostream are all fair game.
#
# Like the pure core, tools MUST NOT include JUCE. Only the plugin and ui
# modules may. The top level guards this directory behind CAECILIA_BUILD_TOOLS
# and only adds it when it exists.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Shared helper library: CLI parsing and WAV I/O, compiled once and linked into
# every tool. Its include base is the tools root, so tool sources include their
# siblings as e.g. #include "common/WavFile.h" while still pulling core headers
# through caecilia::core as #include "caecilia/core/EngineTypes.h".
# ---------------------------------------------------------------------------
add_library(caecilia_tools_common STATIC
    common/CliArgs.cpp
    common/WavFile.cpp)

target_include_directories(caecilia_tools_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(caecilia_tools_common PUBLIC caecilia::core)
target_compile_features(caecilia_tools_common PUBLIC cxx_std_20)

set_target_properties(caecilia_tools_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Helper: declare one tool executable that links the shared helper + core and
# lands its binary under <build>/tools for easy discovery.
# ---------------------------------------------------------------------------
function(caecilia_add_tool _target)
    add_executable(${_target} ${ARGN})
    target_link_libraries(${_target} PRIVATE caecilia_tools_common caecilia::core)
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
endfunction()

# 1) Sample-set analyzer: extract loop points + pitch from a pipe recording.
caecilia_add_tool(caecilia-sampleset-analyzer
    sampleset-analyzer/SampleSetAnalyzer.cpp
    sampleset-analyzer/Main.cpp)

# 2) Additive-partial extractor: FFT a pipe sample into a PartialBank.
caecilia_add_tool(caecilia-partial-extractor
    partial-extractor/PartialExtractor.cpp
    partial-extractor/Main.cpp)

# 3) Sonic fingerprint: a stable numeric signature of how the organ sounds, so a
#    large refactor can be shown not to have changed the sound.
caecilia_add_tool(caecilia-fingerprint
    fingerprint/Main.cpp)

# 3) Render benchmark: what fraction of a core does a Tutti actually cost?
caecilia_add_tool(caecilia-bench
    bench/Main.cpp)

# 3) Impulse-response utility for the convolution reverb.
caecilia_add_tool(caecilia-ir-tool
    ir-tool/ImpulseResponseTool.cpp
    ir-tool/Main.cpp)

# 4) Registration render/verification harness: proves the shared composite
#    voicing actually sounds (RMS + Goertzel) and guards the 64-partial-cap fix.
caecilia_add_tool(caecilia-verify-registration
    verify-registration/Main.cpp)

message(STATUS "Caecilia: offline tools enabled "
               "(caecilia-sampleset-analyzer, caecilia-partial-extractor, "
               "caecilia-ir-tool, caecilia-verify-registration)")
