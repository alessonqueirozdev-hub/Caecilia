// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/synthesis/SampleVoice.h"

#include <cmath>

namespace caecilia::synth
{

void SampleVoice::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    envelope_.configure(sampleRate_, 0.002f, release_.shortReleaseMs * 0.001f);
    envelope_.reset();
    cursor_      = 0.0;
    heldSeconds_ = 0.0;
    oneShotDone_ = false;
}

void SampleVoice::noteOn(core::PipeId pipe, core::Velocity velocity) noexcept
{
    cursor_      = 0.0;
    heldSeconds_ = 0.0;
    oneShotDone_ = false;

    // Velocity + deterministic per-pipe level trim.
    const float v = static_cast<float>(velocity) / 127.0f;
    levelGain_ = (0.4f + 0.6f * v) * std::pow(10.0f, voicing_.levelTrimDb * 0.05f);

    // Resample ratio: (source SR / host SR) preserves pitch; the pitch shift is
    // the ratio of desired to recorded frequency, plus per-pipe detune.
    double step = 1.0;
    if (source_ != nullptr)
    {
        const double desired = soundingFrequencyHz(context_, pipe)
                             * std::pow(2.0, static_cast<double>(voicing_.detuneCents) / 1200.0);
        const double root = source_->rootFrequencyHz();
        const double srcSr = source_->sourceSampleRate();
        const double pitchRatio = root > 0.0 ? desired / root : 1.0;
        const double srRatio    = sampleRate_ > 0.0 ? srcSr / sampleRate_ : 1.0;
        step = pitchRatio * srRatio;
    }
    step_ = step > 0.0 ? step : 1.0;

    envelope_.configure(sampleRate_, 0.002f, release_.shortReleaseMs * 0.001f);
    envelope_.noteOn();
}

void SampleVoice::noteOff() noexcept
{
    // Choose a release time from how long the note was held.
    envelope_.setReleaseTime(sampleRate_, release_.releaseMsForHold(heldSeconds_) * 0.001f);
    envelope_.noteOff();
}

void SampleVoice::silence() noexcept
{
    envelope_.reset();
    cursor_      = 0.0;
    heldSeconds_ = 0.0;
}

void SampleVoice::setExpression(float startGain, float incPerSample) noexcept
{
    exprGain_ = startGain;
    exprInc_  = incPerSample;
}

void SampleVoice::adoptRank(const void* voicing) noexcept
{
    // Per-rank voicing is an additive-bank feature; this tier is not part of
    // the migration and keeps whatever it was configured with.
    (void) voicing;
}

void SampleVoice::renderAdd(core::AudioBlock& block) noexcept
{
    if (source_ == nullptr || oneShotDone_ || !envelope_.isActive() || block.isEmpty())
        return;

    const std::size_t frames    = block.numFrames();
    const std::size_t outChans  = block.numChannels();
    const std::size_t srcChans  = source_->channelCount();
    const std::size_t total     = source_->frameCount();
    const std::size_t loopStart = source_->loopStart();
    const std::size_t loopEnd   = source_->loopEnd();
    const bool        looping   = loopEnd > loopStart && loopEnd <= total;

    if (total == 0)
        return;

    for (std::size_t n = 0; n < frames; ++n)
    {
        // Loop / one-shot boundary handling.
        if (looping)
        {
            if (cursor_ >= static_cast<double>(loopEnd))
                cursor_ -= static_cast<double>(loopEnd - loopStart);
        }
        else if (cursor_ >= static_cast<double>(total - 1))
        {
            oneShotDone_ = true;
            break;
        }

        const std::size_t i0 = static_cast<std::size_t>(cursor_);
        const std::size_t i1 = i0 + 1 < total ? i0 + 1 : i0;
        const float frac = static_cast<float>(cursor_ - static_cast<double>(i0));

        const float envGain = envelope_.next() * levelGain_;

        for (std::size_t c = 0; c < outChans; ++c)
        {
            const std::size_t sc = c < srcChans ? c : srcChans - 1;
            // TODO(phase4): replace this linear tap with dsp::SincKaiser16Interpolator.
            const float s0 = source_->sampleAt(sc, i0);
            const float s1 = source_->sampleAt(sc, i1);
            const float sample = s0 + (s1 - s0) * frac;

            float* dst = block.channel(c);
            if (dst != nullptr)
                dst[n] += sample * envGain;
        }

        cursor_ += step_;

        if (!envelope_.isActive())
            break; // release finished mid-block.
    }

    heldSeconds_ += static_cast<double>(frames) / sampleRate_;
}

bool SampleVoice::isActive() const noexcept
{
    return !oneShotDone_ && envelope_.isActive();
}

float SampleVoice::cpuCostEstimate() const noexcept
{
    // Streamed playback with interpolation is cheap and fixed-cost.
    return 0.3f;
}

} // namespace caecilia::synth
