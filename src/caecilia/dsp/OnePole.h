// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

/**
 * @file OnePole.h
 * @brief A first-order (one-pole / one-zero) IIR filter.
 *
 * Cheap building block used for damping (FDN feedback lines), DC blocking, and
 * gentle tone shaping. Coefficients are computed from a cutoff at configure-time;
 * the per-sample @ref OnePole::process is a two-multiply-add hot-path kernel.
 *
 * The coefficients live in their own struct (@ref OnePoleCoeffs) so a BANK of
 * sections sharing one corner frequency computes the pole once rather than once
 * per section — see @ref OnePole::setCoeffs.
 */

namespace caecilia::dsp
{

/**
 * @brief The three coefficients of a first-order section, separated from the
 *        state that runs them.
 *
 * This split exists for a measured reason. The FDN's sixteen damping filters all
 * sit at the same corner, so configuring them one at a time meant sixteen @c exp
 * calls to arrive at one answer — and that happened on the audio thread, on every
 * reverb parameter change, in an automation sweep on every block. Compute the
 * response once and hand the same struct to every section.
 */
struct OnePoleCoeffs
{
    float b0 = 1.0f; ///< Input gain.
    float b1 = 0.0f; ///< Delayed-input gain (zero for the low-pass).
    float a1 = 0.0f; ///< Delayed-output gain, negated (pole at -a1).

    /**
     * @brief Coefficients for a one-pole low-pass at @p cutoffHz.
     * Calls @c exp exactly once. RT-safe, no allocation.
     */
    [[nodiscard]] static OnePoleCoeffs lowpass(core::SampleRate sampleRate,
                                               float            cutoffHz) noexcept;

    /**
     * @brief Coefficients for the complementary one-pole/one-zero high-pass.
     * Calls @c exp exactly once. RT-safe, no allocation.
     */
    [[nodiscard]] static OnePoleCoeffs highpass(core::SampleRate sampleRate,
                                                float            cutoffHz) noexcept;
};

/**
 * @brief First-order low-pass / high-pass filter with a single state pair.
 *
 * The difference equation is @c y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1], which
 * realises either a one-pole low-pass or a one-pole high-pass depending on how
 * the coefficients are set. One instance realises one response; pick the setter
 * that matches the role.
 *
 * Real-time contract: @ref setLowpass / @ref setHighpass may be called from the
 * audio thread (no allocation), but each one evaluates @c exp — prefer
 * @ref setCoeffs when several sections share a corner. @ref process and
 * @ref reset are @c noexcept hot-path safe.
 */
class OnePole
{
public:
    OnePole() noexcept = default;

    /// Store the sample rate used by the setters. Call before configuring.
    void prepare(core::SampleRate sampleRate) noexcept { sampleRate_ = sampleRate; }

    /**
     * @brief Configure a one-pole low-pass with corner frequency @p cutoffHz.
     * RT-safe (no allocation); computes @c exp once.
     */
    void setLowpass(float cutoffHz) noexcept;

    /**
     * @brief Configure a one-pole high-pass with corner frequency @p cutoffHz.
     * RT-safe (no allocation).
     */
    void setHighpass(float cutoffHz) noexcept;

    /**
     * @brief Adopt a precomputed response, leaving the filter memory untouched.
     *
     * Three stores and no transcendental. This is the setter to use when a bank
     * of sections shares one corner frequency, and the one that makes retuning
     * sixteen FDN dampers cost one @c exp instead of sixteen. RT-safe.
     */
    void setCoeffs(const OnePoleCoeffs& c) noexcept
    {
        b0_ = c.b0;
        b1_ = c.b1;
        a1_ = c.a1;
    }

    /// @return The coefficients currently in effect. RT-safe.
    [[nodiscard]] OnePoleCoeffs coeffs() const noexcept { return { b0_, b1_, a1_ }; }

    /**
     * @brief Process one input sample. RT-safe, @c noexcept.
     * @param x Input sample.
     * @return Filtered output sample.
     */
    [[nodiscard]] float process(float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_           = x;
        y1_           = y;
        return y;
    }

    /// Clear filter memory. RT-safe.
    void reset() noexcept
    {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

private:
    core::SampleRate sampleRate_ = 44100.0;
    float            b0_         = 1.0f;
    float            b1_         = 0.0f;
    float            a1_         = 0.0f;
    float            x1_         = 0.0f;
    float            y1_         = 0.0f;
};

} // namespace caecilia::dsp
