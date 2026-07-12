/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/synthesis/NonlinearJet.h"

namespace caecilia::synth
{

void NonlinearJet::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    reset();
}

void NonlinearJet::trigger(double frequencyHz, core::Velocity velocity) noexcept
{
    frequencyHz_   = frequencyHz;
    drive_         = static_cast<float>(velocity) / 127.0f;
    phase_         = 0.0f;
    settleCounter_ = 0;
    // Jet establishment time scales inversely with pitch; ~8 ms at unison.
    settleTarget_  = static_cast<std::uint32_t>(0.008 * sampleRate_);
}

void NonlinearJet::setWind(float pressurePa, float pressureDeviation) noexcept
{
    pressurePa_ = pressurePa;
    deviation_  = pressureDeviation;
}

float NonlinearJet::process() noexcept
{
    if (settleCounter_ < settleTarget_)
        ++settleCounter_;

    // TODO(phase6): bandlimited nonlinear jet-drive with wind-coupled phase
    // offset and emergent chiff, evaluated inside the oversampled polyphase
    // island. Placeholder returns silence so the resonator sees no drive yet.
    return 0.0f;
}

void NonlinearJet::reset() noexcept
{
    frequencyHz_   = 0.0;
    pressurePa_    = 0.0f;
    deviation_     = 0.0f;
    drive_         = 0.0f;
    phase_         = 0.0f;
    settleCounter_ = 0;
    settleTarget_  = 0;
}

bool NonlinearJet::isSettled() const noexcept
{
    return settleCounter_ >= settleTarget_;
}

} // namespace caecilia::synth
