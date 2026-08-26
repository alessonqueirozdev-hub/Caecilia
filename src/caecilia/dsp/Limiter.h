// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
    /// Largest look-ahead @ref setParams will ever accept, and therefore the
    /// window @ref prepare must reserve room for. The ring is sized from THIS,
    /// never from the current @c lookAheadMs_ — sizing it from the current value
    /// let a later setParams() raise look_ past the ring length, which wrapped the
    /// read offset and put a hard splice into every ring cycle of the output.
    static constexpr float kMaxLookAheadMs = 10.0f;

    Limiter() = default;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t numChannels);

    /// @param ceilingDb   Output ceiling (peaks brought to this level); default -1 dBFS.
    /// @param lookAheadMs Look-ahead / attack window in ms; default ~2 ms.
    /// @param holdMs      Hold time: after a reduction the gain is FROZEN this long
    ///                    before it may recover. This is what turns a pumping
    ///                    compressor into a transparent safety limiter — a sustained
    ///                    organ Tutti keeps re-triggering the hold, so the gain sits
    ///                    rock-steady instead of breathing between peaks (the
    ///                    Aeolus "sustain" idea). Default ~400 ms.
    /// @param releaseMs   Release time in ms once the hold expires; default ~600 ms.
    void setParams(float ceilingDb, float lookAheadMs, float holdMs, float releaseMs) noexcept;

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
    float holdMs_      = 400.0f;
    float releaseMs_   = 600.0f;
    float atkCoef_     = 0.5f;      ///< Per-sample attack smoothing coefficient.
    float relCoef_     = 0.001f;    ///< Per-sample release smoothing coefficient.
    std::size_t holdSamples_ = 0;   ///< Hold length in samples (from holdMs_).

    std::array<std::vector<float>, 2> delay_{}; ///< Per-channel look-ahead ring.
    std::size_t writePos_    = 0;
    float       gEnv_        = 1.0f; ///< Smoothed gain envelope (1 = no reduction).
    std::size_t holdCounter_ = 0;    ///< Samples remaining in the current hold.
};

} // namespace caecilia::dsp
