// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstddef>

/**
 * @file Kernels.h
 * @brief The SIMD-friendly inner-loop seam for the DSP module.
 *
 * Each kernel is declared here once and has a portable SCALAR REFERENCE
 * implementation, and callers only ever see these free functions, never a backend,
 * so higher-level DSP code stays architecture-agnostic.
 *
 * @ref partialAccumulate — the instrument's hottest loop — has SSE2 and AVX2
 * backends behind a runtime probe (KernelsPartial.cpp and KernelsAvx2.cpp). The
 * rest still run the reference; they are not on the critical path.
 *
 * The backends are NOT bit-identical to the reference and cannot be: they advance
 * each phasor in strides, so the rounding trajectories separate. The realised
 * frequency is identical to a thousandth of a cent and the level difference sits
 * around −67 dB after four seconds. The CI gate is therefore a fingerprint diff
 * rather than sample equality — see the `backends` job.
 *
 * @todo NEON, for Apple Silicon, which today runs the reference.
 *
 * Real-time contract: every kernel is @c noexcept and allocation-free; all
 * buffers are caller-owned and pre-sized. These run on the audio hot path.
 */

namespace caecilia::dsp::kernels
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

// ---------------------------------------------------------------------------
// The partial oscillator: the hottest loop in the instrument.
// ---------------------------------------------------------------------------

/// One partial's recursive quadrature oscillator, as the caller keeps it.
///
/// The pair IS cos and sin of the phase, and sin is the output. Advancing is a
/// complex rotation, so no transcendental appears on the sample path at all --
/// two per partial per BLOCK instead of one per partial per SAMPLE.
struct PhasorState
{
    float x = 1.0f; ///< cos(phase)
    float y = 0.0f; ///< sin(phase) -- this is the sample
};

/**
 * @brief Accumulate one partial's whole block into stereo buffers.
 *
 * @param dstL  Left accumulator, @p count floats, read-modify-write.
 * @param dstR  Right accumulator, @p count floats, read-modify-write.
 * @param count Samples to generate.
 * @param osc   The partial's phasor. Advanced by @p count samples and written back.
 * @param cosInc,sinInc The per-sample rotation.
 * @param gain  The partial's gain. Ramped and written back.
 * @param gainInc Per-sample gain step, so a block boundary never steps the level.
 * @param panL,panR Equal-power placement.
 *
 * Semantics, exactly: sample @e n uses gain `gain + (n+1)*gainInc` and the phasor's
 * imaginary part BEFORE its @e n-th advance. Any backend must reproduce that, and
 * @c PartialOscillatorTest holds them to it.
 *
 * RT-safe, @c noexcept, allocation-free.
 */
void partialAccumulate(float* dstL, float* dstR, std::size_t count,
                       PhasorState& osc, float cosInc, float sinInc,
                       float& gain, float gainInc,
                       float panL, float panR) noexcept;

/// Which implementation @ref partialAccumulate runs.
///
/// Named by instruction set rather than by lane count, because that is what a
/// `--backend` flag is for: saying which code path was measured. Sse2 and Neon are
/// the same four-wide algorithm on two architectures, and asking for either on the
/// other gives whichever one exists — see @ref selectBackend.
enum class Backend
{
    Scalar, ///< The portable reference. Always available; always correct.
    Sse2,   ///< 4 frames at a time. Baseline on every x86-64, so no CPU probe.
    Neon,   ///< 4 frames at a time. Baseline on every aarch64, likewise.
    Avx2    ///< 8 frames at a time, where the CPU has it. Probed, never assumed.
};

/// The backend in use. Selected once at startup, never on the audio path.
[[nodiscard]] Backend activeBackend() noexcept;

/**
 * @brief Force a backend, or fall back to the best one this CPU supports.
 * @param backend What to use.
 * @return What was actually selected -- asking for a backend the build or the CPU
 *         does not have gives the best available instead, never a crash.
 *
 * Off the audio thread. It exists for the equivalence tests and for the bench's
 * interleaved A/B: measuring two backends in separate processes on a contended
 * machine compares the machine's mood, not the code.
 */
Backend selectBackend(Backend backend) noexcept;

/// @return the best backend this CPU and this build support.
[[nodiscard]] Backend bestAvailableBackend() noexcept;

} // namespace caecilia::dsp::kernels
