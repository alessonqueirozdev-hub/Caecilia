/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/MasterEq.h"
#include "caecilia/dsp/DspMath.h"

#include <cmath>

namespace caecilia::dsp
{

void MasterEq::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t /*numChannels*/)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    // Glide gain changes over ~30 ms so slider moves are click-free at block rate.
    const double blk = maxBlockFrames > 0 ? static_cast<double>(maxBlockFrames) : 256.0;
    smoothA_ = static_cast<float>(1.0 - std::exp(-blk / (0.030 * sr_)));

    setOrganDefaults();
    // Snap the smoothed state to the targets so the first block is already voiced.
    for (auto& b : bands_)
        b.curDb = b.targetDb;
    reset();
}

void MasterEq::setOrganDefaults() noexcept
{
    // Gentle, flatters every registration, barely touches headroom.
    bands_[Warmth]   = { 110.0f,  0.70f,  2.0f, bands_[Warmth].curDb };
    bands_[Boxiness] = { 315.0f,  1.10f, -2.5f, bands_[Boxiness].curDb };
    bands_[Body]     = { 800.0f,  0.80f,  0.0f, bands_[Body].curDb };
    bands_[Presence] = { 3200.0f, 0.90f,  1.5f, bands_[Presence].curDb };
    bands_[Air]      = { 9000.0f, 0.70f,  2.0f, bands_[Air].curDb };
}

void MasterEq::setBand(std::size_t index, float freqHz, float q, float gainDb) noexcept
{
    if (index >= kBands) return;
    bands_[index].freq     = clamp(freqHz, 20.0f, 20000.0f);
    bands_[index].q        = clamp(q, 0.1f, 10.0f);
    bands_[index].targetDb = clamp(gainDb, -18.0f, 18.0f);
}

void MasterEq::setBandGain(std::size_t index, float gainDb) noexcept
{
    if (index >= kBands) return;
    bands_[index].targetDb = clamp(gainDb, -18.0f, 18.0f);
}

BiquadCoeffs MasterEq::design(std::size_t band, float gainDb) const noexcept
{
    const BandState& b = bands_[band];
    switch (band)
    {
        case Warmth: return BiquadCoeffs::lowShelf(sr_, b.freq, b.q, gainDb);
        case Air:    return BiquadCoeffs::highShelf(sr_, b.freq, b.q, gainDb);
        default:     return BiquadCoeffs::peaking(sr_, b.freq, b.q, gainDb);
    }
}

void MasterEq::process(core::AudioBlock& block) noexcept
{
    if (!enabled_ || block.isEmpty())
        return;

    // Glide each band's gain toward its target and redesign coefficients once per
    // block (cheap: five designers total, shared between the L/R sections).
    for (std::size_t i = 0; i < kBands; ++i)
    {
        bands_[i].curDb += smoothA_ * (bands_[i].targetDb - bands_[i].curDb);
        const BiquadCoeffs c = design(i, bands_[i].curDb);
        filtL_[i].setCoeffs(c);
        filtR_[i].setCoeffs(c);
    }

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();
    float* L = block.channel(0);
    float* R = channels > 1 ? block.channel(1) : nullptr;

    for (std::size_t i = 0; i < kBands; ++i)
    {
        if (L) filtL_[i].processBlock(L, frames);
        if (R) filtR_[i].processBlock(R, frames);
    }

    // Mirror onto any further channels (rare).
    for (std::size_t c = 2; c < channels; ++c)
    {
        float* dst = block.channel(c);
        if (dst && L)
            for (std::size_t n = 0; n < frames; ++n) dst[n] = L[n];
    }
}

void MasterEq::reset() noexcept
{
    for (auto& f : filtL_) f.reset();
    for (auto& f : filtR_) f.reset();
}

} // namespace caecilia::dsp
