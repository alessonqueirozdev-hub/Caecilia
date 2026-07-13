/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

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

    /// Append one partial to a model.
    void appendPartial(synth::SpectralModel& model,
                       float ratio,
                       float ampDb,
                       float windSensitivity,
                       float onsetSeconds,
                       float brightnessTrack,
                       float phase = 0.0f)
    {
        synth::PartialTrack t;
        t.ratioToF0       = ratio;
        t.ampDb           = ampDb;
        t.phase           = phase;
        t.windSensitivity = windSensitivity;
        t.onsetSeconds    = onsetSeconds;
        t.brightnessTrack = brightnessTrack;
        model.partials.push_back(t);
    }
} // namespace

// ---------------------------------------------------------------------------
// Spectral recipes.
// ---------------------------------------------------------------------------

synth::SpectralModel makeSpectralPrincipal(core::Footage /*footage*/,
                                           float         brightness,
                                           std::size_t   numHarmonics)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    // Brighter voices roll off more slowly. rolloff ~1.0 => -6 dB/octave.
    const float rolloff = clampf(1.35f - 0.35f * clampf(brightness, 0.0f, 2.0f), 0.55f, 1.6f);

    const std::size_t harmonics = numHarmonics < 1 ? 1 : numHarmonics;
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

synth::SpectralModel makeSpectralFlute(core::Footage /*footage*/,
                                       float         brightness,
                                       bool          stopped)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    // Strong, pure fundamental — the body of a flute/bourdon.
    appendPartial(model, 1.0f, 0.0f, 0.20f, 0.0f, 0.01f);

    if (stopped)
    {
        // Stopped pipes emphasise odd harmonics; all weak.
        const int odd[] = { 3, 5, 7, 9 };
        for (int idx = 0; idx < 4; ++idx)
        {
            const float n     = static_cast<float>(odd[idx]);
            const float ampDb = -16.0f - 6.0f * static_cast<float>(idx) + 3.0f * (brightness - 1.0f);
            appendPartial(model, n, ampDb, 0.18f, 0.002f * static_cast<float>(idx), 0.02f);
        }
    }
    else
    {
        // Open flute: a touch of the low even harmonics too, still gentle.
        appendPartial(model, 2.0f, -14.0f + 3.0f * (brightness - 1.0f), 0.18f, 0.001f, 0.02f);
        appendPartial(model, 3.0f, -22.0f, 0.18f, 0.002f, 0.02f);
        appendPartial(model, 4.0f, -30.0f, 0.18f, 0.003f, 0.02f);
    }
    return model;
}

synth::SpectralModel makeSpectralString(core::Footage /*footage*/,
                                        float         detuneCents,
                                        float         brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    constexpr std::size_t kHarmonics = 16;
    const float rolloff  = clampf(0.70f - 0.10f * (brightness - 1.0f), 0.40f, 1.0f); // slow => bright uppers
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

synth::SpectralModel makeSpectralReed(core::Footage /*footage*/,
                                      float         formantCenterHz,
                                      float         brightness)
{
    synth::SpectralModel model;
    model.fundamentalHz = kNominalFundamentalHz;

    constexpr std::size_t kHarmonics = 16;
    const float rolloff = clampf(0.90f - 0.10f * (brightness - 1.0f), 0.50f, 1.2f);

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

    // Describe the reed's formant for downstream consumers (additive path reads
    // only the partials above, but the envelope documents the voicing intent).
    model.steadyFormants.peaks[0] = { formantCenterHz, 6.0f, formantCenterHz * 0.5f };
    model.steadyFormants.peakCount = 1;
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
        appendPartial(model, ratio, ampDb, 0.45f, 0.001f * static_cast<float>(idx), 0.05f);
        // The octave above each rank pitch adds the characteristic mixture shimmer.
        appendPartial(model, ratio * 2.0f, ampDb - 10.0f, 0.50f, 0.002f * static_cast<float>(idx), 0.06f);
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
        appendPartial(model, ratio,        0.0f + brightBoost, 0.30f, 0.002f, 0.02f);
        appendPartial(model, ratio * 2.0f, -14.0f,             0.30f, 0.004f, 0.03f);
        appendPartial(model, ratio * 3.0f, -22.0f,             0.35f, 0.006f, 0.04f);
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

/// Loudest a seeded voice is ever normalised to. A full TUTTI caps here; the
/// soft-clip downstream keeps even this from saturating. Matches the level users
/// are already comfortable with, so the plenum/tutti loudness is unchanged.
constexpr double kMaxCompositeEnergy = 0.32;
/// Quietest a seeded voice is normalised to — a single soft flute/string for
/// psalmody or plainchant accompaniment sits here (~10 dB below the tutti), which
/// is what gives the console its soft-to-imposing dynamic range.
constexpr double kMinCompositeEnergy = 0.10;

/// Per-rank loudness weight: how much a drawn rank adds to the overall level.
/// Foundations pull the tone up; soft flutes/strings barely raise it; reeds and
/// mixtures are louder. This is what makes a Bourdon-only registration soft and a
/// full plenum loud, instead of every registration being the same volume.
[[nodiscard]] double rankLoudnessWeight(core::TonalFamily family,
                                        core::Footage     footage,
                                        bool              unisonReferenced) noexcept
{
    double base;
    if (unisonReferenced)                 base = 0.85;   // mixtures / mutations
    else switch (family)
    {
        case core::TonalFamily::Principal: base = 1.00; break;
        case core::TonalFamily::Flute:     base = 0.65; break;  // soft, dark
        case core::TonalFamily::String:    base = 0.55; break;  // softest
        case core::TonalFamily::Reed:      base = 1.35; break;  // loud
        case core::TonalFamily::Mixture:   base = 1.10; break;
        default:                           base = 0.80; break;
    }
    // Footage tilt: 16' adds gravity, higher pitches contribute a little less.
    const double feet = footage.feet();
    double ftFactor = 1.0;
    if      (feet >= 16.0) ftFactor = 1.10;
    else if (feet >= 8.0)  ftFactor = 1.00;
    else if (feet >= 4.0)  ftFactor = 0.85;
    else if (feet >= 2.0)  ftFactor = 0.70;
    else                   ftFactor = 0.55;
    return base * ftFactor;
}

/// Map a summed registration weight to a target RMS energy (soft -> loud), so
/// dynamics track the registration instead of being flattened to one level.
[[nodiscard]] double loudnessTargetForWeight(double weight) noexcept
{
    const double t = 0.13 * std::sqrt(weight > 0.0 ? weight : 0.0);
    return t < kMinCompositeEnergy ? kMinCompositeEnergy
         : (t > kMaxCompositeEnergy ? kMaxCompositeEnergy : t);
}

/// Scale every partial so sqrt(sum(amp^2)) == targetEnergy. Off-thread.
void normalizeComposite(synth::SpectralModel& c, double targetEnergy) noexcept
{
    double energy = 0.0;
    for (const synth::PartialTrack& p : c.partials)
    {
        const double a = std::pow(10.0, static_cast<double>(p.ampDb) / 20.0);
        energy += a * a;
    }
    if (energy <= 1.0e-9)
        return;
    const float scaleDb = 20.0f * std::log10(static_cast<float>(targetEnergy / std::sqrt(energy)));
    for (synth::PartialTrack& p : c.partials)
        p.ampDb += scaleDb;
}

/// Break the sterile phase-coherence of a pure additive stack: give each partial a
/// deterministic pseudo-random start phase and a few cents of detune. Real pipes
/// are never perfectly harmonic or in phase — this alone pulls the tone away from
/// the "electronic organ" sound. Deterministic (index-hashed) so it is stable.
void humanizeComposite(synth::SpectralModel& c) noexcept
{
    std::uint32_t h = 0x9E3779B9u;
    std::size_t i = 0;
    for (synth::PartialTrack& p : c.partials)
    {
        h ^= static_cast<std::uint32_t>(i * 2654435761u);
        h = (h << 13) | (h >> 19);
        h *= 0x85EBCA6Bu;
        const float r1 = static_cast<float>(h & 0xFFFFu) / 65535.0f;         // 0..1
        const float r2 = static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f; // 0..1
        p.phase = r1 * 6.2831853f;                                            // random start phase
        const float cents = (r2 - 0.5f) * 7.0f;                               // +/- 3.5 cents
        p.ratioToF0 *= std::pow(2.0f, cents / 1200.0f);
        ++i;
    }
}
} // namespace

synth::SpectralModel buildRegistrationCompositeSpectrum(
    const Organ& organ, std::span<const core::StopId> engagedStops)
{
    synth::SpectralModel composite;
    if (engagedStops.empty())
        return composite;

    // Gentle global trim so stacking more ranks never clips the master bus.
    const float stackTrimDb = -5.0f * std::log10(static_cast<float>(engagedStops.size()));

    double loudnessWeight = 0.0;
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
        loudnessWeight += rankLoudnessWeight(stop->family(), stop->footage(), unisonReferenced);
        const double feet = stop->footage().feet();
        const double fold = (unisonReferenced || feet <= 0.0) ? 1.0 : 8.0 / feet;
        const float  gainDb = compositeRankGainDb(*stop) + stackTrimDb;

        const synth::SpectralModel recipe = spectralModelForStop(*stop);
        for (const synth::PartialTrack& p : recipe.partials)
        {
            synth::PartialTrack t = p;
            t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * fold);
            t.ampDb     = p.ampDb + gainDb;
            composite.partials.push_back(t);
        }
    }

    humanizeComposite(composite);
    normalizeComposite(composite, loudnessTargetForWeight(loudnessWeight));
    composite.fundamentalHz = 0.0f; // set per note by the voice at noteOn
    return composite;
}

core::Footage footageFromFeet(double feet) noexcept
{
    struct Std { double feet; core::Footage f; };
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

    const float stackTrimDb = -5.0f * std::log10(static_cast<float>(ranks.size()));

    double loudnessWeight = 0.0;
    for (const RegistrationRank& r : ranks)
    {
        const bool unisonReferenced = r.compound
                                   || r.family == core::TonalFamily::Mixture
                                   || r.footage.isMutation();
        loudnessWeight += rankLoudnessWeight(r.family, r.footage, unisonReferenced);
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
        const float gainDb = base + octDark + stackTrimDb;

        const synth::SpectralModel recipe = recipeForRank(r.family, r.footage, r.compound);
        for (const synth::PartialTrack& p : recipe.partials)
        {
            synth::PartialTrack t = p;
            t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * fold);
            t.ampDb     = p.ampDb + gainDb;
            composite.partials.push_back(t);
        }
    }

    humanizeComposite(composite);
    normalizeComposite(composite, loudnessTargetForWeight(loudnessWeight));
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
            Windchest w;
            w.id                = core::WindchestId{ static_cast<std::uint16_t>(windchests.size()) };
            w.name              = std::move(name);
            w.nominalPressurePa = pressurePa;
            w.hasTremulant      = tremulant;
            windchests.push_back(std::move(w));
            return w.id;
        }

        Division& addDivision(std::string name, DivisionKind kind, core::MidiNote low, core::MidiNote high,
                              bool enclosed, bool tremulant, core::WindchestId chest)
        {
            Division d;
            d.setId(core::DivisionId{ static_cast<std::uint16_t>(divisions.size()) });
            d.setName(std::move(name));
            d.setKind(kind);
            d.setCompass(low, high);
            d.setEnclosed(enclosed);
            d.setHasTremulant(tremulant);
            d.addWindchest(chest);
            divisions.push_back(std::move(d));
            return divisions.back();
        }

        // Create one rank + one stop; wire the stop into its division. Returns StopId.
        core::StopId addStop(Division&           division,
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
            stop.setDivision(division.id());
            stop.setRank(rankId);
            if (!mixture.empty())
                stop.setMixtureComposition(std::move(mixture));
            stops.push_back(std::move(stop));

            division.addStop(stopId);
            return stopId;
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
    // Reserve so the Division& references handed to addStop stay valid across the
    // three addDivision() calls (a vector re-alloc would dangle them), and so the
    // rank/stop vectors do not churn while assembling.
    b.windchests.reserve(3);
    b.divisions.reserve(3);
    b.ranks.reserve(26);
    b.stops.reserve(26);

    // --- Windchests (one per division) -------------------------------------
    const core::WindchestId chestPedal  = b.addWindchest("Pédale chest",     980.0f, false);
    const core::WindchestId chestGO      = b.addWindchest("Grand-Orgue chest", 812.0f, false);
    const core::WindchestId chestRecit  = b.addWindchest("Récit chest",       735.0f, true);

    // --- Divisions ---------------------------------------------------------
    Division& pedal = b.addDivision("Pédale",      DivisionKind::Pedal,  36, 67, false, false, chestPedal);
    Division& go     = b.addDivision("Grand-Orgue", DivisionKind::Manual, 36, 96, false, false, chestGO);
    Division& recit  = b.addDivision("Récit",       DivisionKind::Manual, 36, 96, true,  true,  chestRecit);

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

    // --- Manuals (console keyboards) ---------------------------------------
    std::vector<Manual> manuals;
    manuals.push_back(Manual{ go.id(),    0, 0, 36, 96 });    // lower manual: Grand-Orgue
    manuals.push_back(Manual{ recit.id(), 1, 1, 36, 96 });    // upper manual: Récit
    manuals.push_back(Manual{ pedal.id(), 2, 2, 36, 67 });    // pedalboard

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
    addCoupler("Récit/Grand-Orgue", recit.id(), go.id(),    0);
    addCoupler("Grand-Orgue/Pédale", go.id(),    pedal.id(), 0);
    addCoupler("Récit/Pédale",       recit.id(), pedal.id(), 0);

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
