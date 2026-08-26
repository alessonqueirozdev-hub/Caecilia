// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/DemoOrgan.h"

#include "caecilia/model/Coupler.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/Rank.h"
#include "caecilia/model/Windchest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace caecilia::model
{

namespace
{
    // -----------------------------------------------------------------------
    // Small spectral-recipe helpers.
    // -----------------------------------------------------------------------

    /// Nominal analysis reference pitch; overwritten by the true F0 at note-on.
    constexpr float kNominalFundamentalHz = 130.81f; // ~C3

    [[nodiscard]] float clampf(float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /// Sounding pitch ratio of a footage relative to 8' unison: (8 * den) / num.
    /// 8' -> 1, 4' -> 2, 2' -> 4, 2 2/3' -> 3, 1 3/5' -> 5, 1 1/3' -> 6.
    [[nodiscard]] float footageRatioToUnison(core::Footage f) noexcept
    {
        if (f.num == 0)
            return 0.0f;
        return (8.0f * static_cast<float>(f.den)) / static_cast<float>(f.num);
    }

    /// How a rank's harmonic development scales with the size of its pipes.
    ///
    /// Organ builders do not voice a 2' rank like a 16' one. Small pipes develop
    /// fewer usable upper partials — partly the scaling law (a halving ratio
    /// wider than 1:2 makes short pipes relatively broader and duller), partly
    /// plain practicality: the tenth harmonic of a 1' rank is inaudible and only
    /// adds hiss. The recipes used to ignore the footage they were handed, so the
    /// whole 26-stop instrument was five timbres transposed, and the chorus never
    /// cohered the way a real one does.
    ///
    /// @return  .first  = harmonic count multiplier, .second = extra roll-off.
    [[nodiscard]] std::pair<float, float> scalingForFootage(core::Footage f) noexcept
    {
        // Octaves above 8' unison: 16' -> -1, 8' -> 0, 4' -> +1, 2' -> +2, 1' -> +3.
        const double feet = f.feet();
        const float octavesUp = feet > 0.0
            ? static_cast<float>(std::log2(8.0 / feet))
            : 0.0f;
        const float clamped = clampf(octavesUp, -2.0f, 3.0f);
        // Each octave up sheds ~18% of the harmonic count and steepens the
        // roll-off by ~6%.
        return { clampf(1.0f - 0.18f * clamped, 0.30f, 1.45f),
                 clampf(1.0f + 0.06f * clamped, 0.85f, 1.30f) };
    }

    /// Append one partial to a model.
    void appendPartial(synth::SpectralModel& model,
                       float ratio,
                       float ampDb,
                       float windSensitivity,
                       float onsetSeconds,
                       float brightnessTrack,
                       float phase = 0.0f,
                       bool  breaksBack = false)
    {
        synth::PartialTrack t;
        t.ratioToF0       = ratio;
        t.ampDb           = ampDb;
        t.phase           = phase;
        t.windSensitivity = windSensitivity;
        t.onsetSeconds    = onsetSeconds;
        t.brightnessTrack = brightnessTrack;
        t.breaksBack      = breaksBack;
        model.partials.push_back(t);
    }
} // namespace

// ---------------------------------------------------------------------------
// Spectral recipes.
// ---------------------------------------------------------------------------

synth::SpectralModel makeSpectralPrincipal(core::Footage footage,
                                           float         brightness,
                                           std::size_t   numHarmonics)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    // Brighter voices roll off more slowly. rolloff ~1.0 => -6 dB/octave.
    const auto  scale   = scalingForFootage(footage);
    const float rolloff = clampf(1.35f - 0.35f * clampf(brightness, 0.0f, 2.0f), 0.55f, 1.6f)
                        * scale.second;

    const auto scaled = static_cast<std::size_t>(
        static_cast<float>(numHarmonics) * scale.first + 0.5f);
    const std::size_t harmonics = scaled < 1 ? 1 : scaled;
    for (std::size_t n = 1; n <= harmonics; ++n)
    {
        const float fn      = static_cast<float>(n);
        const float ampDb   = -20.0f * rolloff * std::log10(fn);
        const float windSen = clampf(0.30f + 0.03f * fn, 0.0f, 0.60f);
        const float onset   = 0.0015f * (fn - 1.0f);   // gentle upper-partial stagger
        const float bright  = clampf(0.02f * fn, 0.0f, 0.4f);
        appendPartial(model, fn, ampDb, windSen, onset, bright);
    }
    return model;
}

synth::SpectralModel makeSpectralFlute(core::Footage footage,
                                       float         brightness,
                                       bool          stopped)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    // Small flutes are purer still: the scaling roll-off takes their already weak
    // upper partials further down.
    const float extraRolloffDb = -6.0f * (scalingForFootage(footage).second - 1.0f) * 10.0f;

    // Strong, pure fundamental — the body of a flute/bourdon.
    appendPartial(model, 1.0f, 0.0f, 0.20f, 0.0f, 0.01f);

    if (stopped)
    {
        // Stopped pipes emphasise odd harmonics; all weak.
        const int odd[] = { 3, 5, 7, 9 };
        for (int idx = 0; idx < 4; ++idx)
        {
            const float n     = static_cast<float>(odd[idx]);
            const float ampDb = -16.0f - 6.0f * static_cast<float>(idx)
                              + 3.0f * (brightness - 1.0f) + extraRolloffDb;
            appendPartial(model, n, ampDb, 0.18f, 0.002f * static_cast<float>(idx), 0.02f);
        }
    }
    else
    {
        // Open flute: a touch of the low even harmonics too, still gentle.
        appendPartial(model, 2.0f, -14.0f + 3.0f * (brightness - 1.0f) + extraRolloffDb, 0.18f, 0.001f, 0.02f);
        appendPartial(model, 3.0f, -22.0f + extraRolloffDb, 0.18f, 0.002f, 0.02f);
        appendPartial(model, 4.0f, -30.0f + extraRolloffDb, 0.18f, 0.003f, 0.02f);
    }
    return model;
}

synth::SpectralModel makeSpectralString(core::Footage footage,
                                        float         detuneCents,
                                        float         brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    const auto scale = scalingForFootage(footage);
    const auto kHarmonics = static_cast<std::size_t>(16.0f * scale.first + 0.5f);
    const float rolloff  = clampf(0.70f - 0.10f * (brightness - 1.0f), 0.40f, 1.0f)
                         * scale.second; // slow => bright uppers
    const float detune   = std::pow(2.0f, detuneCents / 1200.0f);
    const bool  celeste  = std::fabs(detuneCents) > 1.0e-3f;

    for (std::size_t n = 1; n <= kHarmonics; ++n)
    {
        const float fn      = static_cast<float>(n);
        float       ampDb   = -20.0f * rolloff * std::log10(fn);
        if (n >= 2 && n <= 6)          // characteristic string emphasis band
            ampDb += 2.0f;
        const float windSen = clampf(0.35f + 0.02f * fn, 0.0f, 0.60f);
        const float onset   = 0.004f * (fn - 1.0f);   // strings speak slowly
        const float bright  = clampf(0.03f * fn, 0.0f, 0.5f);

        appendPartial(model, fn, ampDb, windSen, onset, bright);
        if (celeste)
            appendPartial(model, fn * detune, ampDb - 3.0f, windSen, onset, bright);
    }
    return model;
}

synth::SpectralModel makeSpectralReed(core::Footage footage,
                                      float         formantCenterHz,
                                      float         brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    const auto scale = scalingForFootage(footage);
    const auto kHarmonics = static_cast<std::size_t>(16.0f * scale.first + 0.5f);
    const float rolloff = clampf(0.90f - 0.10f * (brightness - 1.0f), 0.50f, 1.2f)
                        * scale.second;

    for (std::size_t n = 1; n <= kHarmonics; ++n)
    {
        const float fn    = static_cast<float>(n);
        float       ampDb = -20.0f * rolloff * std::log10(fn);
        ampDb += (n % 2 == 1) ? 4.0f : -3.0f;          // strong odd harmonics
        if (n >= 4 && n <= 8)                          // emphasised formant partials
            ampDb += 6.0f;
        const float windSen = clampf(0.40f + 0.02f * fn, 0.0f, 0.70f);
        // Reeds speak with a chiff: the fundamental lags a touch, uppers stagger.
        const float onset   = (n == 1 ? 0.001f : 0.0f) + 0.003f * (fn - 1.0f);
        const float bright  = clampf(0.03f * fn, 0.0f, 0.5f);
        appendPartial(model, fn, ampDb, windSen, onset, bright);
    }

    // Fixed-Hz reed formant, now READ by the additive path (PartialBank bakes a
    // per-note boost from it). Fixed centres mean the emphasised band lands on a
    // different harmonic at every pitch — the constant brassy snarl of a Trompette
    // instead of a pitch-tracking synth resonance. Body (~450 Hz), snarl (~1.2 kHz)
    // and a little brilliance (~2.6 kHz); the caller's formantCenterHz nudges the
    // snarl so different reeds (Trompette vs Basson) differ.
    const float snarlHz = clampf(formantCenterHz > 1.0f ? formantCenterHz : 1200.0f, 700.0f, 2000.0f);
    model.steadyFormants.peaks[0] = { 450.0f,   2.5f, 400.0f };
    model.steadyFormants.peaks[1] = { snarlHz,  4.5f, 900.0f };
    model.steadyFormants.peaks[2] = { 2600.0f,  2.0f, 1500.0f };
    model.steadyFormants.peakCount = 3;
    return model;
}

synth::SpectralModel makeSpectralMixture(std::span<const core::Footage> ranks,
                                         float                          brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    // A faint grounding fundamental so the crown still blends with the chorus.
    appendPartial(model, 1.0f, -22.0f, 0.40f, 0.0f, 0.02f);

    const float brightBoost = 2.0f * (brightness - 1.0f);
    std::size_t idx = 0;
    for (const core::Footage f : ranks)
    {
        const float ratio = footageRatioToUnison(f);
        if (ratio <= 0.0f)
            continue;
        const float ampDb = -4.0f - 3.0f * static_cast<float>(idx) + brightBoost;
        appendPartial(model, ratio, ampDb, 0.45f, 0.001f * static_cast<float>(idx), 0.05f,
                      /*phase*/ 0.0f, /*breaksBack*/ true);
        // The octave above each rank pitch adds the characteristic mixture shimmer.
        appendPartial(model, ratio * 2.0f, ampDb - 10.0f, 0.50f, 0.002f * static_cast<float>(idx), 0.06f,
                      /*phase*/ 0.0f, /*breaksBack*/ true);
        ++idx;
    }
    return model;
}

synth::SpectralModel makeSpectralMutation(core::Footage footage,
                                          float         brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    const float ratio       = footageRatioToUnison(footage);
    const float brightBoost = 2.0f * (brightness - 1.0f);

    // A faint true fundamental for blend, then dominant energy on the mutation
    // ratio (quint / tierce) and a couple of its own harmonics.
    appendPartial(model, 1.0f, -20.0f, 0.25f, 0.0f, 0.01f);
    if (ratio > 0.0f)
    {
        appendPartial(model, ratio,        0.0f + brightBoost, 0.30f, 0.002f, 0.02f, 0.0f, true);
        appendPartial(model, ratio * 2.0f, -14.0f,             0.30f, 0.004f, 0.03f, 0.0f, true);
        appendPartial(model, ratio * 3.0f, -22.0f,             0.35f, 0.006f, 0.04f, 0.0f, true);
    }
    return model;
}

synth::SpectralModel spectralModelForStop(const Stop& stop)
{
    const core::Footage footage = stop.footage();

    // Mixtures: place partials at every constituent rank pitch.
    if (stop.family() == core::TonalFamily::Mixture || stop.isCompound())
        return makeSpectralMixture(std::span<const core::Footage>(stop.mixtureComposition()));

    switch (stop.family())
    {
        case core::TonalFamily::Principal:
            return makeSpectralPrincipal(footage);

        case core::TonalFamily::Flute:
            // A flute-family stop at a mutation footage is a Nazard / Tierce.
            if (stop.isMutation())
                return makeSpectralMutation(footage);
            return makeSpectralFlute(footage);

        case core::TonalFamily::String:
            // Colour-role strings are the céleste ranks — give them a beat.
            return makeSpectralString(footage,
                                      stop.role() == core::ChorusRole::Color ? 3.0f : 0.0f);

        case core::TonalFamily::Reed:
            return makeSpectralReed(footage);

        default:
            if (stop.isMutation())
                return makeSpectralMutation(footage);
            return makeSpectralPrincipal(footage);
    }
}

namespace
{
/// Voicing balance for one drawn rank inside the composite (see header): a base
/// level per tonal family, darkened an octave per pitch step above 8'. Mixtures
/// and mutations sit under the foundations.
float compositeRankGainDb(const Stop& stop) noexcept
{
    const bool unisonReferenced = stop.family() == core::TonalFamily::Mixture
                               || stop.isCompound()
                               || stop.footage().isMutation();
    if (unisonReferenced)
        return -11.0f;

    float base = -3.0f;
    switch (stop.family())
    {
        case core::TonalFamily::Principal:  base =  0.0f; break;
        case core::TonalFamily::Flute:      base = -1.5f; break;
        case core::TonalFamily::String:     base = -4.0f; break;
        case core::TonalFamily::Reed:       base = -2.0f; break;
        case core::TonalFamily::Hybrid:     base = -2.0f; break;
        case core::TonalFamily::Mixture:
        case core::TonalFamily::Percussion:
        case core::TonalFamily::Undefined:
        default:                            base = -3.0f; break;
    }
    // octaveClassFrom8(): 0 for 8', +1 for 4', +2 for 2', -1 for 16'.
    return base - 3.3f * static_cast<float>(stop.footage().octaveClassFrom8());
}

/// Fixed calibration from composite spectral energy to voice amplitude.
///
/// This replaces a per-registration ENERGY NORMALISATION — an automatic gain
/// control that scaled every registration to a target RMS. That target was
/// clamped to [0.06, 0.40], so the entire span from one soft flute to full
/// Tutti was squeezed into ~13 dB, and past about thirteen stops' worth of
/// weight the ceiling meant drawing MORE stops changed the timbre without
/// changing the loudness at all. On a real instrument the registration IS the
/// dynamic: there is no touch sensitivity and no swell beyond the shades, so
/// flattening it removes the organ's principal expressive resource.
///
/// The value is chosen so a single Principal 8' lands exactly where it did
/// under the old AGC; everything else now grows and shrinks around it. Headroom
/// comes from the per-voice base gain plus the master limiter — which is only
/// now doing its job, since it used to read from an undersized look-ahead ring
/// and could not hold the ceiling at all.
constexpr double kRegistrationCalibration = 0.0879; // 0.11 / energy(Principal 8')

/// Scale every partial by a fixed factor expressed in the spectrum's dB domain.
/// Unlike the old normaliser this does NOT depend on the registration, so
/// stacking ranks genuinely accumulates energy. Off-thread.
void scaleComposite(synth::SpectralModel& c, double factor) noexcept
{
    if (factor <= 0.0)
        return;
    const float dB = 20.0f * std::log10(static_cast<float>(factor));
    for (synth::PartialTrack& p : c.partials)
        p.ampDb += dB;
}

/// Carry the fixed-Hz formant envelope of the first FORMANTED rank into the
/// composite.
///
/// Both composite builders used to drop it on the floor: they copied partial
/// tracks one by one and never touched `steadyFormants`, so a drawn Trompette
/// lost the 450 Hz / snarl / 2.6 kHz resonances that ARE its character and came
/// out as a generic bright harmonic stack. The envelope is a property of the
/// resonator, not of any one partial, so the composite takes it from the first
/// rank that has one.
void adoptFormants(synth::SpectralModel& composite, const synth::SpectralModel& recipe) noexcept
{
    if (composite.steadyFormants.peakCount == 0 && recipe.steadyFormants.peakCount > 0)
        composite.steadyFormants = recipe.steadyFormants;
}

/// Deterministic per-rank tuning offset, in cents.
///
/// Real ranks are tuned as units and sit a couple of cents apart from each
/// other; the pipes WITHIN a rank are near-harmonic. The previous humanisation
/// inverted that: it scattered every partial independently, including each
/// rank's own fundamental, so the instrument's pitch shifted by up to 3.5 cents
/// and — because the scatter was keyed on the partial's index in the composite
/// vector — the shift CHANGED depending on the order in which stops were drawn.
///
/// Keying the offset on (family, footage) instead makes it stable: the same rank
/// always sits in the same place, drawing stops in any order gives the same
/// tuning, and the beating between ranks is deliberate rather than accidental.
[[nodiscard]] float rankDetuneCents(core::TonalFamily family, core::Footage footage) noexcept
{
    std::uint32_t h = 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(family) * 2654435761u;
    h ^= static_cast<std::uint32_t>(footage.num) * 40503u;
    h ^= static_cast<std::uint32_t>(footage.den) * 2246822519u;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15;
    const float unit = static_cast<float>(h & 0xFFFFu) / 65535.0f; // 0..1
    return (unit - 0.5f) * 4.0f;                                   // +/- 2 cents
}

/// Apply a rank's tuning offset and a deterministic start phase to its partials.
///
/// The whole rank shifts together (so it stays internally harmonic), the
/// fundamental included; a much smaller residual scatter is added to the UPPER
/// partials only, standing in for the slight inharmonicity of a real pipe.
///
/// @param rankSalt Distinguishes ranks that are otherwise the same recipe.
///
/// That last parameter is not decoration. This organ has FOUR Reed 8' ranks and
/// three Flute 8'; keyed on (family, footage) alone they received identical seeds,
/// therefore identical start phases and identical detune, therefore summed
/// COHERENTLY -- four ranks at four times amplitude where four separate ranks of
/// pipes give twice. Measured at 5.9 dB on a full Tutti.
///
/// It is the tutti hotspot the per-VOICE decorrelation was written to fix. That
/// one addressed two voices sounding the same PITCH and could not address two
/// RANKS being the same recipe, because the salt was per voice and the seed it
/// salted was already the same.
void voiceRank(synth::SpectralModel& c, std::size_t firstPartial,
               core::TonalFamily family, core::Footage footage,
               std::uint32_t rankSalt) noexcept
{
    const float rankRatio = std::pow(2.0f,
        (rankDetuneCents(family, footage)
         // A rank's own place in the tuning, so two Trompettes on different
         // manuals sit a hair apart the way two real ranks do.
         + (static_cast<float>((rankSalt * 2654435761u) >> 20 & 0x3FFu) / 1023.0f - 0.5f) * 3.0f)
        / 1200.0f);

    std::uint32_t h = 0x85EBCA6Bu
                    ^ (static_cast<std::uint32_t>(family) * 2654435761u)
                    ^ (static_cast<std::uint32_t>(footage.num) * 2246822519u)
                    ^ (rankSalt * 0x9E3779B9u);
    for (std::size_t i = firstPartial; i < c.partials.size(); ++i)
    {
        synth::PartialTrack& p = c.partials[i];
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;

        p.phase = static_cast<float>(h & 0xFFFFu) / 65535.0f * 6.2831853f;

        // Identity = (rank, position within the rank). Stable no matter where this
        // partial ends up in the composite vector.
        p.seed = h | 1u;

        float ratio = rankRatio;
        if (i > firstPartial) // leave the rank's own fundamental exactly in tune
        {
            const float residual = (static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f - 0.5f);
            ratio *= std::pow(2.0f, residual / 1200.0f); // +/- 0.5 cent
        }
        p.ratioToF0 *= ratio;
    }

    // Where this rank's own fundamental sits, so the treble tilt can tell a rank's
    // fundamental from a fourth harmonic.
    //
    // Read off the FOOTAGE, not off the spectrum. The lowest partial present is not
    // the rank's fundamental: a Nazard 2 2/3' carries a weak trace at the unison
    // (measured ratios 1.0, 3.0, 6.0, 9.0 — it speaks at 3, not at 1), and a
    // compound stop has one fundamental per member. Both make a scan for the
    // minimum answer 1.0 and change nothing, which is exactly what the first
    // attempt at this did.
    const double feet = footage.feet();
    const float  base = feet > 0.0 ? static_cast<float>(8.0 / feet) : 1.0f;
    for (std::size_t i = firstPartial; i < c.partials.size(); ++i)
        c.partials[i].rankBaseRatio = base;
}

} // namespace

synth::SpectralModel buildRegistrationCompositeSpectrum(
    const Organ& organ, std::span<const core::StopId> engagedStops)
{
    synth::SpectralModel composite;
    if (engagedStops.empty())
        return composite;

    // NOTE: there is deliberately no "stack trim" here any more. It used to scale
    // every rank by -5*log10(n), which was silently cancelled by the energy
    // normaliser that followed and therefore did nothing at all. Now that the
    // normaliser is gone, drawing more stops must genuinely add energy — that is
    // what registration dynamics ARE — and headroom is the limiter's job.
    for (const core::StopId sid : engagedStops)
    {
        const Stop* stop = organ.stop(sid);
        if (stop == nullptr)
            continue;

        // Fold the footage into the partial ratios ourselves — EXACTLY as the
        // per-stop voicing does (unison-referenced recipes already sit at 8').
        const bool unisonReferenced = stop->family() == core::TonalFamily::Mixture
                                   || stop->isCompound()
                                   || stop->footage().isMutation();
        const double feet = stop->footage().feet();
        const double fold = (unisonReferenced || feet <= 0.0) ? 1.0 : 8.0 / feet;
        const float  gainDb = compositeRankGainDb(*stop);

        const std::size_t rankFirst = composite.partials.size();
        const synth::SpectralModel recipe = spectralModelForStop(*stop);
        adoptFormants(composite, recipe);
        for (const synth::PartialTrack& p : recipe.partials)
        {
            synth::PartialTrack t = p;
            t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * fold);
            t.ampDb     = p.ampDb + gainDb;
            composite.partials.push_back(t);
        }
        voiceRank(composite, rankFirst, stop->family(), stop->footage(),
                  static_cast<std::uint32_t>(sid.value) + 1u);
    }

    scaleComposite(composite, kRegistrationCalibration);
    composite.fundamentalHz = 0.0f; // set per note by the voice at noteOn
    return composite;
}

synth::SpeechProfile speechProfileFor(core::TonalFamily family, core::Footage footage) noexcept
{
    // The struct's own defaults are a Principal: 55 ms at C2 down to 10 ms at C7,
    // releasing in 300/110. Every other family is expressed as a multiple of that,
    // because what matters musically is the RATIO between families on the same
    // registration, not any one absolute figure.
    synth::SpeechProfile p{};
    float attack = 1.0f;
    float release = 1.0f;

    switch (family)
    {
        case core::TonalFamily::Reed:
            // A tongue beats against the shallot as soon as there is wind. Reeds
            // are the promptest thing on the instrument and their attack is a
            // large part of why a Trompette cuts through a plenum.
            attack = 0.45f; release = 0.75f;
            break;
        case core::TonalFamily::Flute:
            // Wide scale, often stopped: a substantial volume of air to fill
            // before the column speaks, and it collapses slowly too.
            attack = 1.35f; release = 1.25f;
            break;
        case core::TonalFamily::String:
            // Narrow scale. The hardest rank on the organ to get speaking
            // cleanly -- which is why real string ranks carry a beard or a roller.
            attack = 1.90f; release = 1.15f;
            break;
        case core::TonalFamily::Mixture:
            // Small pipes, tiny air columns: essentially instantaneous, and the
            // crown of a plenum is expected to arrive with the chorus rather than
            // after it.
            attack = 0.55f; release = 0.70f;
            break;
        case core::TonalFamily::Hybrid:
        case core::TonalFamily::Principal:
        case core::TonalFamily::Percussion:
        case core::TonalFamily::Undefined:
        default:
            break;
    }

    // Pipe LENGTH dominates within a family as well as across the compass: a 16'
    // Bourdon fills far more slowly than a 2' of the same construction. The
    // per-note interpolation already handles where on the keyboard a note sits;
    // this handles where the whole rank sits.
    const double feet = footage.feet();
    if (feet > 0.0)
    {
        const float lengthScale = static_cast<float>(std::pow(feet / 8.0, 0.25));
        attack  *= lengthScale;
        release *= lengthScale;
    }

    p.attackAtC2Sec  *= attack;
    p.attackAtC7Sec  *= attack;
    p.releaseAtC2Sec *= release;
    p.releaseAtC7Sec *= release;
    return p;
}

RankVoicing buildRankVoicing(const Organ& organ, core::StopId stop)
{
    RankVoicing v;
    const Stop* s = organ.stop(stop);
    if (s == nullptr)
        return v;

    v.stop     = s->id();
    v.division = s->division();
    v.family   = s->family();
    v.footage  = s->footage();
    v.speech   = speechProfileFor(s->family(), s->footage());

    // The chest that feeds this rank, which is what the voice reads its wind
    // pressure from. It lives on the RANK, not on the stop -- and leaving it at its
    // default put every rank of the instrument on chest 0: the Récit's tremulant
    // would have reached nothing, and no division would have sagged under its own
    // load. Caught by "The tremulant reaches the pipes".
    if (const Rank* rank = organ.rank(s->rank()))
        v.chest = rank->windchest();

    // Exactly what buildRegistrationCompositeSpectrum does for one rank, in the
    // same order and with the same numbers. Not "equivalent": the same code path,
    // so the two cannot drift and the agreement test is structural.
    const bool unisonReferenced = s->family() == core::TonalFamily::Mixture
                               || s->isCompound()
                               || s->footage().isMutation();
    const double feet = s->footage().feet();
    const double fold = (unisonReferenced || feet <= 0.0) ? 1.0 : 8.0 / feet;
    const float  gainDb = compositeRankGainDb(*s);

    const synth::SpectralModel recipe = spectralModelForStop(*s);
    adoptFormants(v.spectrum, recipe);
    for (const synth::PartialTrack& p : recipe.partials)
    {
        synth::PartialTrack t = p;
        t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * fold);
        t.ampDb     = p.ampDb + gainDb;
        v.spectrum.partials.push_back(t);
    }
    // The same salt the composite uses for this stop, so the two agree partial
    // for partial -- which is what PerRankDynamicsTest asserts.
    voiceRank(v.spectrum, 0, s->family(), s->footage(),
              static_cast<std::uint32_t>(stop.value) + 1u);

    // The same fixed calibration. It is registration-INDEPENDENT by design -- that
    // is what makes drawing more stops genuinely louder -- and it is exactly why
    // applying it per rank here gives the same result as applying it once to the
    // sum over there.
    scaleComposite(v.spectrum, kRegistrationCalibration);
    v.spectrum.fundamentalHz = 0.0f; // set per note by the voice at noteOn
    return v;
}

core::DivisionId primaryManual(const Organ& organ) noexcept
{
    // Count stops per MANUAL division. A pedal division is never the answer: it
    // is played with the feet, its compass is thirty notes, and routing a
    // controller's manual keyboard there is silently wrong rather than merely
    // odd.
    const Division*  best      = nullptr;
    std::size_t      bestCount = 0;

    for (const Division& d : organ.divisions())
    {
        if (d.kind() == DivisionKind::Pedal)
            continue;
        // Strictly greater, so a tie keeps the earlier division and the answer
        // does not depend on compile order.
        if (best == nullptr || d.stopCount() > bestCount)
        {
            best      = &d;
            bestCount = d.stopCount();
        }
    }

    if (best != nullptr)
        return best->id();

    // No manuals at all. Something is odd about this instrument, but answering
    // with a division that exists beats answering with one that does not.
    return organ.divisions().empty() ? core::DivisionId{} : organ.divisions().front().id();
}

std::vector<core::StopId> defaultOpeningRegistration(const Organ& organ, core::DivisionId manual)
{
    std::vector<core::StopId> drawn;

    // A classic opening plenum: the whole principal chorus, its mixtures, and an
    // 8' flute underneath for body.
    for (const Stop& s : organ.stops())
    {
        if (s.division() != manual)
            continue;
        const bool principalChorus = s.family() == core::TonalFamily::Principal
                                  || s.family() == core::TonalFamily::Mixture;
        const bool fluteFoundation = s.family() == core::TonalFamily::Flute
                                  && s.footage() == core::footage::kEight;
        if (principalChorus || fluteFoundation)
            drawn.push_back(s.id());
    }

    // An instrument that comes up silent reads as broken, so never return nothing
    // while there is an 8' stop anywhere on it.
    if (drawn.empty())
        for (const Stop& s : organ.stops())
            if (s.footage() == core::footage::kEight)
            {
                drawn.push_back(s.id());
                break;
            }

    std::sort(drawn.begin(), drawn.end(),
              [](core::StopId a, core::StopId b) { return a.value < b.value; });
    return drawn;
}

std::vector<core::StopId> defaultOpeningRegistration(const Organ& organ)
{
    return defaultOpeningRegistration(organ, primaryManual(organ));
}

std::vector<core::StopId> resolveRanksToStops(const Organ&                      organ,
                                              std::span<const RegistrationRank> ranks)
{
    const auto& stops = organ.stops();
    std::vector<bool> claimed(stops.size(), false);
    std::vector<core::StopId> out;
    out.reserve(ranks.size());

    for (const RegistrationRank& r : ranks)
    {
        // Lowest unclaimed match wins. Deterministic, and claiming means two
        // identical ranks land on two different stops instead of doubling one.
        const Stop* pick = nullptr;
        std::size_t pickIndex = 0;

        for (std::size_t i = 0; i < stops.size(); ++i)
        {
            if (claimed[i])
                continue;
            const Stop& s = stops[i];
            if (s.family() != r.family || s.footage() != r.footage
                || s.isCompound() != r.compound)
                continue;
            if (pick == nullptr || s.id().value < pick->id().value)
            {
                pick      = &s;
                pickIndex = i;
            }
        }

        // No match resolves to nothing. Substituting the nearest stop would make a
        // registration that does not exist on this instrument sound like one that
        // does, which is worse than a rank that simply fails to speak.
        if (pick != nullptr)
        {
            claimed[pickIndex] = true;
            out.push_back(pick->id());
        }
    }

    std::sort(out.begin(), out.end(),
              [](core::StopId a, core::StopId b) { return a.value < b.value; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](core::StopId a, core::StopId b) { return a.value == b.value; }),
              out.end());
    return out;
}

core::Footage footageFromFeet(double feet) noexcept
{
    // Default member initialisers so the aggregate has no uninitialised state
    // even if somebody later default-constructs one.
    struct Std { double feet = 0.0; core::Footage f{}; };
    static const Std table[] = {
        { 32.0,             core::footage::kThirtyTwo },
        { 16.0,             core::footage::kSixteen },
        { 16.0 / 3.0,       core::footage::kFiveAndThird },      // 5 1/3'
        {  8.0,             core::footage::kEight },
        {  4.0,             core::footage::kFour },
        {  8.0 / 3.0,       core::footage::kTwoAndTwoThird },    // 2 2/3'
        {  2.0,             core::footage::kTwo },
        {  8.0 / 5.0,       core::footage::kOneAndThreeFifth },  // 1 3/5'
        {  4.0 / 3.0,       core::footage::kOneAndThird },       // 1 1/3'
        {  1.0,             core::footage::kOne },
    };
    core::Footage best = core::footage::kEight;
    double bestErr = 1e9;
    for (const Std& s : table)
    {
        const double e = feet > 0.0 ? std::abs(std::log(feet / s.feet)) : std::abs(feet - s.feet);
        if (e < bestErr) { bestErr = e; best = s.f; }
    }
    return best;
}

namespace
{
/// Spectral recipe for a UI rank (family + footage), mirroring spectralModelForStop.
synth::SpectralModel recipeForRank(core::TonalFamily fam, core::Footage ft, bool compound)
{
    if (compound || fam == core::TonalFamily::Mixture)
    {
        // A generic plein-jeu crown when the UI gives no explicit rank list.
        static const core::Footage plein[] = {
            core::footage::kTwo, core::footage::kOneAndThird, core::footage::kOne };
        return makeSpectralMixture(std::span<const core::Footage>(plein, 3), 1.2f);
    }
    switch (fam)
    {
        case core::TonalFamily::Principal:  return makeSpectralPrincipal(ft);
        case core::TonalFamily::Flute:      return ft.isMutation() ? makeSpectralMutation(ft)
                                                                   : makeSpectralFlute(ft);
        case core::TonalFamily::String:     return makeSpectralString(ft);
        case core::TonalFamily::Reed:       return makeSpectralReed(ft);
        case core::TonalFamily::Mixture:    // handled above
        case core::TonalFamily::Hybrid:
        case core::TonalFamily::Percussion:
        case core::TonalFamily::Undefined:
        default:                            return ft.isMutation() ? makeSpectralMutation(ft)
                                                                   : makeSpectralPrincipal(ft);
    }
}
} // namespace

synth::SpectralModel buildCompositeFromRegistration(std::span<const RegistrationRank> ranks)
{
    synth::SpectralModel composite;
    if (ranks.empty())
        return composite;

    // See buildRegistrationCompositeSpectrum: no stack trim, no energy normaliser.
    //
    // This builder is handed a LIST of ranks with no stop ids, so a rank's salt has
    // to come from the rank itself. Two identical ranks must not sum in phase --
    // four Reed 8' ranks summing coherently is a 6 dB hotspot -- but the salt must
    // ALSO not depend on the order they arrive in, or the same registration would
    // sound different depending on which drawstop was pulled first.
    //
    // Both at once: how many identical ranks came before this one. For a set of
    // ranks that is the same whatever order they are listed in, because the second
    // Trompette 8' is always the second Trompette 8'.
    const auto duplicateIndex = [&ranks](std::size_t upTo, const RegistrationRank& r)
    {
        std::uint32_t n = 0;
        for (std::size_t i = 0; i < upTo; ++i)
            if (ranks[i].family == r.family && ranks[i].footage == r.footage
                && ranks[i].compound == r.compound)
                ++n;
        return n;
    };
    std::size_t rankIndex = 0;
    for (const RegistrationRank& r : ranks)
    {
        const bool unisonReferenced = r.compound
                                   || r.family == core::TonalFamily::Mixture
                                   || r.footage.isMutation();
        const double feet = r.footage.feet();
        const double fold = (unisonReferenced || feet <= 0.0) ? 1.0 : 8.0 / feet;

        float base = -3.0f;
        if (unisonReferenced)
            base = -11.0f;
        else switch (r.family)
        {
            case core::TonalFamily::Principal:  base =  0.0f; break;
            case core::TonalFamily::Flute:      base = -1.5f; break;
            case core::TonalFamily::String:     base = -4.0f; break;
            case core::TonalFamily::Reed:       base = -2.0f; break;
            default:                            base = -3.0f; break;
        }
        const float octDark = unisonReferenced
                            ? 0.0f : -3.3f * static_cast<float>(r.footage.octaveClassFrom8());
        const float gainDb = base + octDark;

        const std::size_t rankFirst = composite.partials.size();
        const synth::SpectralModel recipe = recipeForRank(r.family, r.footage, r.compound);
        adoptFormants(composite, recipe);
        for (const synth::PartialTrack& p : recipe.partials)
        {
            synth::PartialTrack t = p;
            t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * fold);
            t.ampDb     = p.ampDb + gainDb;
            composite.partials.push_back(t);
        }
        voiceRank(composite, rankFirst, r.family, r.footage,
                  duplicateIndex(rankIndex++, r) + 1u);
    }

    scaleComposite(composite, kRegistrationCalibration);
    composite.fundamentalHz = 0.0f;
    return composite;
}

// ---------------------------------------------------------------------------
// The demo instrument.
// ---------------------------------------------------------------------------

namespace
{
    /// Per-family rank voicing character (drives tooltips / PerPipeVoicer scatter).
    [[nodiscard]] RankVoicingSpec voicingForFamily(core::TonalFamily family) noexcept
    {
        RankVoicingSpec v;
        switch (family)
        {
            case core::TonalFamily::Principal:
                v.chiffAmount = 0.15f; v.harmonicDevelopment = 0.65f; v.brightness = 0.60f; v.windSensitivity = 0.45f;
                break;
            case core::TonalFamily::Flute:
                v.chiffAmount = 0.25f; v.harmonicDevelopment = 0.30f; v.brightness = 0.40f; v.windSensitivity = 0.35f;
                break;
            case core::TonalFamily::String:
                v.chiffAmount = 0.10f; v.harmonicDevelopment = 0.85f; v.brightness = 0.80f; v.windSensitivity = 0.55f;
                v.detuneScatterCents = 1.5f;
                break;
            case core::TonalFamily::Reed:
                v.chiffAmount = 0.35f; v.harmonicDevelopment = 0.90f; v.brightness = 0.75f; v.windSensitivity = 0.60f;
                break;
            case core::TonalFamily::Mixture:
                v.chiffAmount = 0.20f; v.harmonicDevelopment = 0.95f; v.brightness = 0.90f; v.windSensitivity = 0.50f;
                v.detuneScatterCents = 2.0f;
                break;
            default:
                break;
        }
        return v;
    }

    /// Mutable working sets while assembling the instrument.
    struct Builder
    {
        std::vector<Windchest> windchests;
        std::vector<Rank>      ranks;
        std::vector<Stop>      stops;
        std::vector<Division>  divisions;

        core::WindchestId addWindchest(std::string name, float pressurePa, bool tremulant)
        {
            // The id is taken BEFORE the move, not read back out of the moved-from
            // object afterwards. Reading w.id after std::move(w) happens to work
            // today -- Windchest's implicit move copies its POD members and only
            // moves the string -- and stops working, silently, the day Windchest
            // gains a move constructor of its own that clears them.
            const auto id = core::WindchestId{ static_cast<std::uint16_t>(windchests.size()) };

            Windchest w;
            w.id                = id;
            w.name              = std::move(name);
            w.nominalPressurePa = pressurePa;
            w.hasTremulant      = tremulant;
            windchests.push_back(std::move(w));
            return id;
        }

        /// @return The new division's id.
        ///
        /// Deliberately an ID, not a reference. Returning `divisions.back()` was
        /// only safe because the caller happened to reserve() exactly the number
        /// of divisions it then added; adding one more would have reallocated the
        /// vector and left every previously handed-out reference dangling, with
        /// no diagnostic. An id cannot dangle.
        core::DivisionId addDivision(std::string name, DivisionKind kind,
                                     core::MidiNote low, core::MidiNote high,
                                     bool enclosed, bool tremulant, core::WindchestId chest)
        {
            Division d;
            const auto id = core::DivisionId{ static_cast<std::uint16_t>(divisions.size()) };
            d.setId(id);
            d.setName(std::move(name));
            d.setKind(kind);
            d.setCompass(low, high);
            d.setEnclosed(enclosed);
            d.setHasTremulant(tremulant);
            d.addWindchest(chest);
            divisions.push_back(std::move(d));
            return id;
        }

        /// Look a division up by id. Always valid; never a stale reference.
        [[nodiscard]] Division& division(core::DivisionId id)
        {
            return divisions[id.value];
        }

        // Create one rank + one stop; wire the stop into its division. Returns StopId.
        core::StopId addStop(core::DivisionId    divisionId,
                             std::string         name,
                             core::TonalFamily   family,
                             core::Footage       footage,
                             core::PitchClass    pitchClass,
                             core::ChorusRole    role,
                             core::WindchestId   chest,
                             core::MidiNote      low,
                             core::MidiNote      high,
                             std::vector<core::Footage> mixture = {})
        {
            const auto rankId = core::RankId{ static_cast<std::uint16_t>(ranks.size()) };
            const auto stopId = core::StopId{ static_cast<std::uint16_t>(stops.size()) };

            Rank rank;
            rank.setId(rankId);
            rank.setName(name);
            rank.setFamily(family);
            rank.setEngine(core::EngineKind::Additive);
            rank.setFootage(footage);
            rank.setWindchest(chest);
            rank.setCompass(low, high);
            rank.setVoicing(voicingForFamily(family));
            rank.generatePipes();
            ranks.push_back(std::move(rank));

            // Record the rank on its feeding chest.
            windchests[chest.value].ranks.push_back(rankId);

            Stop stop;
            stop.setId(stopId);
            stop.setName(std::move(name));
            stop.setFamily(family);
            stop.setFootage(footage);
            stop.setPitchClass(pitchClass);
            stop.setRole(role);
            stop.setDivision(divisionId);
            stop.setRank(rankId);
            if (!mixture.empty())
                stop.setMixtureComposition(std::move(mixture));
            stops.push_back(std::move(stop));

            division(divisionId).addStop(stopId);
            return stopId;
        }

        /// Second pass: teach every rank the division of the stop that draws it.
        ///
        /// addStop() happens to know the division and could stamp inline, but
        /// OrganLoader::compile cannot -- it builds every rank before any stop.
        /// Running the same finished-stop-list pass here keeps one rule in one
        /// shape, so a hand-built organ and a loaded one stamp PipeIds alike.
        void stampDivisions() noexcept
        {
            for (const Stop& s : stops)
            {
                const std::size_t rankIdx = s.rank().value;
                if (rankIdx < ranks.size())
                    ranks[rankIdx].stampDivision(s.division());
            }
        }
    };
} // namespace

Organ buildCaeciliaDemoOrgan()
{
    using core::TonalFamily;
    using core::PitchClass;
    using core::ChorusRole;
    namespace ft = core::footage;

    Builder b;
    // Reserve to avoid churn while assembling. Correctness no longer depends on
    // it: divisions are addressed by id, so a reallocation cannot dangle anything.
    b.windchests.reserve(3);
    b.divisions.reserve(3);
    b.ranks.reserve(26);
    b.stops.reserve(26);

    // --- Windchests (one per division) -------------------------------------
    const core::WindchestId chestPedal  = b.addWindchest("Pédale chest",     980.0f, false);
    const core::WindchestId chestGO      = b.addWindchest("Grand-Orgue chest", 812.0f, false);
    const core::WindchestId chestRecit  = b.addWindchest("Récit chest",       735.0f, true);

    // --- Divisions ---------------------------------------------------------
    const core::DivisionId pedal = b.addDivision("Pédale",      DivisionKind::Pedal,  36, 67, false, false, chestPedal);
    const core::DivisionId go     = b.addDivision("Grand-Orgue", DivisionKind::Manual, 36, 96, false, false, chestGO);
    const core::DivisionId recit  = b.addDivision("Récit",       DivisionKind::Manual, 36, 96, true,  true,  chestRecit);

    // === Pédale (6 stops) ==================================================
    b.addStop(pedal, "Contrebasse 16",  TonalFamily::Principal, ft::kSixteen, PitchClass::Sub,    ChorusRole::Foundation, chestPedal, 36, 67);
    b.addStop(pedal, "Soubasse 16",     TonalFamily::Flute,     ft::kSixteen, PitchClass::Sub,    ChorusRole::Foundation, chestPedal, 36, 67);
    b.addStop(pedal, "Octavebasse 8",   TonalFamily::Principal, ft::kEight,   PitchClass::Unison, ChorusRole::Foundation, chestPedal, 36, 67);
    b.addStop(pedal, "Bourdon 8",       TonalFamily::Flute,     ft::kEight,   PitchClass::Unison, ChorusRole::Foundation, chestPedal, 36, 67);
    b.addStop(pedal, "Bombarde 16",     TonalFamily::Reed,      ft::kSixteen, PitchClass::Sub,    ChorusRole::Foundation, chestPedal, 36, 67);
    b.addStop(pedal, "Trompette 8",     TonalFamily::Reed,      ft::kEight,   PitchClass::Unison, ChorusRole::Foundation, chestPedal, 36, 67);

    // === Grand-Orgue (12 stops) ===========================================
    b.addStop(go, "Montre 16",     TonalFamily::Principal, ft::kSixteen,         PitchClass::Sub,      ChorusRole::Foundation,   chestGO, 36, 96);
    b.addStop(go, "Montre 8",      TonalFamily::Principal, ft::kEight,           PitchClass::Unison,   ChorusRole::Foundation,   chestGO, 36, 96);
    b.addStop(go, "Bourdon 16",    TonalFamily::Flute,     ft::kSixteen,         PitchClass::Sub,      ChorusRole::Foundation,   chestGO, 36, 96);
    b.addStop(go, "Bourdon 8",     TonalFamily::Flute,     ft::kEight,           PitchClass::Unison,   ChorusRole::Foundation,   chestGO, 36, 96);
    b.addStop(go, "Prestant 4",    TonalFamily::Principal, ft::kFour,            PitchClass::Octave,   ChorusRole::Chorus,       chestGO, 36, 96);
    b.addStop(go, "Flûte 4",       TonalFamily::Flute,     ft::kFour,            PitchClass::Octave,   ChorusRole::Chorus,       chestGO, 36, 96);
    b.addStop(go, "Nazard 2 2/3",  TonalFamily::Flute,     ft::kTwoAndTwoThird,  PitchClass::Mutation, ChorusRole::Color,        chestGO, 36, 96);
    b.addStop(go, "Doublette 2",   TonalFamily::Principal, ft::kTwo,             PitchClass::Super,    ChorusRole::Chorus,       chestGO, 36, 96);
    b.addStop(go, "Tierce 1 3/5",  TonalFamily::Flute,     ft::kOneAndThreeFifth,PitchClass::Mutation, ChorusRole::Color,        chestGO, 36, 96);
    b.addStop(go, "Fourniture IV", TonalFamily::Mixture,   ft::kTwo,             PitchClass::Compound, ChorusRole::MixtureCrown, chestGO, 36, 96,
              { ft::kTwo, ft::kOneAndThird, ft::kOne, core::Footage{ 2, 3 } });
    b.addStop(go, "Trompette 8",   TonalFamily::Reed,      ft::kEight,           PitchClass::Unison,   ChorusRole::Foundation,   chestGO, 36, 96);
    b.addStop(go, "Clairon 4",     TonalFamily::Reed,      ft::kFour,            PitchClass::Octave,   ChorusRole::Chorus,       chestGO, 36, 96);

    // === Récit (enclosed, 8 stops) =========================================
    b.addStop(recit, "Bourdon 8",       TonalFamily::Flute,     ft::kEight, PitchClass::Unison,   ChorusRole::Foundation,   chestRecit, 36, 96);
    b.addStop(recit, "Flûte octaviante 4", TonalFamily::Flute,  ft::kFour,  PitchClass::Octave,   ChorusRole::Chorus,       chestRecit, 36, 96);
    b.addStop(recit, "Gambe 8",         TonalFamily::String,    ft::kEight, PitchClass::Unison,   ChorusRole::Foundation,   chestRecit, 36, 96);
    b.addStop(recit, "Voix Céleste 8",  TonalFamily::String,    ft::kEight, PitchClass::Unison,   ChorusRole::Color,        chestRecit, 36, 96);
    b.addStop(recit, "Plein Jeu III",   TonalFamily::Mixture,   ft::kTwo,   PitchClass::Compound, ChorusRole::MixtureCrown, chestRecit, 36, 96,
              { ft::kTwo, ft::kOneAndThird, ft::kOne });
    b.addStop(recit, "Basson-Hautbois 8", TonalFamily::Reed,    ft::kEight, PitchClass::Unison,   ChorusRole::Color,        chestRecit, 36, 96);
    b.addStop(recit, "Trompette 8",     TonalFamily::Reed,      ft::kEight, PitchClass::Unison,   ChorusRole::Foundation,   chestRecit, 36, 96);
    b.addStop(recit, "Clairon 4",       TonalFamily::Reed,      ft::kFour,  PitchClass::Octave,   ChorusRole::Chorus,       chestRecit, 36, 96);

    // Now that every stop exists, tell each rank which division owns it and stamp
    // that onto its pipes. Order matters: ranks -> stops -> stamp -> couplers.
    // A rank cannot know its division before the stops are built, and a coupler
    // resolved before the stamp would carry PipeIds that all claim division 0.
    b.stampDivisions();

    // --- Manuals (console keyboards) ---------------------------------------
    std::vector<Manual> manuals;
    manuals.push_back(Manual{ go,    0, 0, 36, 96 });    // lower manual: Grand-Orgue
    manuals.push_back(Manual{ recit, 1, 1, 36, 96 });    // upper manual: Récit
    manuals.push_back(Manual{ pedal, 2, 2, 36, 67 });    // pedalboard

    // --- Couplers ----------------------------------------------------------
    std::vector<Coupler> couplers;
    auto addCoupler = [&](std::string name, core::DivisionId from, core::DivisionId to, std::int32_t shift)
    {
        Coupler c;
        c.setId(CouplerId{ static_cast<std::uint16_t>(couplers.size()) });
        c.setName(std::move(name));
        c.setFrom(from);
        c.setTo(to);
        c.setOctaveShift(shift);
        c.setKind(CouplerKind::InterManual);
        couplers.push_back(std::move(c));
    };
    addCoupler("Récit/Grand-Orgue", recit, go,    0);
    addCoupler("Grand-Orgue/Pédale", go,    pedal, 0);
    addCoupler("Récit/Pédale",       recit, pedal, 0);

    // --- Assemble the immutable organ --------------------------------------
    Organ organ;
    organ.setName("Caecilia Demonstration Organ");
    organ.setBuilder("Alesson Queiroz");
    organ.setYear(2026);
    organ.setWindchests(std::move(b.windchests));
    organ.setRanks(std::move(b.ranks));
    organ.setStops(std::move(b.stops));
    organ.setDivisions(std::move(b.divisions));
    organ.setManuals(std::move(manuals));
    organ.setCouplers(std::move(couplers));
    return organ;
}

// ---------------------------------------------------------------------------
// Registration -> ready-to-bind voices.
// ---------------------------------------------------------------------------

DemoVoiceBank buildDemoRegistrationVoices(const Organ&                  organ,
                                          std::span<const core::StopId> engagedStops,
                                          const core::IWindSupply*      wind,
                                          const core::ITuning*          tuning,
                                          core::SampleRate              sampleRate,
                                          std::size_t                   maxBlockFrames,
                                          std::size_t                   poolCapacity)
{
    DemoVoiceBank bank;
    if (poolCapacity == 0 || engagedStops.empty())
        return bank;

    // Precompute, once per engaged stop, its spectral seed and render context.
    struct Entry
    {
        synth::SpectralModel model;
        synth::VoiceContext  context;
    };
    std::vector<Entry> entries;
    entries.reserve(engagedStops.size());

    for (const core::StopId stopId : engagedStops)
    {
        const Stop* stop = organ.stop(stopId);
        if (stop == nullptr)
            continue;
        const Rank* rank = organ.rank(stop->rank());
        if (rank == nullptr)
            continue;

        Entry e;
        e.model = spectralModelForStop(*stop);

        // Mixtures and mutations encode their pitch content in the partial
        // ratios relative to 8' unison, so the voice must sound at unison.
        const bool unisonBase = (stop->family() == core::TonalFamily::Mixture)
                             || stop->isCompound()
                             || stop->isMutation();

        e.context.wind    = wind;
        e.context.tuning  = tuning;
        e.context.chest   = rank->windchest();
        e.context.footage = unisonBase ? core::footage::kEight : stop->footage();
        e.context.family  = stop->family();
        e.context.rank    = rank->id();

        entries.push_back(std::move(e));
    }

    if (entries.empty())
        return bank;

    // Distribute the pool capacity round-robin across the engaged stops.
    bank.voices.reserve(poolCapacity);
    bank.pointers.reserve(poolCapacity);
    for (std::size_t i = 0; i < poolCapacity; ++i)
    {
        const Entry& e = entries[i % entries.size()];

        auto voice = std::make_unique<synth::AdditiveVoice>();
        voice->prepare(sampleRate, maxBlockFrames);
        voice->setContext(e.context);
        voice->seedFrom(e.model);

        bank.pointers.push_back(voice.get());
        bank.voices.push_back(std::move(voice));
    }

    return bank;
}

} // namespace caecilia::model
