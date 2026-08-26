# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

cmake_minimum_required(VERSION 3.22)

project(Caecilia
    VERSION 0.0.1
    DESCRIPTION "Caecilia - open-source hybrid pipe-organ virtual instrument"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(CAECILIA_BUILD_PLUGIN "Build the JUCE VST3/AU/Standalone plugin target" ON)
option(CAECILIA_BUILD_TESTS  "Build the Catch2 headless unit tests"            ON)
option(CAECILIA_BUILD_TOOLS  "Build the offline, JUCE-free analysis CLI tools"  ON)
option(CAECILIA_CORE_ONLY    "Build only the pure caecilia_core static library (no JUCE)" OFF)

# CORE_ONLY is an authoritative kill-switch for anything JUCE-flavoured.
if(CAECILIA_CORE_ONLY)
    set(CAECILIA_BUILD_PLUGIN OFF CACHE BOOL "Forced OFF by CAECILIA_CORE_ONLY" FORCE)
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
# caecilia_core : the pure, JUCE-free static library.
#
# It links ONLY the C++ standard library and owns ALL DSP / engine / synth /
# wind / model / tuning / midi / registration / control logic. Every other
# module depends on core; core depends on nothing but the standard library.
# ---------------------------------------------------------------------------
add_library(caecilia_core STATIC)
add_library(caecilia::core ALIAS caecilia_core)

target_include_directories(caecilia_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(caecilia_core PUBLIC cxx_std_20)

# Core must never see exceptions/RTTI on the hot path; keep the contract tight.
set_target_properties(caecilia_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Third-party dependencies (fetched ONLY when actually required).
# ---------------------------------------------------------------------------
include(FetchContent)

# JUCE : only when we are building the plugin AND not in core-only mode.
if(NOT CAECILIA_CORE_ONLY AND CAECILIA_BUILD_PLUGIN)
    message(STATUS "Caecilia: fetching JUCE (plugin build enabled)")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4)
    FetchContent_MakeAvailable(juce)
endif()

# Catch2 : only when tests are enabled.
if(CAECILIA_BUILD_TESTS)
    message(STATUS "Caecilia: fetching Catch2 (tests enabled)")
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Pure-core module directories: each contributes sources to caecilia_core.
# We add a subdirectory only when it actually exists on disk, so the tree can
# be materialised incrementally as the roadmap progresses.
# ---------------------------------------------------------------------------
set(CAECILIA_CORE_MODULES
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

foreach(_mod IN LISTS CAECILIA_CORE_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/caecilia/${_mod}/CMakeLists.txt")
        add_subdirectory("src/caecilia/${_mod}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# JUCE-consuming modules: contribute to the `Caecilia` plugin target.
# ---------------------------------------------------------------------------
if(NOT CAECILIA_CORE_ONLY AND CAECILIA_BUILD_PLUGIN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/caecilia/plugin/CMakeLists.txt")
        add_subdirectory("src/caecilia/plugin")
    endif()
    # NOTE: src/caecilia/ui is header-only now. The JUCE console that used to live
    # there (ConsoleView, PhotorealConsole, FlatConsole and their widgets) was
    # compiled into every build and never instantiated -- the editor is a
    # WebBrowserComponent serving docs/mockups/console.html. Only StateMirror.h
    # survived, and a header needs no target.
endif()

# ---------------------------------------------------------------------------
# Offline analysis tools: host-side executables that link caecilia_core but,
# like core, NEVER include JUCE. Available even in CORE_ONLY builds.
# ---------------------------------------------------------------------------
if(CAECILIA_BUILD_TOOLS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
        add_subdirectory(tools)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(CAECILIA_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

message(STATUS "Caecilia configuration:")
message(STATUS "  CAECILIA_BUILD_PLUGIN = ${CAECILIA_BUILD_PLUGIN}")
message(STATUS "  CAECILIA_BUILD_TESTS  = ${CAECILIA_BUILD_TESTS}")
message(STATUS "  CAECILIA_BUILD_TOOLS  = ${CAECILIA_BUILD_TOOLS}")
message(STATUS "  CAECILIA_CORE_ONLY    = ${CAECILIA_CORE_ONLY}")
