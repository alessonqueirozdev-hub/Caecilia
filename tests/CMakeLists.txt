# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# tests : the headless Catch2 unit suite for the pure caecilia_core library.
#
# Guarded by CAECILIA_BUILD_TESTS (the top-level CMakeLists only descends here
# when that option is ON and Catch2 has been fetched). The suite links ONLY
# `caecilia::core` + Catch2 — no JUCE, no plugin host — so the DSP, tuning,
# registration, model and wind logic is exercised entirely off the audio thread.
#
# Everything under caecilia_core is compiled into that one static library, so a
# single executable linking it can reach every module's implementation.
# ---------------------------------------------------------------------------

if(NOT CAECILIA_BUILD_TESTS)
    # Defensive: allow this directory to be added standalone without exploding
    # when the option (and therefore the Catch2 target) is absent.
    return()
endif()

if(NOT TARGET Catch2::Catch2WithMain)
    message(WARNING "Caecilia tests: Catch2 target not found; skipping the test suite.")
    return()
endif()

add_executable(caecilia_tests
    core/CoreTypesTest.cpp
    core/ParameterIdHashTest.cpp
    core/ReverbSendGateTest.cpp
    core/TripleBufferTest.cpp
    engine/AudioEngineTest.cpp
    engine/CouplerTest.cpp
    engine/CpuGovernorTest.cpp
    engine/SwellBoxTest.cpp
    engine/VoiceSheddingTest.cpp
    engine/AuFormatContractTest.cpp
    engine/ExpressionTest.cpp
    engine/RegistrationUnderHeldNotesTest.cpp
    dsp/ReverbTest.cpp
    dsp/PartialOscillatorTest.cpp
    dsp/LimiterTest.cpp
    dsp/MasterEqTest.cpp
    dsp/OnePoleTest.cpp
    dsp/DspMathTest.cpp
    synthesis/PartialBankTest.cpp
    synthesis/MixtureBreakTest.cpp
    synthesis/WindBrightnessTest.cpp
    synthesis/RankVoiceTest.cpp
    dsp/ResamplerTest.cpp
    tuning/TemperamentTest.cpp
    tuning/LiveTuningTest.cpp
    registration/RegistrationTest.cpp
    registration/HostRegistrationTest.cpp
    registration/FactoryGeneralsTest.cpp
    model/OrganLoaderTest.cpp
    model/RegistrationDynamicsTest.cpp
    model/PerRankDynamicsTest.cpp
    wind/WindModelTest.cpp
    wind/OrganWindTest.cpp)

target_link_libraries(caecilia_tests PRIVATE
    caecilia::core
    Catch2::Catch2WithMain)

target_compile_features(caecilia_tests PRIVATE cxx_std_20)

# Let the test sources reach the shared support helpers via a stable include
# root (e.g. #include "support/TestOrgan.h").
target_include_directories(caecilia_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# CTest registration.
#
# Prefer Catch2's per-test discovery (one CTest entry per TEST_CASE) when its
# helper module is reachable — FetchContent exposes it under the fetched
# sources' `extras` directory. Fall back to a single aggregate test otherwise,
# so `ctest` still works on any CMake/Catch2 layout.
# ---------------------------------------------------------------------------
if(DEFINED catch2_SOURCE_DIR AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(caecilia_tests)
else()
    add_test(NAME caecilia_tests COMMAND caecilia_tests)
endif()
