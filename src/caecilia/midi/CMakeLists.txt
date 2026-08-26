# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# midi module : MIDI interpretation and the console control surface.
#
# Contributes to the pure caecilia_core static library. It turns core-native
# MIDI (no JUCE MIDI types cross the seam) into typed engine intent: keyboard
# notes routed per division, the combination sequencer (si5/do6 = Prev/Next),
# generals + divisionals, program-change -> generals, CC mapping and MIDI-learn
# bindings that carry the shared selector-grammar identity.
#
# Links ONLY the C++ standard library; the no-JUCE lint (wired at the top level)
# guards against any juce:: header leaking in here.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    MidiMap.cpp
    MidiRouter.cpp
    Sequencer.cpp
    CombinationStore.cpp
    MidiLearn.cpp)

# Public headers carry the module's contract. Listed for IDE discoverability
# and consumed via the project-wide include root (<root>/src):
#   #include "caecilia/midi/MidiRouter.h"
set(CAECILIA_MIDI_PUBLIC_HEADERS
    MidiTypes.h
    MidiEvent.h
    VelocityCurve.h
    ChannelToDivisionMap.h
    ProgramChangeMap.h
    SequencerNavMap.h
    RegistrationCommandTemplate.h
    MidiLearnBinding.h
    MidiMap.h
    MidiRouteResult.h
    MidiRouter.h
    Sequencer.h
    CombinationStore.h
    MidiLearn.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_midi_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_MIDI_PUBLIC_HEADERS})
