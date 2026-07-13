# Copyright (c) 2026 Alesson Queiroz. All rights reserved.
# Caecilia is proprietary and confidential; unauthorized copying,
# distribution, or use of any part is prohibited. See LICENSE.

# ---------------------------------------------------------------------------
# dsp module : the independently-written DSP building blocks.
#
# Contributes to the pure caecilia_core static library (std-library only, no
# JUCE). Provides the dual reverb (Jot-style FDN + convolution), the fresh
# 16-point Kaiser-windowed-sinc interpolator and resamplers, the filter set
# (one-pole / biquad / formant), the power-of-two oversampler and band-limited
# residual helpers, per-pipe stereo spatialisation, and the SIMD kernel seam.
#
# Every kernel is prepare()-then-process(), noexcept on the hot path and
# FTZ/DAZ-safe. All algorithms are authored from public-domain mathematics
# (Jot 1991 FDN principles, standard Kaiser-window / windowed-sinc theory); no
# GPL DSP source is copied or referenced.
# ---------------------------------------------------------------------------

target_sources(caecilia_core PRIVATE
    Kernels.cpp
    OnePole.cpp
    Biquad.cpp
    FormantFilter.cpp
    SincKaiser16Interpolator.cpp
    PolyphaseResampler.cpp
    Resampler.cpp
    Oversampler.cpp
    Bandlimited.cpp
    FdnReverb.cpp
    ConvolutionReverb.cpp
    Limiter.cpp
    MasterEq.cpp
    Spatializer.cpp)

# Public headers consumed via the project-wide include root (<root>/src):
#   #include "caecilia/dsp/FdnReverb.h"
# Listed here for IDE discoverability and to carry the module's contract.
set(CAECILIA_DSP_PUBLIC_HEADERS
    DspMath.h
    Kernels.h
    OnePole.h
    Biquad.h
    FormantFilter.h
    SincKaiser16Interpolator.h
    PolyphaseResampler.h
    Resampler.h
    Oversampler.h
    Bandlimited.h
    FdnReverb.h
    ConvolutionReverb.h
    Limiter.h
    MasterEq.h
    Spatializer.h)

target_sources(caecilia_core PUBLIC
    FILE_SET caecilia_dsp_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/src"
    FILES ${CAECILIA_DSP_PUBLIC_HEADERS})
