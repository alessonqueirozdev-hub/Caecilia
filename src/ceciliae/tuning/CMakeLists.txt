# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# tuning module : historical temperaments and micro-tuning.
#
# Contributes to the pure ceciliae_core static library. It turns a chosen
# temperament + reference pitch + optional stretch/detune into per-pipe sounding
# frequencies (implementing core::ITuning), computed off the audio thread and
# read RT-safely by the synthesis layer.
#
# Links ONLY the C++ standard library; the no-JUCE lint (wired at the top level)
# guards against any juce:: header leaking in here.
# ---------------------------------------------------------------------------

target_sources(ceciliae_core PRIVATE
    Temperament.cpp
    TemperamentLibrary.cpp
    TuningTable.cpp
    TuningModel.cpp)

# Public headers are consumed via the project-wide include root (<root>/src):
#   #include "ceciliae/tuning/TuningModel.h"
# Listed here for IDE discoverability; they carry the module's contract.
set(CECILIAE_TUNING_PUBLIC_HEADERS
    Temperament.h
    TemperamentLibrary.h
    TuningTable.h
    StretchTuning.h
    DetuneCurve.h
    TuningModel.h)

target_sources(ceciliae_core PUBLIC
    FILE_SET ceciliae_tuning_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CECILIAE_TUNING_PUBLIC_HEADERS})
