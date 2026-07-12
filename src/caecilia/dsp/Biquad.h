/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>

/**
 * @file Biquad.h
 * @brief Second-order (biquad) IIR filter plus its coefficient designers.
 *
 * The transfer-function designers below are standard, textbook analogue-prototype
 * bilinear-transform formulas (public-domain DSP mathematics), authored fresh.
 * No GPL DSP source is referenced.
 */

namespace caecilia::dsp
{

/**
 * @brief Normalised biquad coefficients (a0 folded to 1).
 *
 * Difference equation (Transposed Direct Form II):
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2].
 */
struct BiquadCoeffs
{
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    // --- Designers. All take the sample rate and return normalised coeffs. ---

    /// Second-order low-pass at @p freqHz with quality @p q.
    [[nodiscard]] static BiquadCoeffs lowpass(core::SampleRate fs, float freqHz, float q) noexcept;
    /// Second-order high-pass at @p freqHz with quality @p q.
    [[nodiscard]] static BiquadCoeffs highpass(core::SampleRate fs, float freqHz, float q) noexcept;
    /// Constant-peak-gain band-pass centred at @p freqHz with quality @p q.
    [[nodiscard]] static BiquadCoeffs bandpass(core::SampleRate fs, float freqHz, float q) noexcept;
    /// Band-reject (notch) centred at @p freqHz with quality @p q.
    [[nodiscard]] static BiquadCoeffs notch(core::SampleRate fs, float freqHz, float q) noexcept;
    /// Peaking EQ: @p gainDb boost/cut in a band of quality @p q around @p freqHz.
    [[nodiscard]] static BiquadCoeffs peaking(core::SampleRate fs,
                                              float            freqHz,
                                              float            q,
                                              float            gainDb) noexcept;
    /// Low-shelf of @p gainDb below @p freqHz with slope @p q.
    [[nodiscard]] static BiquadCoeffs lowShelf(core::SampleRate fs,
                                               float            freqHz,
                                               float            q,
                                               float            gainDb) noexcept;
    /// High-shelf of @p gainDb above @p freqHz with slope @p q.
    [[nodiscard]] static BiquadCoeffs highShelf(core::SampleRate fs,
                                                float            freqHz,
                                                float            q,
                                                float            gainDb) noexcept;
};

/**
 * @brief A single biquad section with per-instance state.
 *
 * Real-time contract: @ref setCoeffs is RT-safe (a plain copy, no reallocation);
 * @ref process, @ref processBlock and @ref reset are @c noexcept hot-path safe.
 * Coefficient design (the @ref BiquadCoeffs static helpers) is best done off the
 * audio thread on parameter changes.
 */
class Biquad
{
public:
    Biquad() noexcept = default;

    /// Swap in new coefficients without clearing state (click-free). RT-safe.
    void setCoeffs(const BiquadCoeffs& c) noexcept { coeffs_ = c; }

    /// @return The active coefficient set.
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return coeffs_; }

    /// Process one sample. RT-safe, @c noexcept.
    [[nodiscard]] float process(float x) noexcept
    {
        const float y = coeffs_.b0 * x + z1_;
        z1_           = coeffs_.b1 * x - coeffs_.a1 * y + z2_;
        z2_           = coeffs_.b2 * x - coeffs_.a2 * y;
        return y;
    }

    /// Process @p count samples in place. RT-safe, @c noexcept.
    void processBlock(float* data, std::size_t count) noexcept;

    /// Clear filter memory. RT-safe.
    void reset() noexcept
    {
        z1_ = 0.0f;
        z2_ = 0.0f;
    }

private:
    BiquadCoeffs coeffs_{};
    float        z1_ = 0.0f;
    float        z2_ = 0.0f;
};

} // namespace caecilia::dsp
