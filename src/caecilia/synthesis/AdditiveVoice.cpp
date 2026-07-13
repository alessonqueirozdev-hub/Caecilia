/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/wind/WindResponseCurve.h"

#include <cmath>

namespace caecilia::synth
{

void AdditiveVoice::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames)
{
    bank_.prepare(sampleRate, maxBlockFrames);
}

void AdditiveVoice::setContext(const VoiceContext& context) noexcept
{
    context_ = context;
    // Wire the wind coupling from the tonal family's response curve (the curve
    // type and its per-family defaults are owned by the wind module).
    bank_.setWindCoupling(context_.wind,
                          context_.chest,
                          wind::defaultResponseFor(context_.family));

    // Per-family "living pipe" character (Aeolus-inspired). Each family drifts,
    // speaks and rolls off differently: strings shimmer (a strong celeste-like
    // beat) and speak slowly; reeds have a pronounced speech glide; flutes are
    // the steadiest; mixtures are bright and speak fast.
    // NOTE: the wind-attack chiff is DISABLED (setChiff 0). It was a filtered
    // white-noise burst, and firing one per voice meant a chord's simultaneous
    // onsets stacked into a metallic, cymbal-like "tss" transient. The organic,
    // soft attack now comes entirely from the per-partial bloom + speech glide
    // (which the ear reads as a pipe speaking, with no noise). A proper chiff
    // would need to be TONAL (noise resonated at the pipe pitch), not broadband.
    // The per-partial bloom is kept SMALL and nearly frequency-FLAT (all partials
    // speak together within ~8-12 ms, regardless of pitch). A frequency-dependent
    // bloom made the upper partials of harmonically-rich stops (strings, and any
    // string-bearing composite) lag behind the fundamental, so the brightness
    // swept up during the attack — an artificial "wah" / filter-opening, worst on
    // high notes. A flat bloom removes the sweep entirely; the soft onset now
    // comes from the whole-bank raised-cosine envelope, not the partial stagger.
    // The 5th arg is the PER-NOTE treble tilt (dB/octave of upper-harmonic roll on
    // high notes — the Aeolus _h_lev cue that a real pipe sheds brightness up the
    // compass). Because that now does the note-dependent taming, the absolute-Hz
    // hfCorner is RAISED so the bass isn't double-dulled. Strings keep their
    // uppers (low tilt); flutes shed fast; mixtures stay the bright crown.
    //                              instability  glide    bloom   hfCorner  tilt
    switch (context_.family)
    {
        case core::TonalFamily::Principal:
            bank_.setLiveliness(5.5f, -18.0f, 0.012f, 7000.0f, 6.0f); break;
        case core::TonalFamily::Flute:
            bank_.setLiveliness(3.5f, -12.0f, 0.011f, 8000.0f, 7.5f); break;
        case core::TonalFamily::String:
            bank_.setLiveliness(7.5f,  -8.0f, 0.010f, 6500.0f, 3.0f); break;
        case core::TonalFamily::Reed:
            bank_.setLiveliness(6.0f, -22.0f, 0.012f, 6000.0f, 4.0f); break;
        case core::TonalFamily::Mixture:
            bank_.setLiveliness(8.0f, -14.0f, 0.010f, 6000.0f, 2.5f); break;
        default:
            bank_.setLiveliness(5.5f, -16.0f, 0.011f, 7000.0f, 6.0f); break;
    }
    bank_.setChiff(0.0f);
}

void AdditiveVoice::seedFrom(const SpectralModel& model, float phaseAlignSeconds) noexcept
{
    bank_.seedFrom(model, phaseAlignSeconds);
}

void AdditiveVoice::noteOn(core::PipeId pipe, core::Velocity velocity) noexcept
{
    pipe_ = pipe;

    double frequency = soundingFrequencyHz(context_, pipe);
    // Apply the deterministic per-pipe detune (cents -> ratio).
    frequency *= std::pow(2.0, static_cast<double>(voicing_.detuneCents) / 1200.0);

    // The per-pipe level trim folds into the bank's master gain. The base level is
    // set so a single soft registration is clearly audible (~-19 dBFS) while a full
    // registration rides gently into the master limiter — the loudness restage that
    // fixes both "too quiet" (sparse) and "distorts" (dense slamming the limiter).
    bank_.setMasterGain(0.85f * std::pow(10.0f, voicing_.levelTrimDb * 0.05f));
    bank_.trigger(pipe, velocity, frequency);
}

void AdditiveVoice::noteOff() noexcept
{
    bank_.release();
}

void AdditiveVoice::renderAdd(core::AudioBlock& block) noexcept
{
    bank_.renderAdd(block);
}

bool AdditiveVoice::isActive() const noexcept
{
    return bank_.isActive();
}

float AdditiveVoice::cpuCostEstimate() const noexcept
{
    // Roughly proportional to the seeded partial count; additive is mid-cost.
    return 0.5f + 0.02f * static_cast<float>(bank_.activePartialCount());
}

} // namespace caecilia::synth
