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
    tuning/TemperamentConstructionTest.cpp
    tuning/LiveTuningTest.cpp
    registration/RegistrationTest.cpp
    registration/HostRegistrationTest.cpp
    registration/FactoryGeneralsTest.cpp
    midi/LearnedControlsTest.cpp
    midi/MidiLearnTest.cpp
    midi/MidiRouterTest.cpp
    model/JsonTest.cpp
    model/ExampleOrganTest.cpp
    model/OrganCapacityTest.cpp
    model/OrganFileTest.cpp
    model/OrganLoaderTest.cpp
    model/RegistrationDynamicsTest.cpp
    model/PerRankDynamicsTest.cpp
    wind/BellowsPlateTest.cpp
    wind/WindModelTest.cpp
    wind/OrganWindTest.cpp)

target_link_libraries(caecilia_tests PRIVATE
    caecilia::core
    Catch2::Catch2WithMain)

target_compile_features(caecilia_tests PRIVATE cxx_std_20)

# Where the shipped example organ lives, so ExampleOrganTest can read the same
# file a user copies rather than a fixture that resembles it.
target_compile_definitions(caecilia_tests PRIVATE
    CAECILIA_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

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
# ---------------------------------------------------------------------------
# caecilia_plugin_tests : the same Catch2 suite, for the layer above the core.
#
# Built ONLY when the plugin was built -- a core-only configuration has no JUCE
# and must stay that way, which is why this is guarded on the target existing
# rather than on an option.
#
# It links `Caecilia`, whose shared-code library carries the processor, so a test
# constructs CaeciliaAudioProcessor directly: no host, no editor, no audio device.
# That reaches everything on the message thread -- state, loading an organ, the
# registration, MIDI learn -- which is where the plugin's own logic lives and
# where, until this existed, defects were found only by reading the code.
#
# A separate executable rather than more files in caecilia_tests, because that
# suite's contract is that it links the standard library and nothing else. Mixing
# JUCE into it would quietly end the guarantee that caecilia_core stands alone.
# ---------------------------------------------------------------------------
if(TARGET Caecilia)
    add_executable(caecilia_plugin_tests
        plugin/HostContractTest.cpp
        plugin/MidiLearnEndToEndTest.cpp
        plugin/ProcessorOrganStateTest.cpp
        plugin/RealTimeCostTest.cpp
        plugin/RegistrationUndoTest.cpp
        plugin/SelectorQueryTest.cpp
        plugin/OrganSwapUnderPlayTest.cpp
        plugin/SessionRoundTripTest.cpp)

    # `Caecilia` IS the static library that carries the processor's objects --
    # juce_add_plugin builds one shared-code library under that name and gives it
    # the file name Caecilia_SharedCode.lib, then hangs the VST3/AU/Standalone
    # wrappers off it. Linking it here reuses JUCE as already compiled rather than
    # compiling the whole of it a second time for this executable.
    target_link_libraries(caecilia_plugin_tests PRIVATE
        Caecilia
        caecilia::core
        Catch2::Catch2WithMain)

    # The plugin keeps its JUCE modules PRIVATE, which is right -- nothing above it
    # should reach them by accident, and a test is the one thing that must. It
    # takes the module flags from the targets themselves rather than restating a
    # path into the fetched sources, which would drift the day JUCE is found some
    # other way.
    target_include_directories(caecilia_plugin_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
        $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:juce::juce_gui_extra,INTERFACE_INCLUDE_DIRECTORIES>)

    target_compile_definitions(caecilia_plugin_tests PRIVATE
        $<TARGET_PROPERTY:Caecilia,COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:juce::juce_gui_extra,INTERFACE_COMPILE_DEFINITIONS>)

    target_compile_features(caecilia_plugin_tests PRIVATE cxx_std_20)
endif()

if(DEFINED catch2_SOURCE_DIR AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(caecilia_tests)
    if(TARGET caecilia_plugin_tests)
        catch_discover_tests(caecilia_plugin_tests)
    endif()
else()
    add_test(NAME caecilia_tests COMMAND caecilia_tests)
    if(TARGET caecilia_plugin_tests)
        add_test(NAME caecilia_plugin_tests COMMAND caecilia_plugin_tests)
    endif()
endif()
