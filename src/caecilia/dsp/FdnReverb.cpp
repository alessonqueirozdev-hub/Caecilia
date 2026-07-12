/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/FdnReverb.h"

#include "caecilia/dsp/DspMath.h"
#include "caecilia/dsp/Kernels.h"

#include <algorithm>
#include <cmath>

namespace caecilia::dsp
{

namespace
{
// Sixteen mutually-incommensurate base delay lengths in milliseconds. Spreading
// them over ~19..86 ms and keeping the ratios irrational-ish maximises the modal
// density of the tail and avoids flutter echoes. Scaled to the sample rate in
// prepare().
constexpr std::array<double, FdnReverb::kLines> kBaseDelaysMs = {
    19.3, 23.7, 28.1, 31.9, 36.7, 41.3, 45.9, 50.3,
    54.7, 59.1, 63.7, 68.3, 72.9, 77.1, 81.7, 86.3};

// Longest pre-delay we allocate room for.
constexpr double kMaxPreDelayMs = 120.0;
} // namespace

void FdnReverb::prepare(core::SampleRate sampleRate,
                        std::size_t      maxBlockFrames,
                        std::size_t      numChannels)
{
    sampleRate_     = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockFrames_ = maxBlockFrames;
    numChannels_    = numChannels == 0 ? 1 : numChannels;

    for (std::size_t i = 0; i < kLines; ++i)
    {
        const auto len = static_cast<std::size_t>(kBaseDelaysMs[i] * 0.001 * sampleRate_);
        lengths_[i]    = std::max<std::size_t>(len, 2);
        lines_[i].assign(lengths_[i], 0.0f);
        positions_[i] = 0;

        dampers_[i].prepare(sampleRate_);

        // Alternate the input injection sign to decorrelate the initial build-up.
        inject_[i] = (i & 1U) ? -1.0f : 1.0f;
    }

    const auto preMax =
        static_cast<std::size_t>(kMaxPreDelayMs * 0.001 * sampleRate_) + 1;
    preDelay_.assign(std::max<std::size_t>(preMax, 2), 0.0f);
    preDelayPos_ = 0;

    updateModulation();
    setParams(params_);
    reset();
}

void FdnReverb::updateModulation() noexcept
{
    // Slow (< ~1 Hz) mutually-detuned LFOs, one per line, so no two lines sweep
    // in lock-step. Depth scales with the sample rate so the peak excursion is a
    // constant fraction of a millisecond (about +/- 0.03 ms) at any rate — light
    // enough to be inaudible as pitch, deep enough to smear the tail's modes.
    constexpr double kModBaseHz = 0.6;
    for (std::size_t i = 0; i < kLines; ++i)
    {
        const double rateHz = kModBaseHz * (1.0 + 0.05 * static_cast<double>(i));
        modInc_[i] = static_cast<float>(rateHz / sampleRate_);
    }
    modDepth_ = static_cast<float>(1.5 * sampleRate_ / 44100.0);
}

core::ReverbParams FdnReverb::presetParams(ReverbPreset preset) noexcept
{
    core::ReverbParams p{};
    switch (preset)
    {
        case ReverbPreset::Room:
            p.mix = 0.15f; p.decaySec = 0.8f;  p.preDelayMs = 6.0f;  p.dampingHz = 4500.0f; p.widthNorm = 0.6f;
            break;
        case ReverbPreset::Chamber:
            p.mix = 0.22f; p.decaySec = 1.6f;  p.preDelayMs = 10.0f; p.dampingHz = 5500.0f; p.widthNorm = 0.8f;
            break;
        case ReverbPreset::Hall:
            p.mix = 0.28f; p.decaySec = 2.6f;  p.preDelayMs = 18.0f; p.dampingHz = 6500.0f; p.widthNorm = 1.0f;
            break;
        case ReverbPreset::Cathedral:
            p.mix = 0.34f; p.decaySec = 4.8f;  p.preDelayMs = 32.0f; p.dampingHz = 7500.0f; p.widthNorm = 1.0f;
            break;
        case ReverbPreset::Plate:
            p.mix = 0.30f; p.decaySec = 2.0f;  p.preDelayMs = 2.0f;  p.dampingHz = 8000.0f; p.widthNorm = 0.9f;
            break;
    }
    return p;
}

void FdnReverb::setParams(const core::ReverbParams& params) noexcept
{
    params_ = params;
    params_.mix        = clamp(params_.mix, 0.0f, 1.0f);
    params_.decaySec   = std::max(params_.decaySec, 0.05f);
    params_.preDelayMs = std::max(params_.preDelayMs, 0.0f);
    params_.dampingHz  = clamp(params_.dampingHz, 200.0f, static_cast<float>(sampleRate_ * 0.49));
    params_.widthNorm  = clamp(params_.widthNorm, 0.0f, 1.0f);

    if (!preDelay_.empty())
    {
        const auto req = static_cast<std::size_t>(params_.preDelayMs * 0.001 * sampleRate_);
        preDelayLen_   = clamp<std::size_t>(req == 0 ? 1 : req, 1, preDelay_.size() - 1);
    }

    updateDecay();
    updateDamping();
    updateWidth();
}

void FdnReverb::updateDecay() noexcept
{
    // Per-line feedback gain that yields the target RT60: energy must drop by
    // 60 dB (a linear factor of 0.001) over decaySec seconds, so a line of L
    // samples uses g = 0.001^(L / (decaySec * fs)) = 10^(-3 L / (decaySec fs)).
    const double denom = std::max(params_.decaySec * sampleRate_, 1.0);
    for (std::size_t i = 0; i < kLines; ++i)
    {
        const double g = std::pow(10.0, -3.0 * static_cast<double>(lengths_[i]) / denom);
        feedback_[i]   = static_cast<float>(std::min(g, 0.9999));
    }
}

void FdnReverb::updateDamping() noexcept
{
    for (auto& d : dampers_)
        d.setLowpass(params_.dampingHz);
}

void FdnReverb::updateWidth() noexcept
{
    // At full width, even lines feed L and odd lines feed R (decorrelated);
    // reducing width bleeds each line into the opposite channel toward mono.
    const float cross = 1.0f - params_.widthNorm;
    const float norm  = 1.0f / std::sqrt(static_cast<float>(kLines) * 0.5f);
    for (std::size_t i = 0; i < kLines; ++i)
    {
        const bool even = (i & 1U) == 0U;
        tapLeft_[i]     = (even ? 1.0f : cross) * norm;
        tapRight_[i]    = (even ? cross : 1.0f) * norm;
    }
}

void FdnReverb::process(core::AudioBlock& block) noexcept
{
    if (block.isEmpty() || lines_[0].empty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = std::min(block.numChannels(), numChannels_);
    float*            left     = block.channel(0);
    float*            right    = channels > 1 ? block.channel(1) : nullptr;

    const float wet = params_.mix;
    const float dry = 1.0f - params_.mix;

    for (std::size_t n = 0; n < frames; ++n)
    {
        const float inL = left[n];
        const float inR = right ? right[n] : inL;
        const float mono = 0.5f * (inL + inR);

        // Pre-delay the tank input (dry path stays un-delayed).
        const std::size_t preRead =
            (preDelayPos_ + preDelay_.size() - preDelayLen_) % preDelay_.size();
        const float tankIn      = preDelay_[preRead];
        preDelay_[preDelayPos_] = mono;
        preDelayPos_            = (preDelayPos_ + 1) % preDelay_.size();

        // Read, damp and apply feedback gain to every line. Each read tap is
        // jittered forward by a light, slow LFO and linearly interpolated, so the
        // effective delay wanders within [len - modDepth_, len] samples.
        float o[kLines];
        for (std::size_t i = 0; i < kLines; ++i)
        {
            modPhase_[i] += modInc_[i];
            if (modPhase_[i] >= 1.0f)
                modPhase_[i] -= 1.0f;

            const float lfo    = std::sin(kTwoPiF * modPhase_[i]);       // -1..+1
            const float offset = modDepth_ * (0.5f + 0.5f * lfo);        // 0..depth
            const std::size_t len = lengths_[i];

            const float basePos = static_cast<float>(positions_[i]) + offset;
            std::size_t i0       = static_cast<std::size_t>(basePos);
            const float frac     = basePos - static_cast<float>(i0);
            if (i0 >= len)
                i0 -= len;
            std::size_t i1 = i0 + 1;
            if (i1 >= len)
                i1 -= len;

            const float delayed = lines_[i][i0] + (lines_[i][i1] - lines_[i][i0]) * frac;
            o[i]                = dampers_[i].process(delayed) * feedback_[i];
        }

        // Collect the stereo tail BEFORE mixing (taps read the line outputs).
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (std::size_t i = 0; i < kLines; ++i)
        {
            wetL += o[i] * tapLeft_[i];
            wetR += o[i] * tapRight_[i];
        }

        // Lossless recombination through the orthonormal Hadamard matrix.
        float mixed[kLines];
        for (std::size_t i = 0; i < kLines; ++i)
            mixed[i] = o[i];
        kernels::hadamard16(mixed);

        // Inject the (pre-delayed) input and write back into each line.
        for (std::size_t i = 0; i < kLines; ++i)
        {
            const float v         = inject_[i] * tankIn + mixed[i];
            lines_[i][positions_[i]] = flushDenormal(v);
            positions_[i]         = (positions_[i] + 1) % lengths_[i];
        }

        left[n] = dry * inL + wet * wetL;
        if (right)
            right[n] = dry * inR + wet * wetR;
    }

    // Mirror the tail onto any further channels (rare; keeps buses coherent).
    for (std::size_t c = 2; c < channels; ++c)
    {
        float* dst = block.channel(c);
        for (std::size_t n = 0; n < frames; ++n)
            dst[n] = left[n];
    }
}

void FdnReverb::reset() noexcept
{
    for (std::size_t i = 0; i < kLines; ++i)
    {
        std::fill(lines_[i].begin(), lines_[i].end(), 0.0f);
        positions_[i] = 0;
        dampers_[i].reset();
        // Spread the LFO phases evenly so the tail is deterministic and the
        // per-line modulation starts maximally decorrelated.
        modPhase_[i] = static_cast<float>(i) / static_cast<float>(kLines);
    }
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayPos_ = 0;
}

} // namespace caecilia::dsp
