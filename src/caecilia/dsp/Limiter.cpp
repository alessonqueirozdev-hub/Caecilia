/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/DspMath.h"

#include <cmath>

namespace caecilia::dsp
{

void Limiter::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/, std::size_t numChannels)
{
    sr_          = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels_ = numChannels == 0 ? 1 : (numChannels > 2 ? 2 : numChannels);

    recompute();

    // The delay ring must hold at least look_+1 samples; keep a small margin.
    const std::size_t ringLen = look_ + 2;
    for (auto& d : delay_)
        d.assign(ringLen, 0.0f);
    writePos_ = 0;
    gEnv_     = 1.0f;
}

void Limiter::setParams(float ceilingDb, float lookAheadMs, float holdMs, float releaseMs) noexcept
{
    ceilingLin_  = dbToGain(clamp(ceilingDb, -24.0f, 0.0f));
    lookAheadMs_ = clamp(lookAheadMs, 0.2f, 10.0f);
    holdMs_      = clamp(holdMs, 0.0f, 2000.0f);
    releaseMs_   = clamp(releaseMs, 5.0f, 2000.0f);
    recompute();
}

void Limiter::recompute() noexcept
{
    const double sr = sr_ > 0.0 ? sr_ : 48000.0;
    look_ = static_cast<std::size_t>(lookAheadMs_ * 0.001f * sr + 0.5f);
    if (look_ < 1) look_ = 1;

    // Attack must be FULLY settled by the time a peak reaches the delayed output,
    // i.e. within the look-ahead window. A one-pole with time constant = look_ only
    // reaches 63% in that time and leaks the peak, so use ~5 time constants inside
    // the window (settle ~99% within look_ samples) — no overshoot.
    const double look = look_ > 1 ? static_cast<double>(look_) : 1.0;
    atkCoef_ = static_cast<float>(1.0 - std::exp(-8.0 / look));
    const double relSamples = releaseMs_ * 0.001 * sr;
    relCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (relSamples > 1.0 ? relSamples : 1.0)));
    holdSamples_ = static_cast<std::size_t>(holdMs_ * 0.001 * sr + 0.5f);
}

void Limiter::process(core::AudioBlock& block) noexcept
{
    if (block.isEmpty() || delay_[0].empty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels() < numChannels_ ? block.numChannels() : numChannels_;
    const std::size_t ringLen  = delay_[0].size();

    float* chL = block.channel(0);
    float* chR = channels > 1 ? block.channel(1) : nullptr;

    for (std::size_t n = 0; n < frames; ++n)
    {
        const float xl = chL[n];
        const float xr = chR ? chR[n] : xl;

        // Read the look-ahead-delayed samples, then write the current input.
        const std::size_t readPos = (writePos_ + ringLen - look_) % ringLen;
        const float dl = delay_[0][readPos];
        const float dr = chR ? delay_[1][readPos] : dl;
        delay_[0][writePos_] = xl;
        if (chR) delay_[1][writePos_] = xr;
        writePos_ = (writePos_ + 1) % ringLen;

        // Stereo-linked peak sidechain on the UNDELAYED input (the future peak).
        const float absL = xl < 0.0f ? -xl : xl;
        const float absR = xr < 0.0f ? -xr : xr;
        const float key  = absL > absR ? absL : absR;

        // Target gain that would bring this peak exactly to the ceiling.
        const float target = key > ceilingLin_ ? ceilingLin_ / key : 1.0f;

        // Attack / HOLD / release. Any sample needing more reduction snaps the gain
        // down (attack) and (re)arms the hold. While the hold counts down the gain is
        // frozen — so a sustained Tutti, whose peaks keep re-arming the hold, holds a
        // steady gain instead of breathing up and down between peaks (the pumping the
        // old 90 ms release produced). Only after the hold expires does it release.
        if (target < gEnv_)
        {
            gEnv_       += atkCoef_ * (target - gEnv_);
            holdCounter_ = holdSamples_;
        }
        else if (holdCounter_ > 0)
        {
            --holdCounter_;                       // frozen: no recovery yet
        }
        else
        {
            gEnv_ += relCoef_ * (target - gEnv_); // slow, click-free recovery
        }

        chL[n] = dl * gEnv_;
        if (chR) chR[n] = dr * gEnv_;
    }
}

void Limiter::reset() noexcept
{
    for (auto& d : delay_)
        std::fill(d.begin(), d.end(), 0.0f);
    writePos_    = 0;
    gEnv_        = 1.0f;
    holdCounter_ = 0;
}

float Limiter::gainReductionDb() const noexcept
{
    return gEnv_ < 1.0f ? -gainToDb(gEnv_) : 0.0f;
}

} // namespace caecilia::dsp
