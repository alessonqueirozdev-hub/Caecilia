# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# registration module : the queryable relational registration brain.
#
# Contributes to the pure caecilia_core static library. It owns the STATE
# (RegistrationState + first-class couplers), the INTENT algebra (StopQuery /
# Selector / SelectorParser), the command-sourced branching undo/redo, the
# domain-intelligent plenum builder, provenance/explain, and the portable
# RegistrationIntent. Every apply() emits a fixed-capacity StateDelta — the only
# registration object that crosses the audio seam.
#
# It links ONLY the C++ standard library (consuming core + model value types) —
# no JUCE, guarded by the top-level no-JUCE lint.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    StopSet.cpp
    RegistrationState.cpp
    StopQuery.cpp
    Selector.cpp
    SelectorParser.cpp
    StateDelta.cpp
    PlenumBuilder.cpp
    RegistrationIntent.cpp
    Explanation.cpp
    RegistrationCommand.cpp
    RegistrationHistory.cpp
    RegistrationEngine.cpp
    FactoryGenerals.cpp)

# Public headers carry the module's contract. Listed for IDE discoverability and
# consumed via the project-wide include root (<root>/src):
#   #include "caecilia/registration/RegistrationEngine.h"
set(CAECILIA_REGISTRATION_PUBLIC_HEADERS
    StopSet.h
    RegistrationState.h
    StopQuery.h
    Selector.h
    SelectorParser.h
    StateDelta.h
    PlenumBuilder.h
    RegistrationIntent.h
    Explanation.h
    RegistrationCommand.h
    RegistrationHistory.h
    Piston.h
    RegistrationEngine.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_registration_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_REGISTRATION_PUBLIC_HEADERS})
