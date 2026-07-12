/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstddef>
#include <vector>

/**
 * @file SincKaiser16Interpolator.h
 * @brief A 16-tap Kaiser-windowed-sinc fractional-delay interpolator.
 *
 * Written fresh from standard public-domain Kaiser-window / windowed-sinc theory
 * (no GPL source referenced). The ideal fractional interpolator convolves the
 * signal with a shifted sinc; truncating it to 16 taps and multiplying by a
 * Kaiser window trades a small, controllable amount of pass-band ripple and
 * stop-band leakage for a finite, cheap kernel. A dense polyphase table of the
 * windowed kernel is precomputed once so run-time interpolation is a single
 * 16-tap dot product.
 */

namespace caecilia::dsp
{

/**
 * @brief Precomputed 16-tap Kaiser-windowed-sinc polyphase interpolator.
 *
 * Real-time contract: @ref build allocates the coefficient table and MUST run at
 * prepare()-time. @ref process is @c noexcept, allocation-free and does a fixed
 * 16-multiply-add per call, safe for the audio hot path.
 */
class SincKaiser16Interpolator
{
public:
    /// Number of filter taps (fixed by the type name / API contract).
    static constexpr std::size_t kTaps = 16;

    SincKaiser16Interpolator() = default;

    /**
     * @brief Build the polyphase coefficient table.
     * @param numPhases Fractional-position resolution (e.g. 512). Higher = less
     *                  quantisation of the sub-sample delay.
     * @param kaiserBeta Kaiser shape parameter (~8.6 gives ~ -80 dB sidelobes).
     *
     * Allocates and fills @c numPhases * kTaps coefficients. Not RT-safe.
     */
    void build(std::size_t numPhases = 512, double kaiserBeta = 8.6);

    /**
     * @brief Interpolate at a fractional position between the centre taps.
     * @param x16  Pointer to 16 contiguous input samples; the interpolation point
     *             lies at @p frac between x16[kTaps/2 - 1] and x16[kTaps/2].
     * @param frac Fractional offset in [0, 1).
     * @return The band-limited interpolated sample.
     *
     * Selects the nearest polyphase row for @p frac and returns the 16-tap dot
     * product with @p x16. RT-safe, @c noexcept.
     *
     * @note TODO(phase3): linearly blend adjacent phase rows for even smoother
     *       sub-phase accuracy when @c numPhases is small.
     */
    [[nodiscard]] float process(const float* x16, float frac) const noexcept;

    /// @return true once @ref build has produced a usable table.
    [[nodiscard]] bool isBuilt() const noexcept { return numPhases_ > 0; }

    /// @return The configured phase resolution.
    [[nodiscard]] std::size_t numPhases() const noexcept { return numPhases_; }

private:
    std::vector<float> table_;     ///< numPhases_ * kTaps coefficients, row-major.
    std::size_t        numPhases_ = 0;
};

} // namespace caecilia::dsp
