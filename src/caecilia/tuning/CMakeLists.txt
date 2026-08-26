# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# tuning module : historical temperaments and micro-tuning.
#
# Contributes to the pure caecilia_core static library. It turns a chosen
# temperament + reference pitch + optional stretch/detune into per-pipe sounding
# frequencies (implementing core::ITuning), computed off the audio thread and
# read RT-safely by the synthesis layer.
#
# Links ONLY the C++ standard library; the no-JUCE lint (wired at the top level)
# guards against any juce:: header leaking in here.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    Temperament.cpp
    TemperamentLibrary.cpp
    TuningTable.cpp
    TuningModel.cpp)

# Public headers are consumed via the project-wide include root (<root>/src):
#   #include "caecilia/tuning/TuningModel.h"
# Listed here for IDE discoverability; they carry the module's contract.
set(CAECILIA_TUNING_PUBLIC_HEADERS
    Temperament.h
    TemperamentLibrary.h
    TuningTable.h
    StretchTuning.h
    DetuneCurve.h
    TuningModel.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_tuning_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_TUNING_PUBLIC_HEADERS})
