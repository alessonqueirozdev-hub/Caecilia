# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Caecilia is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# model module : the instrument definition and per-pipe physical/spatial data.
#
# Contributes to the pure caecilia_core static library. It parses a
# human-readable organ document into a mutable OrganDefinition and compiles that
# into the immutable OrganSpec (Organ) the audio engine, the registration brain
# and the data-driven UI all read.
#
# Depends ONLY on `core` and the C++ standard library. It performs NO OS or
# filesystem I/O: OrganLoader works on in-memory document text handed in by the
# JUCE shell, keeping the no-JUCE / no-filesystem contract on caecilia_core.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    Rank.cpp
    Stop.cpp
    Division.cpp
    Organ.cpp
    OrganDefinition.cpp
    OrganLoader.cpp)

# Public headers consumed via the project-wide include root (<root>/src):
#   #include "caecilia/model/Organ.h"
# Listed for IDE discoverability and to carry the module's contract.
set(CAECILIA_MODEL_PUBLIC_HEADERS
    PipeSpatial.h
    RankVoicingSpec.h
    SampleSetDescriptor.h
    Pipe.h
    Windchest.h
    Rank.h
    Stop.h
    Coupler.h
    Division.h
    Organ.h
    Diagnostics.h
    OrganDefinition.h
    OrganLoader.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_model_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_MODEL_PUBLIC_HEADERS})
