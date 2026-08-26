# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# core module : the shared vocabulary and RT contract.
#
# Contributes to the pure caecilia_core static library. This module defines the
# semantic enums, the exact-rational Footage, the stable identity keys, the
# non-owning AudioBlock view and the top-level interfaces (IVoice, IWindSupply,
# ITuning, IReverb) that every other module compiles against.
#
# It links ONLY the C++ standard library. A no-JUCE lint (wired at the top
# level) guards against any juce:: header leaking in here.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    Version.cpp)

# Public headers are consumed via the project-wide include root (<root>/src),
# already set on caecilia_core at the top level:
#   #include "caecilia/core/EngineTypes.h"
#
# Listed here for IDE discoverability; they carry the module's contract.
set(CAECILIA_CORE_PUBLIC_HEADERS
    AudioBlock.h
    EngineTypes.h
    IVoice.h
    IWindSupply.h
    ITuning.h
    IReverb.h
    Version.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_core_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_CORE_PUBLIC_HEADERS})
