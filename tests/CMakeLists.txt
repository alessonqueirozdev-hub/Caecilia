# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# tests : the headless Catch2 unit suite for the pure ceciliae_core library.
#
# Guarded by CECILIAE_BUILD_TESTS (the top-level CMakeLists only descends here
# when that option is ON and Catch2 has been fetched). The suite links ONLY
# `ceciliae::core` + Catch2 — no JUCE, no plugin host — so the DSP, tuning,
# registration, model and wind logic is exercised entirely off the audio thread.
#
# Everything under ceciliae_core is compiled into that one static library, so a
# single executable linking it can reach every module's implementation.
# ---------------------------------------------------------------------------

if(NOT CECILIAE_BUILD_TESTS)
    # Defensive: allow this directory to be added standalone without exploding
    # when the option (and therefore the Catch2 target) is absent.
    return()
endif()

if(NOT TARGET Catch2::Catch2WithMain)
    message(WARNING "Ceciliae tests: Catch2 target not found; skipping the test suite.")
    return()
endif()

add_executable(ceciliae_tests
    core/CoreTypesTest.cpp
    dsp/ReverbTest.cpp
    dsp/ResamplerTest.cpp
    tuning/TemperamentTest.cpp
    registration/RegistrationTest.cpp
    model/OrganLoaderTest.cpp
    wind/WindModelTest.cpp)

target_link_libraries(ceciliae_tests PRIVATE
    ceciliae::core
    Catch2::Catch2WithMain)

target_compile_features(ceciliae_tests PRIVATE cxx_std_20)

# Let the test sources reach the shared support helpers via a stable include
# root (e.g. #include "support/TestOrgan.h").
target_include_directories(ceciliae_tests PRIVATE
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
    catch_discover_tests(ceciliae_tests)
else()
    add_test(NAME ceciliae_tests COMMAND ceciliae_tests)
endif()
