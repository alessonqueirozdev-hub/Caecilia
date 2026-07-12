# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# tools : offline, host-side, JUCE-free command-line utilities.
#
# These executables CONSUME the pure ceciliae_core static library (for the
# shared vocabulary: PipeId, Footage, versionString, ...) but are NOT part of
# it. They form the off-line analysis toolchain that produces the proprietary
# SpectralModel / sample-set metadata; nothing here ever runs on the audio
# thread, so std::vector, <fstream> and iostream are all fair game.
#
# Like the pure core, tools MUST NOT include JUCE. Only the plugin and ui
# modules may. The top level guards this directory behind CECILIAE_BUILD_TOOLS
# and only adds it when it exists.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Shared helper library: CLI parsing and WAV I/O, compiled once and linked into
# every tool. Its include base is the tools root, so tool sources include their
# siblings as e.g. #include "common/WavFile.h" while still pulling core headers
# through ceciliae::core as #include "ceciliae/core/EngineTypes.h".
# ---------------------------------------------------------------------------
add_library(ceciliae_tools_common STATIC
    common/CliArgs.cpp
    common/WavFile.cpp)

target_include_directories(ceciliae_tools_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ceciliae_tools_common PUBLIC ceciliae::core)
target_compile_features(ceciliae_tools_common PUBLIC cxx_std_20)

set_target_properties(ceciliae_tools_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Helper: declare one tool executable that links the shared helper + core and
# lands its binary under <build>/tools for easy discovery.
# ---------------------------------------------------------------------------
function(ceciliae_add_tool _target)
    add_executable(${_target} ${ARGN})
    target_link_libraries(${_target} PRIVATE ceciliae_tools_common ceciliae::core)
    target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
endfunction()

# 1) Sample-set analyzer: extract loop points + pitch from a pipe recording.
ceciliae_add_tool(ceciliae-sampleset-analyzer
    sampleset-analyzer/SampleSetAnalyzer.cpp
    sampleset-analyzer/Main.cpp)

# 2) Additive-partial extractor: FFT a pipe sample into a PartialBank.
ceciliae_add_tool(ceciliae-partial-extractor
    partial-extractor/PartialExtractor.cpp
    partial-extractor/Main.cpp)

# 3) Impulse-response utility for the convolution reverb.
ceciliae_add_tool(ceciliae-ir-tool
    ir-tool/ImpulseResponseTool.cpp
    ir-tool/Main.cpp)

message(STATUS "Ceciliae: offline tools enabled "
               "(ceciliae-sampleset-analyzer, ceciliae-partial-extractor, ceciliae-ir-tool)")
