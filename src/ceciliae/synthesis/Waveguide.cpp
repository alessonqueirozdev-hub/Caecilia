/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/synthesis/Waveguide.h"

namespace ceciliae::synth
{

void Waveguide::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;

    // Size the delay line for the lowest supported note, plus a small guard.
    const std::size_t maxDelay = static_cast<std::size_t>(sampleRate_ / kMinFrequencyHz) + 4;
    line_.assign(maxDelay, 0.0f);

    writeIndex_   = 0;
    delaySamples_ = 2;
    dampState_    = 0.0f;
}

void Waveguide::setFrequency(double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0 || line_.empty())
        return;

    std::size_t d = static_cast<std::size_t>(sampleRate_ / frequencyHz + 0.5);
    if (d < 2)
        d = 2;
    if (d >= line_.size())
        d = line_.size() - 1;
    delaySamples_ = d;
}

void Waveguide::setFeedback(float feedback) noexcept
{
    // Keep strictly below unity for guaranteed stability.
    if (feedback < 0.0f)  feedback = 0.0f;
    if (feedback > 0.999f) feedback = 0.999f;
    feedback_ = feedback;
}

float Waveguide::process(float excitation) noexcept
{
    if (line_.empty())
        return 0.0f;

    const std::size_t size = line_.size();
    const std::size_t readIndex =
        (writeIndex_ + size - delaySamples_) % size;

    const float delayed = line_[readIndex];

    // One-pole low-pass in the feedback path (frequency-dependent loss).
    dampState_ = damping_ * dampState_ + (1.0f - damping_) * delayed;

    const float out = excitation + feedback_ * dampState_;

    line_[writeIndex_] = out;
    writeIndex_ = (writeIndex_ + 1) % size;

    return out;
}

void Waveguide::reset() noexcept
{
    for (float& s : line_)
        s = 0.0f;
    writeIndex_ = 0;
    dampState_  = 0.0f;
}

} // namespace ceciliae::synth
