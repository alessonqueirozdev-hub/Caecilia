# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Caecilia is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# engine module : the real-time voice-pool + scheduler layer of the pure core.
#
# Contributes to the pure caecilia_core static library (namespace
# caecilia::core::engine). It owns the lock-free VoicePool, the VoiceScheduler,
# the SPSC command ring and the AudioEngine seam that the JUCE plugin drives.
#
# It depends ONLY on caecilia/core headers (AudioBlock, EngineTypes, IVoice,
# IWindSupply, ITuning, IReverb) and the C++ standard library — never on JUCE
# nor on any other caecilia module. Concrete wind / dsp / synthesis / model
# implementations are reached exclusively through the core interfaces.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    VoiceScheduler.cpp
    AudioEngine.cpp)

# Public headers carrying this module's contract. Consumed via the project-wide
# include root (<root>/src), e.g. #include "caecilia/engine/AudioEngine.h".
# Listed here for IDE discoverability and the header file-set.
set(CAECILIA_ENGINE_PUBLIC_HEADERS
    VoiceHandle.h
    SpscRing.h
    DeadlineBudget.h
    StealPolicy.h
    EngineCommand.h
    MeterSnapshot.h
    VoiceBatchView.h
    RenderContext.h
    VoicePool.h
    VoiceScheduler.h
    AudioEngine.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_engine_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_ENGINE_PUBLIC_HEADERS})
