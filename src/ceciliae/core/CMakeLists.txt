# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# core module : the shared vocabulary and RT contract.
#
# Contributes to the pure ceciliae_core static library. This module defines the
# semantic enums, the exact-rational Footage, the stable identity keys, the
# non-owning AudioBlock view and the top-level interfaces (IVoice, IWindSupply,
# ITuning, IReverb) that every other module compiles against.
#
# It links ONLY the C++ standard library. A no-JUCE lint (wired at the top
# level) guards against any juce:: header leaking in here.
# ---------------------------------------------------------------------------

target_sources(ceciliae_core PRIVATE
    Version.cpp)

# Public headers are consumed via the project-wide include root (<root>/src),
# already set on ceciliae_core at the top level:
#   #include "ceciliae/core/EngineTypes.h"
#
# Listed here for IDE discoverability; they carry the module's contract.
set(CECILIAE_CORE_PUBLIC_HEADERS
    AudioBlock.h
    EngineTypes.h
    IVoice.h
    IWindSupply.h
    ITuning.h
    IReverb.h
    Version.h)

target_sources(ceciliae_core PUBLIC
    FILE_SET ceciliae_core_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CECILIAE_CORE_PUBLIC_HEADERS})
