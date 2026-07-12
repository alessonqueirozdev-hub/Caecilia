/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/synthesis/PhysicalPipeVoice.h"

#include <cmath>
#include <utility>

namespace caecilia::synth
{

void PhysicalPipeVoice::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    if (excitation_) excitation_->prepare(sampleRate_, maxBlockFrames);
    if (resonator_)  resonator_->prepare(sampleRate_, maxBlockFrames);
    envelope_.configure(sampleRate_, 0.006f, release_.shortReleaseMs * 0.001f);
    envelope_.reset();
    heldSeconds_ = 0.0;
}

void PhysicalPipeVoice::setComponents(std::unique_ptr<IExcitation> excitation,
                                      std::unique_ptr<IResonator> resonator,
                                      core::EngineKind engineKind,
                                      core::VoiceTier tier) noexcept
{
    excitation_ = std::move(excitation);
    resonator_  = std::move(resonator);
    engineKind_ = engineKind;
    tier_       = tier;
}

void PhysicalPipeVoice::noteOn(core::PipeId pipe, core::Velocity velocity) noexcept
{
    chest_       = context_.chest;
    heldSeconds_ = 0.0;

    double frequency = soundingFrequencyHz(context_, pipe)
                     * std::pow(2.0, static_cast<double>(voicing_.detuneCents) / 1200.0);

    masterGain_ = 0.5f * std::pow(10.0f, voicing_.levelTrimDb * 0.05f);

    if (excitation_) excitation_->trigger(frequency, velocity);
    if (resonator_)
    {
        resonator_->reset();
        resonator_->setFrequency(frequency);
    }

    envelope_.configure(sampleRate_, 0.006f, release_.shortReleaseMs * 0.001f);
    envelope_.noteOn();
}

void PhysicalPipeVoice::noteOff() noexcept
{
    envelope_.setReleaseTime(sampleRate_, release_.releaseMsForHold(heldSeconds_) * 0.001f);
    envelope_.noteOff();
    // TODO(phase6): trigger the pressure-collapse chiff on the excitation as the
    // wind falls away, per ReleaseSpec::pressureCollapseChiff.
}

void PhysicalPipeVoice::renderAdd(core::AudioBlock& block) noexcept
{
    if (!envelope_.isActive() || block.isEmpty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();

    // Per-block wind coupling: read the immutable snapshot once and drive the
    // excitation. TODO(phase6): tap the wind per control-rate sub-block for
    // sample-accurate FM/AM through the nonlinear stage.
    if (excitation_ != nullptr && context_.wind != nullptr)
    {
        const float pressure  = context_.wind->pressureAt(chest_, 0);
        const float deviation = context_.wind->pressureDeviation(chest_, 0);
        excitation_->setWind(pressure, deviation);
    }

    for (std::size_t n = 0; n < frames; ++n)
    {
        const float env = envelope_.next();

        float sample = 0.0f;
        if (excitation_ != nullptr && resonator_ != nullptr)
        {
            const float exc = excitation_->process();
            sample = resonator_->process(exc);
        }
        sample *= env * masterGain_;

        for (std::size_t c = 0; c < channels; ++c)
        {
            float* dst = block.channel(c);
            if (dst != nullptr)
                dst[n] += sample;
        }

        if (!envelope_.isActive())
            break;
    }

    heldSeconds_ += static_cast<double>(frames) / sampleRate_;
}

bool PhysicalPipeVoice::isActive() const noexcept
{
    return envelope_.isActive();
}

float PhysicalPipeVoice::cpuCostEstimate() const noexcept
{
    // The nonlinear physical tier is the most expensive; reeds (modal) sit a
    // little below the waveguide flues.
    return engineKind_ == core::EngineKind::Modal ? 1.4f : 1.6f;
}

} // namespace caecilia::synth
