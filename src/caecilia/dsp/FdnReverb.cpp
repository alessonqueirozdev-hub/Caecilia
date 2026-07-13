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

// Early-reflection pattern: nine discrete taps spread over ~13..80 ms with a
// decaying, left/right ping-ponging gain. Chosen to evoke a large stone nave
// (sparse, asymmetric) rather than a symmetric "box". Scaled to the sample rate.
struct ErTap { double ms; float l; float r; };
constexpr std::array<ErTap, 9> kErPattern = {{
    { 13.5f, 0.92f, 0.55f },
    { 19.1f, 0.55f, 0.86f },
    { 26.3f, 0.80f, 0.48f },
    { 33.7f, 0.46f, 0.74f },
    { 42.1f, 0.66f, 0.42f },
    { 50.9f, 0.40f, 0.62f },
    { 61.3f, 0.52f, 0.34f },
    { 70.7f, 0.32f, 0.50f },
    { 79.9f, 0.42f, 0.28f },
}};

// Longest ER tap we allocate room for.
constexpr double kMaxErMs = 90.0;

// Dattorro input-diffusor delays (his 1997 figures, given at 29761 Hz, expressed
// here in milliseconds so they scale to any sample rate) and coefficients. The
// mutually-incommensurate lengths and moderate coefficients smear the input into
// a dense, uncoloured wash before it enters the tank.
struct DiffAp { double ms; float g; };
constexpr std::array<DiffAp, 4> kDiffusors = {{
    {  4.7713f, 0.750f },   // 142 / 29761
    {  3.5952f, 0.750f },   // 107 / 29761
    { 12.7348f, 0.625f },   // 379 / 29761
    {  9.3075f, 0.625f },   // 277 / 29761
}};

// Prime-length delay lines share no common factors, so their modal frequencies
// never coincide — this kills the periodic "metallic flutter" a plain FDN rings
// with, which is worst under exactly the sustained chords an organ plays.
[[nodiscard]] bool isPrime(std::size_t n) noexcept
{
    if (n < 2) return false;
    if (n < 4) return true;
    if ((n & 1U) == 0U) return false;
    for (std::size_t d = 3; d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}
// The NEAREST prime to n (search outward), so the line keeps its intended length
// and the tank's total delay — hence its sustained-input energy — is unchanged;
// only the modal periods are made mutually prime. (Forcing strictly-ascending
// primes lengthened the lines and made a held chord's tail build up ~6 dB hotter.)
[[nodiscard]] std::size_t nearestPrime(std::size_t n) noexcept
{
    if (n < 2) return 2;
    if (isPrime(n)) return n;
    for (std::size_t d = 1; ; ++d)
    {
        if (isPrime(n + d)) return n + d;
        if (n > d && isPrime(n - d)) return n - d;
    }
}
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
        const auto raw = static_cast<std::size_t>(kBaseDelaysMs[i] * 0.001 * sampleRate_ + 0.5);
        // Round to the NEAREST prime (the base delays are ~4 ms apart, so the nearest
        // primes are naturally distinct) — mutually-prime periods kill the flutter
        // WITHOUT lengthening the tank.
        std::size_t len = nearestPrime(std::max<std::size_t>(raw, 2));
        for (std::size_t j = 0; j < i; ++j)          // guarantee distinctness
            if (lengths_[j] == len) { len = nearestPrime(len + 2); j = std::size_t(-1); }
        lengths_[i]    = len;
        lines_[i].assign(lengths_[i], 0.0f);
        positions_[i] = 0;

        dampers_[i].prepare(sampleRate_);

        // Low-band extractor for the bass-bloom shelf (fixed crossover).
        bloomLp_[i].prepare(sampleRate_);
        bloomLp_[i].setLowpass(kBloomHz);
        bloomBoost_[i] = 1.0f;

        // Alternate the input injection sign to decorrelate the initial build-up.
        inject_[i] = (i & 1U) ? -1.0f : 1.0f;
    }

    // Dattorro input diffusors: allocate one delay line each, sized to the
    // sample-rate-scaled tap length.
    for (std::size_t k = 0; k < kDiff; ++k)
    {
        auto len = static_cast<std::size_t>(kDiffusors[k].ms * 0.001 * sampleRate_);
        diffLen_[k]  = std::max<std::size_t>(len, 2);
        diff_[k].assign(diffLen_[k], 0.0f);
        diffPos_[k]  = 0;
        diffCoef_[k] = kDiffusors[k].g;
    }

    const auto preMax =
        static_cast<std::size_t>(kMaxPreDelayMs * 0.001 * sampleRate_) + 1;
    preDelay_.assign(std::max<std::size_t>(preMax, 2), 0.0f);
    preDelayPos_ = 0;

    const auto erMax =
        static_cast<std::size_t>(kMaxErMs * 0.001 * sampleRate_) + 2;
    erBuffer_.assign(std::max<std::size_t>(erMax, 2), 0.0f);
    erPos_ = 0;
    updateEarlyReflections();

    updateModulation();
    setParams(params_);
    reset();
}

void FdnReverb::updateEarlyReflections() noexcept
{
    for (std::size_t k = 0; k < kErTaps; ++k)
    {
        auto d = static_cast<std::size_t>(kErPattern[k].ms * 0.001 * sampleRate_);
        if (d < 1) d = 1;
        if (!erBuffer_.empty() && d > erBuffer_.size() - 1)
            d = erBuffer_.size() - 1;
        erDelay_[k]  = d;
        erGainL_[k]  = kErPattern[k].l;
        erGainR_[k]  = kErPattern[k].r;
    }
}

void FdnReverb::updateModulation() noexcept
{
    // Slow (< ~1 Hz) mutually-detuned LFOs, one per line, so no two lines sweep
    // in lock-step. Depth scales with the sample rate so the peak excursion is a
    // constant fraction of a millisecond (about +/- 0.03 ms) at any rate — light
    // enough to be inaudible as pitch, deep enough to smear the tail's modes.
    // Mutually-INCOMMENSURATE rates (golden-ratio spread over 0.13..0.47 Hz) so no
    // two lines sweep in lock-step — the old near-linear 0.6*(1+0.05i) ramp let the
    // lines beat together and metallise the tail.
    for (std::size_t i = 0; i < kLines; ++i)
    {
        const double frac   = std::fmod(static_cast<double>(i) * 0.6180339887498949, 1.0);
        const double rateHz = 0.13 + 0.34 * frac;
        modInc_[i] = static_cast<float>(rateHz / sampleRate_);
    }
    modDepth_ = static_cast<float>(2.2 * sampleRate_ / 44100.0);
}

core::ReverbParams FdnReverb::presetParams(ReverbPreset preset) noexcept
{
    core::ReverbParams p{};
    switch (preset)
    {
        case ReverbPreset::Room:
            p.mix = 0.15f; p.decaySec = 0.8f;  p.preDelayMs = 6.0f;  p.dampingHz = 4500.0f; p.widthNorm = 0.6f; p.bassBloom = 1.05f;
            break;
        case ReverbPreset::Chamber:
            p.mix = 0.22f; p.decaySec = 1.6f;  p.preDelayMs = 10.0f; p.dampingHz = 5500.0f; p.widthNorm = 0.8f; p.bassBloom = 1.18f;
            break;
        case ReverbPreset::Hall:
            p.mix = 0.28f; p.decaySec = 2.6f;  p.preDelayMs = 18.0f; p.dampingHz = 6500.0f; p.widthNorm = 1.0f; p.bassBloom = 1.35f;
            break;
        case ReverbPreset::Cathedral:
            p.mix = 0.32f; p.decaySec = 5.2f;  p.preDelayMs = 38.0f; p.dampingHz = 6800.0f; p.widthNorm = 1.0f; p.bassBloom = 1.6f;
            break;
        case ReverbPreset::Plate:
            p.mix = 0.30f; p.decaySec = 2.0f;  p.preDelayMs = 2.0f;  p.dampingHz = 8000.0f; p.widthNorm = 0.9f; p.bassBloom = 1.0f;
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
    // Bloom only ever LENGTHENS the bass relative to the mids; a value below 1 (which
    // would shorten it and, more importantly, break the "attenuate-only" stability
    // argument) is clamped away. Capped so the low-band tail stays musical.
    params_.bassBloom  = clamp(params_.bassBloom, 1.0f, 3.0f);

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
    const double denomMid = std::max(params_.decaySec * sampleRate_, 1.0);
    // Low band uses a LONGER RT60 (decay * bloom), so its per-line gain is closer to
    // unity — the bass rings on after the mids have died. Both gains are < 1, and we
    // lift the loop from the mid gain up to the low gain, never above it, so the
    // network can never self-oscillate.
    const double denomLow = std::max(params_.decaySec * params_.bassBloom * sampleRate_, 1.0);
    for (std::size_t i = 0; i < kLines; ++i)
    {
        const double gMid = std::min(std::pow(10.0, -3.0 * static_cast<double>(lengths_[i]) / denomMid), 0.9999);
        const double gLow = std::min(std::pow(10.0, -3.0 * static_cast<double>(lengths_[i]) / denomLow), 0.9999);
        feedback_[i]      = static_cast<float>(gMid);
        // Low-band lift, applied to the mid feedback: gMid * boost == gLow (< 1).
        bloomBoost_[i]    = static_cast<float>(gMid > 0.0 ? gLow / gMid : 1.0);
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

        // Dattorro input diffusion: run the pre-delayed input through four cascaded
        // Schroeder all-pass sections so it enters the tank as a smooth wash. Each
        // section: w[n] = x + g*w[n-D];  y = w[n-D] - g*w[n]  (unity-magnitude).
        float diffused = tankIn;
        for (std::size_t k = 0; k < kDiff; ++k)
        {
            const std::size_t p = diffPos_[k];
            const float w  = diff_[k][p];                       // w[n-D]
            const float in = diffused + diffCoef_[k] * w;       // w[n]
            diff_[k][p]    = flushDenormal(in);
            diffPos_[k]    = (p + 1) % diffLen_[k];
            diffused       = w - diffCoef_[k] * in;             // all-pass output
        }

        // Early reflections: sparse, stereo-panned taps of the input read before
        // the diffuse tail. These give the "large stone room" spatial cue.
        float erL = 0.0f;
        float erR = 0.0f;
        const std::size_t erSize = erBuffer_.size();
        for (std::size_t k = 0; k < kErTaps; ++k)
        {
            const std::size_t rd = (erPos_ + erSize - erDelay_[k]) % erSize;
            const float s = erBuffer_[rd];
            erL += s * erGainL_[k];
            erR += s * erGainR_[k];
        }
        erBuffer_[erPos_] = mono;
        erPos_            = (erPos_ + 1) % erSize;
        const float erMono = 0.5f * (erL + erR);

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
            // Bipolar around a fixed 1-depth centre (mean delay constant), so the
            // modulation shimmers the tail without one-sidedly detuning held tones.
            const float offset = modDepth_ * (1.0f + lfo);               // 0..2*depth, mean = depth
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

            // High-frequency damping (mids/highs die first), then the bass-bloom
            // low-shelf: lift only the sub-kBloomHz band from the mid gain up to the
            // low gain. At DC the loop gain is gMid*bloomBoost == gLow < 1; above the
            // crossover the low-pass output vanishes and it collapses to gMid. So the
            // bass rings on, the mids are the reference, and nothing can blow up.
            const float damped  = dampers_[i].process(delayed);
            const float lowBand = bloomLp_[i].process(damped);
            const float shelved = damped + (bloomBoost_[i] - 1.0f) * lowBand;
            o[i]                = shelved * feedback_[i];
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

        // Inject the (pre-delayed) input PLUS the early-reflection energy and
        // write back into each line, so the diffuse tail grows out of the early
        // pattern rather than from a bare impulse.
        for (std::size_t i = 0; i < kLines; ++i)
        {
            const float v         = inject_[i] * (diffused + erSendToTank_ * erMono) + mixed[i];
            lines_[i][positions_[i]] = flushDenormal(v);
            positions_[i]         = (positions_[i] + 1) % lengths_[i];
        }

        // Wet output = early reflections (placed, stereo) + diffuse tail.
        constexpr float erOutGain = 0.30f;
        left[n] = dry * inL + wet * (wetL + erOutGain * erL);
        if (right)
            right[n] = dry * inR + wet * (wetR + erOutGain * erR);
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
        bloomLp_[i].reset();
        // Spread the LFO phases evenly so the tail is deterministic and the
        // per-line modulation starts maximally decorrelated.
        modPhase_[i] = static_cast<float>(i) / static_cast<float>(kLines);
    }
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayPos_ = 0;
    std::fill(erBuffer_.begin(), erBuffer_.end(), 0.0f);
    erPos_ = 0;
    for (std::size_t k = 0; k < kDiff; ++k)
    {
        std::fill(diff_[k].begin(), diff_[k].end(), 0.0f);
        diffPos_[k] = 0;
    }
}

} // namespace caecilia::dsp
