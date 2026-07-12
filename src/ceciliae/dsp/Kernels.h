/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstddef>

/**
 * @file Kernels.h
 * @brief The SIMD-friendly inner-loop seam for the DSP module.
 *
 * Each kernel is declared here once and has a portable SCALAR REFERENCE
 * implementation in Kernels.cpp. Architecture backends (SSE/AVX2 on x86-64,
 * NEON on Apple Silicon) are selected by compile-time/runtime dispatch and are
 * bit/ULP-compared against this scalar reference in CI. Callers only ever see
 * these free functions, never the backend, so higher-level DSP code stays
 * architecture-agnostic.
 *
 * Real-time contract: every kernel is @c noexcept and allocation-free; all
 * buffers are caller-owned and pre-sized. These run on the audio hot path.
 */

namespace ceciliae::dsp::kernels
{

/**
 * @brief Accumulate a gained copy of @p src into @p dst: dst[i] += src[i] * gain.
 * @param dst  Destination buffer of @p count floats (read-modify-write).
 * @param src  Source buffer of @p count floats.
 * @param count Number of samples.
 * @param gain Scalar gain applied to @p src.
 *
 * The core mixing primitive: voices accumulate into per-windchest buses this way.
 * RT-safe, @c noexcept.
 */
void accumulate(float* dst, const float* src, std::size_t count, float gain) noexcept;

/**
 * @brief Inner product of two equal-length buffers, sum(a[i] * b[i]).
 *
 * Backs the windowed-sinc interpolation tap sum. RT-safe, @c noexcept.
 */
[[nodiscard]] float dotProduct(const float* a, const float* b, std::size_t count) noexcept;

/**
 * @brief Process @p count samples through a biquad in Transposed Direct Form II.
 * @param data  In-place sample buffer of length @p count.
 * @param count Number of samples.
 * @param b0,b1,b2,a1,a2 Normalised biquad coefficients (a0 == 1).
 * @param z1,z2 The two state variables, updated in place across calls.
 *
 * Kept as a raw-coefficient kernel (no coupling to the Biquad class) so it can be
 * vectorised across channels. RT-safe, @c noexcept.
 */
void biquadBlock(float*      data,
                 std::size_t count,
                 float       b0,
                 float       b1,
                 float       b2,
                 float       a1,
                 float       a2,
                 float&      z1,
                 float&      z2) noexcept;

/**
 * @brief In-place orthonormal 16-point Walsh-Hadamard transform.
 * @param v The 16-element vector, overwritten with H * v.
 *
 * This is the lossless, maximally-diffusing feedback mixing matrix of the FDN
 * reverb (a normalised Hadamard is orthogonal, so it preserves energy while
 * scattering every line into every other). Scaled by 1/sqrt(16) = 0.25 so the
 * transform is unitary. Implemented as an in-place radix-2 butterfly cascade.
 * RT-safe, @c noexcept.
 */
void hadamard16(float* v) noexcept;

} // namespace ceciliae::dsp::kernels
