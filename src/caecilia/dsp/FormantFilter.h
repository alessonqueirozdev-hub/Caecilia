// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/dsp/Biquad.h"

#include <array>
#include <cstddef>

/**
 * @file FormantFilter.h
 * @brief A parallel bank of resonant peaks modelling a fixed formant envelope.
 *
 * Reed resonators and the steady-state spectral colour of many flue voices are
 * dominated by a handful of fixed formant regions. This filter sums a small bank
 * of peaking/band-pass biquads to impose that envelope on an excitation signal,
 * and mirrors the @c FormantEnvelope carried by the analysis SpectralModel.
 */

namespace caecilia::dsp
{

/// One resonant formant region.
struct Formant
{
    float centreHz     = 500.0f; ///< Centre frequency of the resonance.
    float bandwidthHz  = 80.0f;  ///< -3 dB bandwidth (drives the biquad Q).
    float gainDb       = 0.0f;   ///< Peak gain of the region.
};

/// Maximum simultaneous formant regions (F1..F5 covers vowels and most reeds).
inline constexpr std::size_t kMaxFormants = 5;

/**
 * @brief A parallel bank of up to @ref kMaxFormants resonant peaks.
 *
 * Real-time contract: @ref setFormants is RT-safe (designs coefficients in place,
 * no allocation) but is cheapest on parameter changes; @ref process and
 * @ref reset are @c noexcept hot-path safe.
 */
class FormantFilter
{
public:
    FormantFilter() noexcept = default;

    /// Store the sample rate used to design each peak. Call before setFormants.
    void prepare(core::SampleRate sampleRate) noexcept { sampleRate_ = sampleRate; }

    /**
     * @brief Configure the bank from up to @ref kMaxFormants regions.
     * @param formants Pointer to @p count Formant descriptors.
     * @param count    Number of active formants (clamped to kMaxFormants).
     *
     * Designs one constant-peak-gain band-pass per formant (Q = centre/bandwidth)
     * and uses @ref Formant::gainDb as that section's summing weight, not as a
     * biquad gain. RT-safe: no allocation.
     */
    void setFormants(const Formant* formants, std::size_t count) noexcept;

    /// Process one sample through the parallel bank. RT-safe, @c noexcept.
    [[nodiscard]] float process(float x) noexcept;

    /// Clear all section memory. RT-safe.
    void reset() noexcept;

    /// @return Number of currently active formant sections.
    [[nodiscard]] std::size_t activeCount() const noexcept { return active_; }

private:
    core::SampleRate               sampleRate_ = 44100.0;
    std::array<Biquad, kMaxFormants> sections_{};
    std::array<float, kMaxFormants>  weights_{}; ///< Per-section linear gain.
    std::size_t                      active_ = 0;
};

} // namespace caecilia::dsp
