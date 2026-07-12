/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstdint>

namespace caecilia::synth
{

/**
 * @brief A minimal linear attack / sustain / release amplitude envelope.
 *
 * Shared by the sample and physical voices to gate their output and drive their
 * @c isActive() lifecycle. Deliberately tiny and branch-light; a richer
 * multi-stage, wind-modeled release (see @ref ReleaseSpec) layers on top in a
 * later phase.
 *
 * Real-time contract: @ref configure precomputes step sizes; @ref noteOn,
 * @ref noteOff, @ref next, @ref isActive and @ref reset are @c noexcept and
 * allocation-free.
 */
class ArEnvelope
{
public:
    /// Precompute per-sample step sizes. Call off the audio thread.
    void configure(core::SampleRate sampleRate, float attackSeconds, float releaseSeconds) noexcept
    {
        const double sr = sampleRate > 1.0 ? sampleRate : 48000.0;
        const float a = attackSeconds  > 0.0f ? attackSeconds  : 0.0005f;
        const float r = releaseSeconds > 0.0f ? releaseSeconds : 0.0005f;
        attackStep_  = static_cast<float>(1.0 / (a * sr));
        releaseStep_ = static_cast<float>(1.0 / (r * sr));
    }

    /// Begin the attack from the current gain (retrigger-safe). RT-safe.
    void noteOn() noexcept { stage_ = Stage::Attack; }

    /// Enter the release stage. RT-safe.
    void noteOff() noexcept
    {
        if (stage_ != Stage::Idle)
            stage_ = Stage::Release;
    }

    /// Override the release time (e.g. from a hold-time-dependent ReleaseSpec).
    void setReleaseTime(core::SampleRate sampleRate, float releaseSeconds) noexcept
    {
        const double sr = sampleRate > 1.0 ? sampleRate : 48000.0;
        const float r = releaseSeconds > 0.0f ? releaseSeconds : 0.0005f;
        releaseStep_ = static_cast<float>(1.0 / (r * sr));
    }

    /// Advance one sample and return the current gain in [0, 1]. RT-safe.
    [[nodiscard]] float next() noexcept
    {
        switch (stage_)
        {
            case Stage::Attack:
                gain_ += attackStep_;
                if (gain_ >= 1.0f) { gain_ = 1.0f; stage_ = Stage::Sustain; }
                break;
            case Stage::Release:
                gain_ -= releaseStep_;
                if (gain_ <= 0.0f) { gain_ = 0.0f; stage_ = Stage::Idle; }
                break;
            case Stage::Sustain:
            case Stage::Idle:
            default:
                break;
        }
        return gain_;
    }

    /// @return true unless the envelope has fully released. RT-safe.
    [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }

    /// Force the envelope to silence and idle. RT-safe.
    void reset() noexcept { stage_ = Stage::Idle; gain_ = 0.0f; }

private:
    enum class Stage : std::uint8_t { Idle, Attack, Sustain, Release };

    Stage stage_       = Stage::Idle;
    float gain_        = 0.0f;
    float attackStep_  = 1.0f;
    float releaseStep_ = 1.0f;
};

} // namespace caecilia::synth
