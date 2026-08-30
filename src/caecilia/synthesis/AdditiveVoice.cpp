// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"
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

    // Per-voice HEADROOM baked into the synthesis (Aeolus/GrandOrgue philosophy):
    // each voice sits ~-12 dBFS so that dozens summed stay under the ceiling by
    // static gain alone — no bus compressor, no polyphony duck. Aeolus generates
    // each pipe at ~0.08-0.21 linear (-14..-22 dBFS); GrandOrgue folds a -15 dB
    // master (0.178) into every voice. 0.25 sits in that band. The tutti is kept
    // clean by this headroom PLUS the per-voice decorrelation (uncorrelated drift +
    // scattered start phase), which makes N voices sum incoherently (~sqrt(N)).
    constexpr float kVoiceHeadroom = 0.25f;
    bank_.setMasterGain(kVoiceHeadroom * std::pow(10.0f, voicing_.levelTrimDb * 0.05f));
    bank_.trigger(pipe, velocity, frequency);
}

void AdditiveVoice::noteOff() noexcept
{
    bank_.release();
}

void AdditiveVoice::silence() noexcept
{
    bank_.silence();
}

void AdditiveVoice::setExpression(float startGain, float incPerSample) noexcept
{
    bank_.setExpression(startGain, incPerSample);
}

void AdditiveVoice::adoptRank(const void* voicing) noexcept
{
    if (voicing == nullptr)
        return;
    const auto& v = *static_cast<const RankVoicing*>(voicing);

    context_.family = v.family;
    context_.chest  = v.chest;
    context_.rank   = core::RankId{ v.stop.value };

    // EIGHT FOOT, always -- not the rank's own footage, however wrong that reads.
    //
    // The voicing's partial ratios are already referenced to 8' unison: a 16' rank
    // carries its ratios halved, and a mixture or mutation encodes its pitch in the
    // ratios directly (which is what buildRankVoicing's `fold` is for). Setting the
    // context to the rank's footage transposes all of that a SECOND time.
    //
    // Measured before this line existed: the 16' ranks sounded an octave low, and
    // the mixtures and mutations were pitched so far up that the anti-aliasing gate
    // silenced most of them. A full Tutti came out 3.8 dB down and thin -- with the
    // right number of partials, the right spectra and the right levels.
    //
    // The footage does still matter, just not here: it shapes the speech profile,
    // which arrives already resolved in v.speech.
    context_.footage = core::footage::kEight;

    // Re-wire the wind coupling for the rank just adopted. The bank was given a
    // chest and a family response curve by setContext(), once, at prepare time --
    // and a voice does not know which rank it will become until here, so without
    // this it keeps them: every voice of the instrument read chest 0's pressure and
    // responded with the curve of whatever family the pool happened to be built
    // with. The Récit's tremulant reached nothing, and a Trompette breathed like a
    // Montre. Both are invisible in a spectrum and only show up as a rendered
    // pressure response, which is what "The tremulant reaches the pipes" measures.
    // Scaled by the rank's own sensitivity, which is what the organ file's
    // voicing.windSensitivity finally reaches. 0.5 leaves the family curve alone,
    // so an organ that does not mention it sounds exactly as it did.
    wind::WindResponseCurve curve = wind::defaultResponseFor(context_.family);
    {
        const float k = 2.0f * (v.windSensitivity < 0.0f ? 0.0f
                                                         : (v.windSensitivity > 1.0f ? 1.0f
                                                                                     : v.windSensitivity));
        curve.centsPerDeviation      *= k;
        curve.dbPerDeviation         *= k;
        curve.brightnessPerDeviation *= k;
        curve.attackPerDeviation     *= k;
    }
    bank_.setWindCoupling(context_.wind, context_.chest, curve);

    bank_.setSpeechProfile(v.speech);

    // Allocation-free: the bank's storage was reserved for kMaxPartials at prepare
    // time, and seedFrom copies into it. A Tutti chord is twenty-six of these
    // inside one audio callback and must not touch the allocator once.
    bank_.seedFrom(v.spectrum, 0.0f);
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
