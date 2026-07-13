/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/dsp/Biquad.h"

#include <array>
#include <cstddef>

/**
 * @file MasterEq.h
 * @brief A 5-band stereo master EQ voiced for pipe organ.
 *
 * A post-reverb, pre-limiter tone-voicing stage: low shelf (warmth), three
 * parametric peaks (tame boxiness, body, presence) and a high shelf (air). Built
 * on the existing transposed-direct-form-II @ref Biquad, so it is click-free and
 * a band at 0 dB is a true mathematical pass-through (no bypass branch needed).
 *
 * ## Real-time contract
 * - @ref prepare allocates nothing beyond the fixed filter array. Not RT-safe only
 *   because it touches the sample rate.
 * - @ref setBand / @ref setEnabled store targets; coefficients are redesigned once
 *   per block from block-rate-smoothed gains, so slider moves never click. RT-safe.
 * - @ref process filters in place. RT-safe, @c noexcept, allocation-free.
 */
namespace caecilia::dsp
{

class MasterEq
{
public:
    static constexpr std::size_t kBands = 5;

    /// Band roles (fixed filter type per index).
    enum Band { Warmth = 0, Boxiness = 1, Body = 2, Presence = 3, Air = 4 };

    MasterEq() = default;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t numChannels);

    /// Set a band's target frequency, Q and gain (dB). Coeffs glide to it. RT-safe.
    void setBand(std::size_t index, float freqHz, float q, float gainDb) noexcept;
    /// Convenience: change only a band's gain (keeps its freq/Q). RT-safe.
    void setBandGain(std::size_t index, float gainDb) noexcept;

    void setEnabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// Install the default pipe-organ voicing (gentle, <= +/-2.5 dB).
    void setOrganDefaults() noexcept;

    void process(core::AudioBlock& block) noexcept;
    void reset() noexcept;

private:
    struct BandState
    {
        float freq     = 1000.0f;
        float q        = 0.7f;
        float targetDb = 0.0f;
        float curDb    = 0.0f;
    };

    core::SampleRate sr_       = 48000.0;
    bool             enabled_  = true;
    float            smoothA_  = 0.3f;   ///< Per-block gain-glide coefficient.

    std::array<BandState, kBands> bands_{};
    std::array<Biquad, kBands>    filtL_{};
    std::array<Biquad, kBands>    filtR_{};

    [[nodiscard]] BiquadCoeffs design(std::size_t band, float gainDb) const noexcept;
};

} // namespace caecilia::dsp
