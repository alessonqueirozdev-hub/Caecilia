# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Caecilia is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# wind module : the shared wind supply.
#
# Contributes to the pure caecilia_core static library. It implements
# core::IWindSupply as an audio-rate control system: a reservoir/bellows
# pressure ODE, windchest distribution, demand-driven sag under polyphony, and
# a tremulant modelled as wind-pressure modulation. It links ONLY the C++
# standard library — no JUCE, guarded by the top-level no-JUCE lint.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    Bellows.cpp
    Windchest.cpp
    Tremulant.cpp
    WindResponseCurve.cpp
    WindModel.cpp)

# Public headers carry the module's contract. Listed for IDE discoverability
# and consumed via the project-wide include root (<root>/src):
#   #include "caecilia/wind/WindModel.h"
set(CAECILIA_WIND_PUBLIC_HEADERS
    WindTypes.h
    Bellows.h
    Windchest.h
    Tremulant.h
    WindResponseCurve.h
    WindTap.h
    WindModel.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_wind_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_WIND_PUBLIC_HEADERS})
