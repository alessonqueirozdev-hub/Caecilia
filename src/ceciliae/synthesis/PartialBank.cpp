/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/synthesis/PartialBank.h"

#include <cmath>

namespace ceciliae::synth
{

namespace
{
    constexpr float  kTwoPi        = 6.28318530717958647692f;
    constexpr double kMinSampleRate = 1.0;

    /// Convert decibels to a linear gain.
    [[nodiscard]] inline float dbToLinear(float db) noexcept
    {
        return std::pow(10.0f, db * 0.05f);
    }

    /// Wrap a phase into [0, 2*pi).
    [[nodiscard]] inline float wrapPhase(float phase) noexcept
    {
        while (phase >= kTwoPi) phase -= kTwoPi;
        while (phase < 0.0f)    phase += kTwoPi;
        return phase;
    }
} // namespace

void PartialBank::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > kMinSampleRate ? sampleRate : 48000.0;

    // The ONLY allocation in this class: reserve the maximum partial storage up
    // front so seedFrom()/renderAdd() never touch the heap.
    partials_.assign(maxPartials_, Partial{});
    partialCount_ = 0;

    setEnvelopeTimes(attackSeconds_, releaseSeconds_);
    stage_          = Stage::Idle;
    envGain_        = 0.0f;
    noteTimeSeconds_ = 0.0;
}

void PartialBank::seedFrom(const SpectralModel& model, float /*phaseAlignSeconds*/) noexcept
{
    // Copy only up to the reserved capacity; excess partials are dropped rather
    // than allocating. This keeps the call RT-safe.
    const std::size_t capacity = partials_.size();
    const std::size_t count    = model.partials.size() < capacity ? model.partials.size() : capacity;

    for (std::size_t i = 0; i < count; ++i)
    {
        const PartialTrack& t = model.partials[i];
        Partial& p          = partials_[i];
        p.ratioToF0         = t.ratioToF0;
        p.amplitude         = dbToLinear(t.ampDb);
        p.windSensitivity   = t.windSensitivity;
        p.onsetSeconds      = t.onsetSeconds;
        p.phase             = t.phase;
        p.increment         = 0.0f;
        p.blockGain         = 0.0f;
    }

    partialCount_  = count;
    fundamentalHz_ = model.fundamentalHz;
}

void PartialBank::trigger(core::PipeId /*pipe*/, core::Velocity velocity, double frequencyHz) noexcept
{
    fundamentalHz_   = frequencyHz;
    noteTimeSeconds_ = 0.0;

    // Velocity scales the initial drive; a gentle curve avoids a hard step. This
    // is kept separate from masterGain_ so an owner-set level trim survives.
    const float v = static_cast<float>(velocity) / 127.0f;
    velocityGain_ = 0.4f + 0.6f * v;

    // Reset oscillator phases so every note starts phase-coherent; the
    // per-pipe voicing seed (applied upstream) provides the desired scatter.
    for (std::size_t i = 0; i < partialCount_; ++i)
        partials_[i].phase = wrapPhase(partials_[i].phase);

    stage_   = Stage::Attack;
    envGain_ = 0.0f;
}

void PartialBank::release() noexcept
{
    if (stage_ != Stage::Idle)
        stage_ = Stage::Release;
}

void PartialBank::setWindCoupling(const core::IWindSupply* wind,
                                  core::WindchestId chest,
                                  wind::WindResponseCurve curve) noexcept
{
    wind_      = wind;
    chest_     = chest;
    windCurve_ = curve;
}

void PartialBank::setEnvelopeTimes(float attackSeconds, float releaseSeconds) noexcept
{
    attackSeconds_  = attackSeconds  > 0.0f ? attackSeconds  : 0.0005f;
    releaseSeconds_ = releaseSeconds > 0.0f ? releaseSeconds : 0.0005f;

    const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
    attackStep_  = static_cast<float>(1.0 / (attackSeconds_  * sr));
    releaseStep_ = static_cast<float>(1.0 / (releaseSeconds_ * sr));
}

bool PartialBank::isActive() const noexcept
{
    return stage_ != Stage::Idle;
}

void PartialBank::recomputeBlockCoefficients() noexcept
{
    const double sr      = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
    const double nyquist = 0.5 * sr;

    // Per-block wind coupling: read the pressure deviation once and translate it
    // into a global pitch/level shift through the tonal-family response curve.
    // TODO(phase2): sample the wind tap per control-rate sub-block for
    // sample-accurate FM/AM instead of one read per block.
    float deviation = 0.0f;
    if (wind_ != nullptr)
        deviation = wind_->pressureDeviation(chest_, 0);

    const double pitchRatio =
        std::pow(2.0, static_cast<double>(windCurve_.pitchCents(deviation)) / 1200.0);
    const float levelLin = dbToLinear(windCurve_.levelDb(deviation));

    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        Partial& p = partials_[i];

        // Per-partial wind sensitivity scales how much this partial tracks the
        // global deviation (brightness development lives here in a later phase).
        const double partialPitch = 1.0 + (pitchRatio - 1.0) * static_cast<double>(p.windSensitivity + 1.0f);
        const double freq         = fundamentalHz_ * static_cast<double>(p.ratioToF0) * partialPitch;

        // Anti-aliasing: drop partials above Nyquist and fade the top octave.
        float aaGain = 1.0f;
        if (freq >= nyquist)
        {
            aaGain = 0.0f;
        }
        else if (freq > nyquist * 0.5)
        {
            aaGain = static_cast<float>((nyquist - freq) / (nyquist * 0.5));
            if (aaGain < 0.0f) aaGain = 0.0f;
        }

        // Staggered onset: a partial speaks only after its onset time, then
        // ramps in over a short window so the chiff emerges rather than clicks.
        float onsetGain = 1.0f;
        if (p.onsetSeconds > 0.0f)
        {
            const double dt = noteTimeSeconds_ - static_cast<double>(p.onsetSeconds);
            if (dt <= 0.0)
                onsetGain = 0.0f;
            else if (dt < 0.01)
                onsetGain = static_cast<float>(dt / 0.01);
        }

        p.increment = static_cast<float>(kTwoPi * freq / sr);
        p.blockGain = p.amplitude * aaGain * onsetGain * levelLin;
    }
}

void PartialBank::renderAdd(core::AudioBlock& block) noexcept
{
    if (stage_ == Stage::Idle || block.isEmpty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();
    const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;

    recomputeBlockCoefficients();

    for (std::size_t n = 0; n < frames; ++n)
    {
        // Advance the whole-bank amplitude envelope.
        switch (stage_)
        {
            case Stage::Attack:
                envGain_ += attackStep_;
                if (envGain_ >= 1.0f) { envGain_ = 1.0f; stage_ = Stage::Sustain; }
                break;
            case Stage::Release:
                envGain_ -= releaseStep_;
                if (envGain_ <= 0.0f) { envGain_ = 0.0f; stage_ = Stage::Idle; }
                break;
            case Stage::Sustain:
            case Stage::Idle:
            default:
                break;
        }

        // Sum the partials for this frame.
        float sample = 0.0f;
        for (std::size_t i = 0; i < partialCount_; ++i)
        {
            Partial& p = partials_[i];
            sample += p.blockGain * std::sin(p.phase);
            p.phase += p.increment;
            if (p.phase >= kTwoPi) p.phase -= kTwoPi;
        }

        sample *= envGain_ * masterGain_ * velocityGain_;

        for (std::size_t c = 0; c < channels; ++c)
        {
            float* dst = block.channel(c);
            if (dst != nullptr)
                dst[n] += sample;
        }

        if (stage_ == Stage::Idle)
            break; // released tail finished mid-block.
    }

    noteTimeSeconds_ += static_cast<double>(frames) / sr;
}

} // namespace ceciliae::synth
