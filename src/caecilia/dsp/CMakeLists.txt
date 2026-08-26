# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
    KernelsPartial.cpp
    KernelsAvx2.cpp
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

# ---------------------------------------------------------------------------
# KernelsAvx2.cpp is the ONLY unit compiled with AVX2, and the flag applies to the
# whole unit rather than to the function that wants it. Anything else that landed
# here -- an inline function from a header, a container's growth path -- would be
# compiled with AVX2 too, and COMDAT folding is free to splice that copy into a
# caller that never ran the CPU probe. The crash is an illegal instruction on a
# pre-Haswell machine, in unrelated-looking code, and it does not reproduce on the
# developer's machine.
#
# The file's own defence is that it includes only <immintrin.h> and Kernels.h and
# defines exactly one function. This is the other half: nothing else gets the flag.
#
# Verify:  dumpbin /disasm caecilia_core.lib | findstr ymm
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i.86)" OR
   CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "(arm|aarch64)")
    if(MSVC)
        set_source_files_properties(KernelsAvx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(KernelsAvx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
    endif()
endif()
