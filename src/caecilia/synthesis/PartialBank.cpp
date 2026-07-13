/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/synthesis/PartialBank.h"

#include <cmath>

namespace caecilia::synth
{

namespace
{
    constexpr float  kTwoPi        = 6.28318530717958647692f;
    constexpr double kMinSampleRate = 1.0;
    constexpr float  kLn2Over1200  = 0.000577622650f; ///< ln(2)/1200: cents -> ratio (linear approx).

    /// Convert decibels to a linear gain.
    [[nodiscard]] inline float dbToLinear(float db) noexcept
    {
        return std::pow(10.0f, db * 0.05f);
    }

    /// Cheap ratio for a small cents offset: 2^(c/1200) ~= 1 + c*ln2/1200.
    /// Accurate to <1e-4 for |c| < ~30 cents, which covers all drift/glide here,
    /// and avoids a per-partial pow() on the audio thread.
    [[nodiscard]] inline float centsToRatioApprox(float cents) noexcept
    {
        return 1.0f + cents * kLn2Over1200;
    }

    /// Smoothstep (raised-cosine-like) 0->1 easing; kills the hard linear-ramp
    /// "blip" that made the attack sound synthetic.
    [[nodiscard]] inline float smoothstep01(float x) noexcept
    {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;
        return x * x * (3.0f - 2.0f * x);
    }

    /// One step of a cheap xorshift PRNG in [-1, 1). RT-safe, deterministic.
    [[nodiscard]] inline float nextBipolar(std::uint32_t& s) noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        // Map the top bits into [-1, 1).
        return static_cast<float>(static_cast<std::int32_t>(s)) * (1.0f / 2147483648.0f);
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
        p.curGain           = 0.0f;
        p.gainInc           = 0.0f;

        // Seed a decorrelated PRNG per partial so every partial drifts on its own
        // trajectory (correlated drift would just be vibrato, not a living chorus).
        p.rng = 0x9E3779B9u ^ (static_cast<std::uint32_t>(i) * 2654435761u);
        if (p.rng == 0u) p.rng = 0x2545F491u;
        p.driftCents = 0.0f;
        p.bloomSeconds = 0.010f; // filled per note in trigger() from the real pitch.

        // Nearest integer harmonic (>=1) drives the per-note treble tilt; per-note
        // level scale and formant gain are baked in trigger() from the real pitch.
        const int h = static_cast<int>(p.ratioToF0 + 0.5f);
        p.harmonicIndex  = h < 1 ? 1 : h;
        p.noteLevelScale = 1.0f;
        p.formantGain    = 1.0f;
        p.active         = true;
    }

    partialCount_    = count;
    fundamentalHz_   = model.fundamentalHz;
    steadyFormants_  = model.steadyFormants; // fixed-Hz formant (reeds); flues have peakCount==0
}

void PartialBank::trigger(core::PipeId /*pipe*/, core::Velocity velocity, double frequencyHz) noexcept
{
    fundamentalHz_   = frequencyHz;
    noteTimeSeconds_ = 0.0;

    // Velocity scales the initial drive; a gentle curve avoids a hard step. This
    // is kept separate from masterGain_ so an owner-set level trim survives.
    const float v = static_cast<float>(velocity) / 127.0f;
    velocityGain_ = 0.4f + 0.6f * v;

    // Per-partial attack bloom: a real pipe's fundamental speaks first and the
    // upper partials develop over the next tens of milliseconds. Derive each
    // partial's bloom time from its ABSOLUTE sounding pitch (log-spaced 120 Hz ->
    // 6 kHz), so a bass note blooms fast and treble partials speak progressively
    // later — this is what removes the hard, all-at-once "blip" attack.
    const double f0 = fundamentalHz_ > 1.0 ? fundamentalHz_ : 120.0;
    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        Partial& p = partials_[i];
        // Keep the scattered start phase from the seed (never re-align to
        // coherence — coherent phase is exactly the "electronic organ" click).
        p.phase = wrapPhase(p.phase);
        p.driftCents = 0.0f;
        p.curGain = 0.0f;   // ramp up from silence (click-free onset)
        p.gainInc = 0.0f;

        const double freq = f0 * static_cast<double>(p.ratioToF0);
        const float  oct  = static_cast<float>(std::log2((freq > 1.0 ? freq : 1.0) / 120.0) / 5.64f);
        const float  frac = oct < 0.0f ? 0.0f : (oct > 1.0f ? 1.0f : oct);
        p.bloomSeconds = 0.008f + (maxBloomSeconds_ - 0.008f) * frac;

        // Per-note treble tilt (Aeolus _h_lev): the higher the NOTE, the more its
        // upper harmonics are voiced down — real pipes shed brightness up the
        // compass, so a static composite (bright everywhere) sounds synthetic.
        // noteHeight 0 at ~C2 (65.4 Hz), 1 at ~5 octaves up.
        const float noteHeight = static_cast<float>(
            std::log2((f0 > 1.0 ? f0 : 65.4) / 65.4) / 5.0);
        const float nh = noteHeight < 0.0f ? 0.0f : (noteHeight > 1.0f ? 1.0f : noteHeight);
        const float tiltDb = -trebleTiltDb_ * nh * std::log2(static_cast<float>(p.harmonicIndex));
        p.noteLevelScale = dbToLinear(tiltDb);

        // Fixed-Hz formant boost (reeds): the boosted band sits at an absolute
        // frequency, so which harmonic it lifts changes note to note — the constant
        // brassy color of a Trompette. 0 dB baseline + peaks above (never silences
        // the inter-formant body).
        float fg = 1.0f;
        for (std::size_t k = 0; k < steadyFormants_.peakCount; ++k)
        {
            const FormantPeak& pk = steadyFormants_.peaks[k];
            if (pk.bandwidthHz <= 0.0f) continue;
            const float d = (static_cast<float>(freq) - pk.centerHz) / (0.5f * pk.bandwidthHz);
            fg += (dbToLinear(pk.gainDb) - 1.0f) / (1.0f + d * d);
        }
        p.formantGain = fg < 0.05f ? 0.05f : (fg > 3.0f ? 3.0f : fg);
    }

    // Arm the wind-attack chiff: a short filtered-noise breath, pitched by the
    // note (a one-pole low-pass a few multiples above the fundamental) with a fast
    // attack and a short exponential decay — the "speech" of a pipe starting.
    if (chiffAmount_ > 0.0f)
    {
        const double sr = sampleRate_ > kMinSampleRate ? sampleRate_ : 48000.0;
        const double fc = std::min(0.45 * sr, std::max(1500.0, f0 * 4.0));
        chiffLpCoef_  = static_cast<float>(std::min(1.0, 6.2831853 * fc / sr));
        chiffAtkSamp_ = static_cast<int>(0.003 * sr);
        if (chiffAtkSamp_ < 1) chiffAtkSamp_ = 1;
        const double decayTau = 0.045 * sr;                 // ~45 ms decay constant
        chiffDecayMul_ = static_cast<float>(std::exp(-1.0 / decayTau));
        chiffLenSamp_  = chiffAtkSamp_ + static_cast<int>(decayTau * 4.0);
        chiffSamp_ = 0;
        chiffEnv_  = 0.0f;
        chiffLp_   = 0.0f;
    }
    else
    {
        chiffLenSamp_ = 0;
    }

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

void PartialBank::setLiveliness(float instabilityCents,
                                float attackGlideCents,
                                float maxBloomSeconds,
                                float hfRolloffHz,
                                float trebleTiltDb) noexcept
{
    instabilityCents_ = instabilityCents < 0.0f ? 0.0f : instabilityCents;
    attackGlideCents_ = attackGlideCents;
    maxBloomSeconds_  = maxBloomSeconds < 0.008f ? 0.008f : maxBloomSeconds;
    hfRolloffHz_      = hfRolloffHz < 500.0f ? 500.0f : hfRolloffHz;
    trebleTiltDb_     = trebleTiltDb < 0.0f ? 0.0f : trebleTiltDb;
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

    // Attack pitch glide (bank-level): a labial pipe starts a touch flat and
    // "pulls" up to pitch as it speaks. Applied to every partial equally so the
    // whole tone glides in — a strong cue that this is a wind instrument, not a
    // switched-on oscillator.
    float glideRatio = 1.0f;
    if (noteTimeSeconds_ < static_cast<double>(glideSeconds_) && attackGlideCents_ != 0.0f)
    {
        const float frac  = static_cast<float>(noteTimeSeconds_) / glideSeconds_;
        const float cents = attackGlideCents_ * (1.0f - smoothstep01(frac));
        glideRatio = centsToRatioApprox(cents);
    }

    // The random-walk detune is updated once per block (control rate). A leaky
    // integrator pulling toward fresh white noise gives a slow, bounded, sub-Hz
    // wobble — the beating between independently-drifting partials is exactly the
    // "chorus of real pipes" shimmer that a static Fourier stack lacks.
    constexpr float kDriftCoef = 0.10f;

    for (std::size_t i = 0; i < partialCount_; ++i)
    {
        Partial& p = partials_[i];

        // Advance this partial's independent slow pitch drift.
        p.driftCents += kDriftCoef * (nextBipolar(p.rng) * instabilityCents_ - p.driftCents);
        const float driftRatio = centsToRatioApprox(p.driftCents);

        // Per-partial wind sensitivity scales how much this partial tracks the
        // global deviation (brightness development lives here in a later phase).
        const double partialPitch = 1.0 + (pitchRatio - 1.0) * static_cast<double>(p.windSensitivity + 1.0f);
        const double freq = fundamentalHz_ * static_cast<double>(p.ratioToF0)
                          * partialPitch * static_cast<double>(glideRatio)
                          * static_cast<double>(driftRatio);

        // Anti-aliasing: drop partials above Nyquist and fade the top with a
        // raised-cosine (smoothstep) rather than a linear ramp, so a partial
        // sliding across the fade edge under drift/glide never steps its level.
        const float aaGain = smoothstep01(static_cast<float>((nyquist - freq) / (nyquist * 0.4)));

        // Gentle per-note top-octave tilt (-6 dB/oct above the corner) that tames
        // the metallic upper partials of high notes without dulling foundations.
        float hfGain = 1.0f;
        if (freq > static_cast<double>(hfRolloffHz_))
            hfGain = hfRolloffHz_ / static_cast<float>(freq);

        // Per-partial attack bloom: the partial stays silent until its onset time,
        // then eases in over its (pitch-derived) bloom window with a smooth curve.
        // The onset delay is CAPPED very short: a long, frequency-progressive
        // stagger (some recipes delay the top harmonics by tens of ms) made the
        // brightness sweep up during the attack on harmonically-rich stops — an
        // artificial "wah". Capping it keeps all partials speaking within a tight
        // window, so the onset is soft but never sweeps.
        constexpr float kMaxOnset = 0.004f;
        const float effOnset = p.onsetSeconds < kMaxOnset ? p.onsetSeconds : kMaxOnset;
        const double dt = noteTimeSeconds_ - static_cast<double>(effOnset);
        const float  bloomGain = smoothstep01(static_cast<float>(dt) / p.bloomSeconds);

        p.increment = static_cast<float>(kTwoPi * freq / sr);
        p.blockGain = p.amplitude * aaGain * hfGain * bloomGain * levelLin
                    * p.noteLevelScale * p.formantGain;
        // Skip the sinf for effectively-silent partials (net CPU win), but only
        // once the per-sample gain ramp has faded them out — see renderAdd.
        p.active = p.blockGain > 1.0e-6f || p.curGain > 1.0e-6f;
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

    // Per-partial gain ramp: interpolate each partial's gain from its previous
    // value to this block's target ACROSS the block, so the per-block coefficient
    // update (bloom, wind, drift) never steps at a block boundary. That step was a
    // subtle click at every note attack; ramping removes it (no zipper).
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;
    for (std::size_t i = 0; i < partialCount_; ++i)
        partials_[i].gainInc = (partials_[i].blockGain - partials_[i].curGain) * invFrames;

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

        // Sum the partials for this frame, ramping each partial's gain per sample.
        // Silent partials (marked !active this block) skip the sinf but still
        // advance phase and ramp their gain, so muting/un-muting stays click-free
        // and phase-coherent while buying back CPU on sparse spectra.
        float tone = 0.0f;
        for (std::size_t i = 0; i < partialCount_; ++i)
        {
            Partial& p = partials_[i];
            p.curGain += p.gainInc;
            if (p.active)
                tone += p.curGain * std::sin(p.phase);
            p.phase += p.increment;
            if (p.phase >= kTwoPi) p.phase -= kTwoPi;
        }

        // Shape the whole-bank envelope with a raised-cosine curve so the overall
        // onset/decay is never a hard linear ramp.
        tone *= smoothstep01(envGain_) * masterGain_ * velocityGain_;

        // Wind-attack chiff: added AFTER the tone envelope so it speaks at full
        // level right at note onset instead of being swallowed by the fade-in.
        float sample = tone;
        if (chiffLenSamp_ > 0 && chiffSamp_ < chiffLenSamp_)
        {
            const float noise = nextBipolar(chiffRng_);
            chiffLp_ += chiffLpCoef_ * (noise - chiffLp_);
            if (chiffSamp_ < chiffAtkSamp_)
                chiffEnv_ = static_cast<float>(chiffSamp_) / static_cast<float>(chiffAtkSamp_);
            else
                chiffEnv_ *= chiffDecayMul_;
            sample += chiffLp_ * chiffEnv_ * chiffAmount_ * velocityGain_ * masterGain_;
            ++chiffSamp_;
        }

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

} // namespace caecilia::synth
