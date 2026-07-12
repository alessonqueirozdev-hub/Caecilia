/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

/**
 * @file OnePole.h
 * @brief A first-order (one-pole / one-zero) IIR filter.
 *
 * Cheap building block used for damping (FDN feedback lines), DC blocking, and
 * gentle tone shaping. Coefficients are computed from a cutoff at configure-time;
 * the per-sample @ref OnePole::process is a two-multiply-add hot-path kernel.
 */

namespace ceciliae::dsp
{

/**
 * @brief First-order low-pass / high-pass filter with a single state pair.
 *
 * The difference equation is @c y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1], which
 * realises either a one-pole low-pass or a one-pole high-pass depending on how
 * the coefficients are set. One instance realises one response; pick the setter
 * that matches the role.
 *
 * Real-time contract: @ref setLowpass / @ref setHighpass may be called from the
 * audio thread (no allocation), but coefficient recomputation is cheapest done
 * on parameter changes. @ref process and @ref reset are @c noexcept hot-path
 * safe.
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

} // namespace ceciliae::dsp
