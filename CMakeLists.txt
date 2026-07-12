# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

cmake_minimum_required(VERSION 3.22)

project(Ceciliae
    VERSION 0.0.1
    DESCRIPTION "Ceciliae - proprietary hybrid pipe-organ virtual instrument"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(CECILIAE_BUILD_PLUGIN "Build the JUCE VST3/AU/Standalone plugin target" ON)
option(CECILIAE_BUILD_TESTS  "Build the Catch2 headless unit tests"            ON)
option(CECILIAE_BUILD_TOOLS  "Build the offline, JUCE-free analysis CLI tools"  ON)
option(CECILIAE_CORE_ONLY    "Build only the pure ceciliae_core static library (no JUCE)" OFF)

# CORE_ONLY is an authoritative kill-switch for anything JUCE-flavoured.
if(CECILIAE_CORE_ONLY)
    set(CECILIAE_BUILD_PLUGIN OFF CACHE BOOL "Forced OFF by CECILIAE_CORE_ONLY" FORCE)
endif()

# ---------------------------------------------------------------------------
# Global C++ settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# ceciliae_core : the pure, JUCE-free static library.
#
# It links ONLY the C++ standard library and owns ALL DSP / engine / synth /
# wind / model / tuning / midi / registration / control logic. Every other
# module depends on core; core depends on nothing but the standard library.
# ---------------------------------------------------------------------------
add_library(ceciliae_core STATIC)
add_library(ceciliae::core ALIAS ceciliae_core)

target_include_directories(ceciliae_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(ceciliae_core PUBLIC cxx_std_20)

# Core must never see exceptions/RTTI on the hot path; keep the contract tight.
set_target_properties(ceciliae_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched ONLY when actually required).
# ---------------------------------------------------------------------------
include(FetchContent)

# JUCE : only when we are building the plugin AND not in core-only mode.
if(NOT CECILIAE_CORE_ONLY AND CECILIAE_BUILD_PLUGIN)
    message(STATUS "Ceciliae: fetching JUCE (plugin build enabled)")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4)
    FetchContent_MakeAvailable(juce)
endif()

# Catch2 : only when tests are enabled.
if(CECILIAE_BUILD_TESTS)
    message(STATUS "Ceciliae: fetching Catch2 (tests enabled)")
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Pure-core module directories: each contributes sources to ceciliae_core.
# We add a subdirectory only when it actually exists on disk, so the tree can
# be materialised incrementally as the roadmap progresses.
# ---------------------------------------------------------------------------
set(CECILIAE_CORE_MODULES
    core
    engine
    synthesis
    wind
    model
    dsp
    tuning
    midi
    registration
    control)

foreach(_mod IN LISTS CECILIAE_CORE_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ceciliae/${_mod}/CMakeLists.txt")
        add_subdirectory("src/ceciliae/${_mod}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# JUCE-consuming modules: contribute to the `Ceciliae` plugin target.
# ---------------------------------------------------------------------------
if(NOT CECILIAE_CORE_ONLY AND CECILIAE_BUILD_PLUGIN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ceciliae/plugin/CMakeLists.txt")
        add_subdirectory("src/ceciliae/plugin")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ceciliae/ui/CMakeLists.txt")
        add_subdirectory("src/ceciliae/ui")
    endif()
endif()

# ---------------------------------------------------------------------------
# Offline analysis tools: host-side executables that link ceciliae_core but,
# like core, NEVER include JUCE. Available even in CORE_ONLY builds.
# ---------------------------------------------------------------------------
if(CECILIAE_BUILD_TOOLS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
        add_subdirectory(tools)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(CECILIAE_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

message(STATUS "Ceciliae configuration:")
message(STATUS "  CECILIAE_BUILD_PLUGIN = ${CECILIAE_BUILD_PLUGIN}")
message(STATUS "  CECILIAE_BUILD_TESTS  = ${CECILIAE_BUILD_TESTS}")
message(STATUS "  CECILIAE_BUILD_TOOLS  = ${CECILIAE_BUILD_TOOLS}")
message(STATUS "  CECILIAE_CORE_ONLY    = ${CECILIAE_CORE_ONLY}")
