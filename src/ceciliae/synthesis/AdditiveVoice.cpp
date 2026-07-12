/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/synthesis/AdditiveVoice.h"
#include "ceciliae/wind/WindResponseCurve.h"

#include <cmath>

namespace ceciliae::synth
{

void AdditiveVoice::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames)
{
    bank_.prepare(sampleRate, maxBlockFrames);
}

void AdditiveVoice::setContext(const VoiceContext& context) noexcept
{
    context_ = context;
    // Wire the wind coupling from the tonal family's response curve (the curve
    // type and its per-family defaults are owned by the wind module).
    bank_.setWindCoupling(context_.wind,
                          context_.chest,
                          wind::defaultResponseFor(context_.family));
}

void AdditiveVoice::seedFrom(const SpectralModel& model, float phaseAlignSeconds) noexcept
{
    bank_.seedFrom(model, phaseAlignSeconds);
}

void AdditiveVoice::noteOn(core::PipeId pipe, core::Velocity velocity) noexcept
{
    pipe_ = pipe;

    double frequency = soundingFrequencyHz(context_, pipe);
    // Apply the deterministic per-pipe detune (cents -> ratio).
    frequency *= std::pow(2.0, static_cast<double>(voicing_.detuneCents) / 1200.0);

    // The per-pipe level trim folds into the bank's master gain.
    bank_.setMasterGain(0.25f * std::pow(10.0f, voicing_.levelTrimDb * 0.05f));
    bank_.trigger(pipe, velocity, frequency);
}

void AdditiveVoice::noteOff() noexcept
{
    bank_.release();
}

void AdditiveVoice::renderAdd(core::AudioBlock& block) noexcept
{
    bank_.renderAdd(block);
}

bool AdditiveVoice::isActive() const noexcept
{
    return bank_.isActive();
}

float AdditiveVoice::cpuCostEstimate() const noexcept
{
    // Roughly proportional to the seeded partial count; additive is mid-cost.
    return 0.5f + 0.02f * static_cast<float>(bank_.activePartialCount());
}

} // namespace ceciliae::synth
