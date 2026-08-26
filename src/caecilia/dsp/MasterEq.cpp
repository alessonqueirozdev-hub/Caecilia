// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/MasterEq.h"
#include "caecilia/dsp/DspMath.h"

#include <algorithm>
#include <cmath>

namespace caecilia::dsp
{

void MasterEq::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t /*numChannels*/)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    (void) maxBlockFrames; // the glide is derived per block, from the real length

    // The SHAPES land unless somebody has chosen their own. Skipping them
    // entirely left every section at the 1 kHz fallback whenever a gain had been
    // restored before prepare() ran; applying them unconditionally reverted a
    // custom frequency on every sample-rate change. Neither failure says anything.
    if (!shaped_)
    {
        setOrganBandShapes();
        shaped_ = true;
    }

    // The GAINS are the user's, so the factory voicing only lands the first time.
    // prepare() runs again on every sample-rate and block-size change, and
    // re-applying the defaults there wiped whatever had been dialled in.
    if (!voiced_)
    {
        setOrganDefaults();
        voiced_ = true;
    }

    // Snap the smoothed state to where the glide would settle, so the first block
    // is already voiced (or already transparent) rather than fading in.
    for (auto& b : bands_)
        b.curDb = enabled_ ? b.targetDb : 0.0f;
    bypassSettled_ = false;
    reset();
}

MasterEq::Params MasterEq::organDefaults() noexcept
{
    // Where the five sections sit, how wide they are, and how hard they push.
    // Warmth is a low shelf and Air a high shelf (see design()); the three between
    // are peaking. The gains are gentle by design: this flatters every
    // registration and barely touches headroom.
    Params p;
    p.bands[Warmth]   = { 110.0f,  0.70f,  2.0f };
    p.bands[Boxiness] = { 315.0f,  1.10f, -2.5f };
    p.bands[Body]     = { 800.0f,  0.80f,  0.0f };
    p.bands[Presence] = { 3200.0f, 0.90f,  1.5f };
    p.bands[Air]      = { 9000.0f, 0.70f,  2.0f };
    p.enabled = true;
    return p;
}

void MasterEq::setOrganBandShapes() noexcept
{
    const Params d = organDefaults();
    for (std::size_t i = 0; i < kBands; ++i)
    {
        bands_[i].freq = d.bands[i].freqHz;
        bands_[i].q    = d.bands[i].q;
    }
}

void MasterEq::setOrganDefaults() noexcept
{
    const Params d = organDefaults();
    for (std::size_t i = 0; i < kBands; ++i)
        bands_[i].targetDb = d.bands[i].gainDb;
}

void MasterEq::setParams(const Params& params) noexcept
{
    for (std::size_t i = 0; i < kBands; ++i)
        setBand(i, params.bands[i].freqHz, params.bands[i].q, params.bands[i].gainDb);
    setEnabled(params.enabled);
}

MasterEq::Params MasterEq::params() const noexcept
{
    Params p;
    for (std::size_t i = 0; i < kBands; ++i)
        p.bands[i] = { bands_[i].freq, bands_[i].q, bands_[i].targetDb };
    p.enabled = enabled_;
    return p;
}

void MasterEq::setBand(std::size_t index, float freqHz, float q, float gainDb) noexcept
{
    if (index >= kBands) return;
    bands_[index].freq     = clamp(freqHz, 20.0f, 20000.0f);
    bands_[index].q        = clamp(q, 0.1f, 10.0f);
    bands_[index].targetDb = clamp(gainDb, -18.0f, 18.0f);
    // This chose both a shape and a gain, so neither may be reverted by the next
    // prepare(). setBandGain marks only the gain, because it only set one.
    shaped_ = true;
    voiced_ = true;
}

void MasterEq::setBandGain(std::size_t index, float gainDb) noexcept
{
    if (index >= kBands) return;
    bands_[index].targetDb = clamp(gainDb, -18.0f, 18.0f);
    voiced_ = true;
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
    if (block.isEmpty())
        return;

    // Bypass is a glide, not a branch. Returning here the moment the switch flips
    // dropped the whole voicing in one sample — a click going out, and another
    // coming back in as five biquads resumed from frozen state. At 0 dB every
    // section is a mathematical pass-through, so "off" is every band gliding to
    // unity; once that has landed the stage skips itself and is free again.
    if (bypassSettled_)
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();
    float* L = block.channel(0);
    float* R = channels > 1 ? block.channel(1) : nullptr;

    // Is any band actually travelling? Only then is the sub-blocking below worth
    // its extra coefficient designs.
    const auto targetOf = [this](std::size_t i) noexcept
    {
        return enabled_ ? bands_[i].targetDb : 0.0f;
    };
    bool gliding = false;
    for (std::size_t i = 0; i < kBands; ++i)
        gliding = gliding || std::abs(bands_[i].curDb - targetOf(i)) > kGlideEpsilonDb;

    // Redesign every kGlideChunk frames while gliding, once per block otherwise.
    //
    // A whole 512-sample block per redesign let the first step of a 30 ms glide
    // move a shelf three quarters of a dB at once, which on a sustained chord is
    // a small but real tick — measured at 2.5x the largest step the signal itself
    // could take. Sixty-four frames cuts that by eight, and the exponential
    // composes exactly, so the glide still takes exactly 30 ms.
    const std::size_t chunk = gliding ? (frames < kGlideChunk ? frames : kGlideChunk)
                                      : frames;

    for (std::size_t off = 0; off < frames; off += chunk)
    {
        const std::size_t n = (frames - off) < chunk ? (frames - off) : chunk;

        // The coefficient comes from THIS chunk's length, not from the maximum the
        // host declared. Hosts routinely send shorter blocks than the maximum, and
        // a fixed per-block coefficient made the 30 ms glide run proportionally
        // faster whenever they did -- one more way the instrument behaved
        // differently in different hosts.
        const float smoothA = static_cast<float>(
            1.0 - std::exp(-static_cast<double>(n) / (0.030 * sr_)));

        for (std::size_t i = 0; i < kBands; ++i)
        {
            bands_[i].curDb += smoothA * (targetOf(i) - bands_[i].curDb);
            const BiquadCoeffs c = design(i, bands_[i].curDb);
            filtL_[i].setCoeffs(c);
            filtR_[i].setCoeffs(c);
        }

        for (std::size_t i = 0; i < kBands; ++i)
        {
            if (L) filtL_[i].processBlock(L + off, n);
            if (R) filtR_[i].processBlock(R + off, n);
        }
    }

    bool settled = true;
    for (std::size_t i = 0; i < kBands; ++i)
        settled = settled && std::abs(bands_[i].curDb - targetOf(i)) <= kGlideEpsilonDb;

    // Mirror onto any further channels (rare).
    for (std::size_t c = 2; c < channels; ++c)
    {
        float* dst = block.channel(c);
        if (dst && L)
            for (std::size_t n = 0; n < frames; ++n) dst[n] = L[n];
    }

    // The fade to unity has finished: stop paying for ten transposed biquads that
    // are now provably identity. Clearing their memory first is what makes the
    // skip exact — a unity section still carries two samples of stale state.
    if (!enabled_ && settled)
    {
        for (auto& b : bands_)
            b.curDb = 0.0f;
        reset();
        bypassSettled_ = true;
    }
}

void MasterEq::reset() noexcept
{
    for (auto& f : filtL_) f.reset();
    for (auto& f : filtR_) f.reset();
}

} // namespace caecilia::dsp
