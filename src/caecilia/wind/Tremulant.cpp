// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/wind/Tremulant.h"

#include <cmath>

namespace caecilia::wind
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

/// Wrap a phase into [0, 1).
[[nodiscard]] double wrap01(double phase) noexcept
{
    phase -= std::floor(phase);
    // Guard against a -0.0 / rounding landing exactly on 1.0.
    return phase >= 1.0 ? 0.0 : phase;
}
} // namespace

void Tremulant::configure(const TremulantConfig& config, float chestNominalPa) noexcept
{
    config_        = config;
    depthTargetPa_ = config.depthFraction * chestNominalPa;
    nominalPa_     = chestNominalPa;
    reset();
}

void Tremulant::setShape(float rateHz, float depthFraction,
                         float chestNominalPa) noexcept
{
    config_.rateHz        = rateHz;
    config_.depthFraction = depthFraction;
    nominalPa_            = chestNominalPa;
    depthTargetPa_        = depthFraction * chestNominalPa;
}

void Tremulant::reset() noexcept
{
    depthPa_    = depthTargetPa_;
    phase_      = 0.0;
    phaseStart_ = 0.0;
    phaseInc_   = 0.0;
    enabled_    = config_.enabledAtStart;
    gain_       = enabled_ ? 1.0f : 0.0f;
}

void Tremulant::setEnabled(bool shouldBeEnabled) noexcept
{
    enabled_ = shouldBeEnabled;
}

void Tremulant::advanceBlock(std::size_t numFrames, core::SampleRate sampleRate) noexcept
{
    phaseStart_ = phase_;
    phaseInc_   = sampleRate > 0.0 ? static_cast<double>(config_.rateHz) / sampleRate : 0.0;
    phase_      = wrap01(phase_ + phaseInc_ * static_cast<double>(numFrames));

    // Click-free depth ramp toward the target enable state. A short per-block
    // approach keeps engaging/disengaging the tremulant from popping.
    // TODO(phase2): make the ramp time constant sample-rate/tempo aware.
    const float target = enabled_ ? 1.0f : 0.0f;
    gain_ += (target - gain_) * 0.25f;

    // The depth follows the same per-block ramp, so a knob turn or an automation
    // jump is a glide rather than a step in chest pressure.
    depthPa_ += (depthTargetPa_ - depthPa_) * 0.25f;
}

float Tremulant::pressureDeltaPa(std::size_t frameInBlock) const noexcept
{
    if (gain_ <= 0.0f || depthPa_ == 0.0f)
        return 0.0f;

    const double phase = wrap01(phaseStart_ + phaseInc_ * static_cast<double>(frameInBlock));
    return depthPa_ * evaluateWaveform(phase) * gain_;
}

float Tremulant::evaluateWaveform(double phase01) const noexcept
{
    switch (config_.waveform)
    {
        case TremulantWaveform::Sine:
            return static_cast<float>(std::sin(kTwoPi * phase01));

        case TremulantWaveform::Triangle:
            // Rising 0→1 over the first half, falling 1→0 over the second,
            // mapped to [-1, 1].
            return static_cast<float>(phase01 < 0.5 ? (4.0 * phase01 - 1.0)
                                                     : (3.0 - 4.0 * phase01));

        case TremulantWaveform::AsymmetricSaw:
            // Fast attack over the first quarter, slow recovery over the rest —
            // reminiscent of a mechanical tremulant valve.
            return static_cast<float>(phase01 < 0.25 ? (8.0 * phase01 - 1.0)
                                                     : (1.0 - (phase01 - 0.25) / 0.75 * 2.0));
    }
    return 0.0f;
}

} // namespace caecilia::wind
