# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Ceciliae is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# synthesis module : the voice layers and engines behind core::IVoice.
#
# Contributes to the pure ceciliae_core static library. It implements the
# LAYERED voice pipeline (attack + modeled sustain + release) as concrete voices
# selected per rank by the VoiceFactory:
#   - SampleVoice       : multi-sample playback (safe fallback tier)
#   - AdditiveVoice     : wind-modulated PartialBank (modeled sustain / plenum)
#   - PhysicalPipeVoice : nonlinear excitation + resonator (solo / exposed tier)
#
# It includes no JUCE. Beyond ceciliae_core (and the C++ standard library) its
# only sibling dependency is the wind module, whose wind::WindResponseCurve the
# partial bank consumes for family-specific breathing. Where the design still
# calls for the dsp module (the 16-point sinc interpolator, the oversampled
# nonlinear island) this scaffold uses self-contained placeholders marked with
# TODO(phaseN) until that module is wired in.
# ---------------------------------------------------------------------------

target_sources(ceciliae_core PRIVATE
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
#   #include "ceciliae/synthesis/VoiceFactory.h"
# Listed here for IDE discoverability and install/export correctness.
set(CECILIAE_SYNTH_PUBLIC_HEADERS
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

target_sources(ceciliae_core PUBLIC
    FILE_SET ceciliae_synth_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CECILIAE_SYNTH_PUBLIC_HEADERS})
