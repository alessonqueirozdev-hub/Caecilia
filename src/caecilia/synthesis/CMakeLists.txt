# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

# ---------------------------------------------------------------------------
# synthesis module : the voice layers and engines behind core::IVoice.
#
# Contributes to the pure caecilia_core static library. It holds the voices the
# LAYERED pipeline (attack + modeled sustain + release) is to be built from; only
# the modeled sustain stage exists so far. VoiceFactory selects between them per
# rank, though nothing calls it yet -- the plugin builds AdditiveVoice directly:
#   - SampleVoice       : multi-sample playback (safe fallback tier)
#   - AdditiveVoice     : wind-modulated PartialBank (modeled sustain / plenum)
#   - PhysicalPipeVoice : nonlinear excitation + resonator (solo / exposed tier)
#
# It includes no JUCE. Beyond caecilia_core (and the C++ standard library) its
# sibling dependencies are the wind module, whose wind::WindResponseCurve the
# partial bank consumes for family-specific breathing, and the dsp module, whose
# DspMath gives the partial bank its equal-power panning. The dsp pieces the
# design still calls for here -- the 16-point sinc interpolator in SampleVoice,
# the oversampled nonlinear island around the excitations -- now exist in dsp but
# are not wired in; those call sites stay marked TODO(phaseN).
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    PartialBank.cpp
    NonlinearJet.cpp
    BeatingReed.cpp
    Waveguide.cpp
    ModalResonator.cpp
    PerPipeVoicer.cpp
    AdditiveVoice.cpp
    SampleVoice.cpp
    PhysicalPipeVoice.cpp
    VoiceFactory.cpp)

# Public headers are consumed via the project-wide include root (<root>/src):
#   #include "caecilia/synthesis/VoiceFactory.h"
# Listed here for IDE discoverability and install/export correctness.
set(CAECILIA_SYNTH_PUBLIC_HEADERS
    SpectralModel.h
    VoicingProfile.h
    AttackSpliceConfig.h
    ReleaseSpec.h
    VoiceContext.h
    ArEnvelope.h
    IVoiceLayer.h
    IExcitation.h
    IResonator.h
    PartialBank.h
    NonlinearJet.h
    BeatingReed.h
    Waveguide.h
    ModalResonator.h
    PerPipeVoicer.h
    AdditiveVoice.h
    SampleVoice.h
    PhysicalPipeVoice.h
    VoiceFactory.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_synth_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_SYNTH_PUBLIC_HEADERS})
