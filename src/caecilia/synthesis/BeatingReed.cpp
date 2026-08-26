// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/synthesis/BeatingReed.h"

namespace caecilia::synth
{

void BeatingReed::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    reset();
}

void BeatingReed::trigger(double frequencyHz, core::Velocity velocity) noexcept
{
    frequencyHz_   = frequencyHz;
    drive_         = static_cast<float>(velocity) / 127.0f;
    tonguePos_     = 0.0f;
    settleCounter_ = 0;
    // Reed lock-in is slower than a flue jet; ~15 ms at unison.
    settleTarget_  = static_cast<std::uint32_t>(0.015 * sampleRate_);
}

void BeatingReed::setWind(float pressurePa, float pressureDeviation) noexcept
{
    pressurePa_ = pressurePa;
    deviation_  = pressureDeviation;
}

float BeatingReed::process() noexcept
{
    if (settleCounter_ < settleTarget_)
        ++settleCounter_;

    // TODO(phase6): nonlinear tongue-vs-shallot beating with pressure-dependent
    // closing, wind-coupled pitch and emergent speech, bandlimited in the
    // oversampled island. Placeholder returns silence.
    return 0.0f;
}

void BeatingReed::reset() noexcept
{
    frequencyHz_   = 0.0;
    pressurePa_    = 0.0f;
    deviation_     = 0.0f;
    drive_         = 0.0f;
    tonguePos_     = 0.0f;
    settleCounter_ = 0;
    settleTarget_  = 0;
}

bool BeatingReed::isSettled() const noexcept
{
    return settleCounter_ >= settleTarget_;
}

} // namespace caecilia::synth
