/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/IResonator.h"

#include <cstddef>
#include <vector>

namespace caecilia::synth
{

/**
 * @brief A tuned delay-line resonator modelling a flue pipe's air column.
 *
 * This scaffold implements a stable, lossy single-delay waveguide (a
 * damped comb): the excitation is injected into a delay line whose length sets
 * the pitch, with a one-pole low-pass in the feedback path modelling frequency-
 * dependent losses. It is written fresh from public delay-line/waveguide math;
 * no GPL DSP is referenced.
 *
 * @note A full bidirectional waveguide with fractional-delay tuning, a proper
 *       reflection/junction model and wind-coupled length lands in phase 6; this
 *       integer-delay version is a coherent, stable placeholder.
 *
 * ## Real-time contract
 * - @ref prepare allocates the delay line (sized for the lowest supported note);
 *   not RT-safe.
 * - @ref setFrequency, @ref process, @ref reset are @c noexcept, allocation-free.
 */
class Waveguide final : public IResonator
{
public:
    /// Lowest supported frequency; sizes the delay line in @ref prepare.
    static constexpr double kMinFrequencyHz = 16.0;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void setFrequency(double frequencyHz) noexcept override;
    [[nodiscard]] float process(float excitation) noexcept override;
    void reset() noexcept override;

    /// Set the loop feedback gain in [0, 1); higher = longer sustain. RT-safe.
    void setFeedback(float feedback) noexcept;

    /// Set the loop damping in [0, 1]; higher = darker tail. RT-safe.
    void setDamping(float damping) noexcept { damping_ = damping; }

private:
    std::vector<float> line_;                ///< Delay buffer, allocated in prepare().
    std::size_t        writeIndex_  = 0;
    std::size_t        delaySamples_ = 2;
    double             sampleRate_  = 0.0;
    float              feedback_    = 0.98f;
    float              damping_     = 0.2f;
    float              dampState_   = 0.0f;  ///< One-pole low-pass memory in the loop.
};

} // namespace caecilia::synth
