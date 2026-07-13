/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"

#include <array>
#include <cstddef>
#include <vector>

/**
 * @file Limiter.h
 * @brief A stereo-linked, look-ahead brick-wall peak limiter.
 *
 * The master bus needs a real limiter, not a waveshaper: a full-organ TUTTI sums
 * many voices and a per-sample @c tanh() flattens every peak above ~0.4 into
 * broadband intermodulation distortion (the "explosion"). This limiter instead
 * looks a couple of milliseconds AHEAD, so it can ramp the gain down BEFORE a peak
 * reaches the (delayed) output — the fff Tutti gets louder and stays clean, with a
 * few dB of inaudible gain reduction, and quiet passages pass through untouched.
 *
 * Written fresh from standard public-domain limiter theory (feedforward peak
 * detection + look-ahead delay + attack/release smoothing). No GPL source.
 *
 * ## Real-time contract
 * - @ref prepare allocates the look-ahead delay lines. Not RT-safe.
 * - @ref setParams recomputes coefficients in place. RT-safe.
 * - @ref process limits in place. RT-safe, @c noexcept, allocation-free.
 * - @ref latencySamples reports the look-ahead for host delay compensation.
 */
namespace caecilia::dsp
{

class Limiter
{
public:
    Limiter() = default;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t numChannels);

    /// @param ceilingDb   Output ceiling (peaks brought to this level); default -1 dBFS.
    /// @param lookAheadMs Look-ahead / attack window in ms; default ~2 ms.
    /// @param releaseMs   Release time in ms; default ~90 ms.
    void setParams(float ceilingDb, float lookAheadMs, float releaseMs) noexcept;

    void process(core::AudioBlock& block) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::size_t latencySamples() const noexcept { return look_; }
    /// Current gain reduction in dB (0 = none), for a future GR meter.
    [[nodiscard]] float gainReductionDb() const noexcept;

private:
    void recompute() noexcept;

    core::SampleRate sr_          = 48000.0;
    std::size_t      numChannels_ = 2;
    std::size_t      look_        = 0;     ///< Look-ahead length (samples).

    float ceilingLin_ = 0.891251f; ///< dbToGain(-1).
    float lookAheadMs_ = 2.0f;
    float releaseMs_   = 90.0f;
    float atkCoef_     = 0.5f;      ///< Per-sample attack smoothing coefficient.
    float relCoef_     = 0.001f;    ///< Per-sample release smoothing coefficient.

    std::array<std::vector<float>, 2> delay_{}; ///< Per-channel look-ahead ring.
    std::size_t writePos_ = 0;
    float       gEnv_     = 1.0f;   ///< Smoothed gain envelope (1 = no reduction).
};

} // namespace caecilia::dsp
