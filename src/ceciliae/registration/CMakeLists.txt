# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# registration module : the queryable relational registration brain.
#
# Contributes to the pure ceciliae_core static library. It owns the STATE
# (RegistrationState + first-class couplers), the INTENT algebra (StopQuery /
# Selector / SelectorParser), the command-sourced branching undo/redo, the
# domain-intelligent plenum builder, provenance/explain, and the portable
# RegistrationIntent. Every apply() emits a fixed-capacity StateDelta — the only
# registration object that crosses the audio seam.
#
# It links ONLY the C++ standard library (consuming core + model value types) —
# no JUCE, guarded by the top-level no-JUCE lint.
# ---------------------------------------------------------------------------

target_sources(ceciliae_core PRIVATE
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
    RegistrationEngine.cpp)

# Public headers carry the module's contract. Listed for IDE discoverability and
# consumed via the project-wide include root (<root>/src):
#   #include "ceciliae/registration/RegistrationEngine.h"
set(CECILIAE_REGISTRATION_PUBLIC_HEADERS
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

target_sources(ceciliae_core PUBLIC
    FILE_SET ceciliae_registration_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CECILIAE_REGISTRATION_PUBLIC_HEADERS})
