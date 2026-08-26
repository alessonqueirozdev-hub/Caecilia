// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The check that has to pass BEFORE one voice per rank replaces one voice per
// note, and the only automated one that can say the migration did not change the
// instrument.
//
// It is stronger than a level comparison, because of one property of the voicing
// path: scaleComposite applies a FIXED factor, independent of the registration.
// That was done so drawing more stops genuinely adds energy — it replaced an
// automatic gain control that squeezed the whole range from one soft flute to
// full Tutti into about 13 dB — and the consequence here is that the sum of the
// ranks' voicings IS the composite, partial for partial. So this asserts
// structural equality rather than agreement within a tolerance.
//
// If that ever stops holding, the per-rank instrument is a different instrument
// from the composite one, and this is what says so.
//

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/core/AudioBlock.h"
#include "caecilia/synthesis/VoiceContext.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
/// Every partial of a spectrum as (ratio, amplitude in dB), sorted, so two
/// spectra can be compared without depending on the order ranks were visited in.
std::vector<std::pair<double, double>> shapeOf(const synth::SpectralModel& m)
{
    std::vector<std::pair<double, double>> out;
    out.reserve(m.partials.size());
    for (const synth::PartialTrack& p : m.partials)
        out.emplace_back(static_cast<double>(p.ratioToF0), static_cast<double>(p.ampDb));
    std::sort(out.begin(), out.end());
    return out;
}

/// Total linear energy, which is what a level comparison would measure.
double energyOf(const synth::SpectralModel& m)
{
    double e = 0.0;
    for (const synth::PartialTrack& p : m.partials)
    {
        const double a = std::pow(10.0, static_cast<double>(p.ampDb) / 20.0);
        e += a * a;
    }
    return e;
}
} // namespace

TEST_CASE("The ranks of a registration sum to its composite", "[model][perrank]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // A real plenum: principals across the compass, a flute foundation, a mixture.
    std::vector<core::StopId> drawn = model::defaultOpeningRegistration(organ);
    REQUIRE(drawn.size() >= 4);

    const synth::SpectralModel composite =
        model::buildRegistrationCompositeSpectrum(organ, drawn);

    synth::SpectralModel summed;
    for (const core::StopId id : drawn)
    {
        const model::RankVoicing v = model::buildRankVoicing(organ, id);
        for (const synth::PartialTrack& p : v.spectrum.partials)
            summed.partials.push_back(p);
    }

    INFO(composite.partials.size() << " composite partials vs "
                                  << summed.partials.size() << " summed");
    REQUIRE(summed.partials.size() == composite.partials.size());

    const auto a = shapeOf(composite);
    const auto b = shapeOf(summed);

    double worstRatio = 0.0;
    double worstAmp   = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        worstRatio = std::max(worstRatio, std::abs(a[i].first - b[i].first));
        worstAmp   = std::max(worstAmp, std::abs(a[i].second - b[i].second));
    }

    INFO("worst ratio delta " << worstRatio << ", worst amp delta " << worstAmp << " dB");
    CHECK(worstRatio < 1.0e-6);
    CHECK(worstAmp   < 1.0e-4);
}

TEST_CASE("Every rank of the instrument sums to the tutti composite",
          "[model][perrank]")
{
    // The worst case, and the one where a per-rank calibration error would show up
    // as the whole instrument being tens of dB out.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<core::StopId> all;
    for (const model::Stop& s : organ.stops())
        all.push_back(s.id());

    const synth::SpectralModel composite =
        model::buildRegistrationCompositeSpectrum(organ, all);

    double summedEnergy = 0.0;
    std::size_t summedPartials = 0;
    for (const core::StopId id : all)
    {
        const model::RankVoicing v = model::buildRankVoicing(organ, id);
        summedEnergy += energyOf(v.spectrum);
        summedPartials += v.spectrum.partials.size();
    }

    REQUIRE(summedPartials == composite.partials.size());
    const double compositeEnergy = energyOf(composite);
    REQUIRE(compositeEnergy > 0.0);

    const double db = 10.0 * std::log10(summedEnergy / compositeEnergy);
    INFO("per-rank sum is " << db << " dB from the composite");
    CHECK(std::abs(db) < 0.01);
}

TEST_CASE("A rank's voicing fits the storage a per-rank voice reserves",
          "[model][perrank][guardrail]")
{
    // A per-rank voice reserves RankVoicing::kMaxPartials once, at prepare time,
    // and seeding is allocation-free on the assumption that nothing exceeds it.
    // The day a recipe grows past this, that assumption becomes a silent truncation
    // — or, worse, an allocation on the audio thread.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::size_t worst = 0;
    for (const model::Stop& s : organ.stops())
    {
        const model::RankVoicing v = model::buildRankVoicing(organ, s.id());
        INFO(s.name() << ": " << v.spectrum.partials.size() << " partials");
        CHECK(v.spectrum.partials.size() <= model::RankVoicing::kMaxPartials);
        worst = std::max(worst, v.spectrum.partials.size());
    }
    INFO("largest rank has " << worst << " partials against a reserve of "
                             << model::RankVoicing::kMaxPartials);
    CHECK(worst > 0);
}

TEST_CASE("An unknown stop yields an empty voicing rather than a guess",
          "[model][perrank]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const model::RankVoicing v = model::buildRankVoicing(organ, core::StopId{ 999 });
    CHECK(v.spectrum.partials.empty());
}

TEST_CASE("Speech timing separates the families", "[model][perrank][speech]")
{
    // PartialBank::setSpeechProfile has no callers today, so every rank runs the
    // struct defaults and the instrument's speech is per-PITCH only: a Gambe and a
    // Trompette start at exactly the same moment, which they emphatically do not
    // on a real organ. These are the numbers that fix that.
    const core::Footage eight = core::footage::kEight;

    const auto reed      = model::speechProfileFor(core::TonalFamily::Reed, eight);
    const auto principal = model::speechProfileFor(core::TonalFamily::Principal, eight);
    const auto flute     = model::speechProfileFor(core::TonalFamily::Flute, eight);
    const auto string    = model::speechProfileFor(core::TonalFamily::String, eight);
    const auto mixture   = model::speechProfileFor(core::TonalFamily::Mixture, eight);

    // A tongue starts as soon as there is wind; a narrow string is the hardest
    // thing on the instrument to get speaking.
    CHECK(reed.attackAtC2Sec < principal.attackAtC2Sec);
    CHECK(principal.attackAtC2Sec < flute.attackAtC2Sec);
    CHECK(flute.attackAtC2Sec < string.attackAtC2Sec);
    CHECK(mixture.attackAtC2Sec < principal.attackAtC2Sec);

    // The spread is musically meaningful, not a rounding difference.
    INFO("string/reed attack ratio " << string.attackAtC2Sec / reed.attackAtC2Sec);
    CHECK(string.attackAtC2Sec > reed.attackAtC2Sec * 3.0f);

    // Treble still speaks faster than bass within every family.
    for (const auto& p : { reed, principal, flute, string, mixture })
    {
        CHECK(p.attackAtC7Sec < p.attackAtC2Sec);
        CHECK(p.releaseAtC7Sec < p.releaseAtC2Sec);
    }
}

TEST_CASE("A longer pipe of the same family speaks more slowly",
          "[model][perrank][speech]")
{
    // Length dominates within a family as well as across the compass: a 16'
    // Bourdon has far more air to move than a 2' of identical construction.
    const auto big   = model::speechProfileFor(core::TonalFamily::Flute,
                                               core::footage::kSixteen);
    const auto small = model::speechProfileFor(core::TonalFamily::Flute,
                                               core::footage::kTwo);

    INFO(big.attackAtC2Sec << " vs " << small.attackAtC2Sec);
    CHECK(big.attackAtC2Sec > small.attackAtC2Sec);
    CHECK(big.releaseAtC2Sec > small.releaseAtC2Sec);
}


TEST_CASE("Per-rank voices render at the same level as the composite",
          "[model][perrank][regression]")
{
    // The spectra being identical is not the same claim as the SOUND being
    // identical, and the gap between them is where the double transposition lived:
    // a rank's partial ratios are already referenced to 8' unison, so handing the
    // voice the rank's own footage transposed every one of them a second time. The
    // 16' ranks sounded an octave low and the mixtures were pitched past Nyquist
    // and silenced -- with the right partials, the right amplitudes, and every
    // spectral test passing.
    //
    // Only a rendered comparison sees it.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<core::StopId> drawn;
    for (const model::Stop& s : organ.stops())
        if (s.division().value == 1)
            drawn.push_back(s.id());
    REQUIRE(drawn.size() >= 6);

    const synth::SpectralModel composite =
        model::buildRegistrationCompositeSpectrum(organ, drawn);

    constexpr core::SampleRate kSr    = 48000.0;
    constexpr std::size_t      kBlock = 512;
    constexpr std::size_t      kTotal = 48000 * 3;

    const auto rmsOf = [&](std::vector<std::unique_ptr<synth::AdditiveVoice>>& vs)
    {
        std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
        float* chans[2] = { l.data(), r.data() };
        double sumSq = 0.0;
        std::size_t n = 0;
        for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            core::AudioBlock b(chans, 2, kBlock);
            for (auto& v : vs)
                v->renderAdd(b);
            if (pos > kTotal / 3) // past the attack
                for (std::size_t i = 0; i < kBlock; ++i)
                {
                    sumSq += static_cast<double>(l[i]) * l[i]
                           + static_cast<double>(r[i]) * r[i];
                    n += 2;
                }
        }
        return n > 0 ? std::sqrt(sumSq / static_cast<double>(n)) : 0.0;
    };

    std::vector<std::unique_ptr<synth::AdditiveVoice>> compVoices;
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(composite.partials.size());
        v->prepare(kSr, kBlock);
        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;
        v->setContext(ctx);
        v->seedFrom(composite);
        v->noteOn(core::PipeId{ 0, 60, 1 }, 100);
        compVoices.push_back(std::move(v));
    }

    std::vector<std::unique_ptr<synth::AdditiveVoice>> rankVoices;
    std::vector<synth::RankVoicing> voicings;
    for (const core::StopId id : drawn)
        voicings.push_back(model::buildRankVoicing(organ, id));
    for (const synth::RankVoicing& rv : voicings)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        v->prepare(kSr, kBlock);
        v->adoptRank(&rv);
        v->noteOn(core::PipeId{ static_cast<std::uint16_t>(rv.stop.value), 60, 1 }, 100);
        rankVoices.push_back(std::move(v));
    }

    const double a = rmsOf(compVoices);
    const double b = rmsOf(rankVoices);
    REQUIRE(a > 0.0);
    REQUIRE(b > 0.0);

    const double db = 20.0 * std::log10(b / a);
    INFO("per-rank is " << db << " dB from the composite");
    // Two genuinely different summation topologies -- one voice of many partials
    // against many voices of few -- so they beat differently and will never agree
    // to the last decimal. Anything past a couple of dB is a defect, not a beat.
    CHECK(std::abs(db) < 2.0);
}

TEST_CASE("Two identical ranks do not sum in phase",
          "[model][perrank][regression]")
{
    // This organ has four Reed 8' ranks and three Flute 8'. Keyed on (family,
    // footage) alone they received identical partial seeds, therefore identical
    // start phases and identical detune, therefore summed COHERENTLY: four ranks at
    // four times amplitude where four separate ranks of pipes give twice.
    //
    // Measured at 5.9 dB on a full Tutti before the rank salt existed. It is the
    // tutti hotspot the per-VOICE decorrelation was written to fix and structurally
    // could not, because the salt was per voice and the seed it salted was already
    // the same.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Two of the same thing, then four.
    const auto energyFor = [&organ](std::size_t copies)
    {
        std::vector<model::RegistrationRank> ranks(
            copies, model::RegistrationRank{ core::TonalFamily::Reed,
                                             core::footage::kEight, false });
        const synth::SpectralModel m = model::buildCompositeFromRegistration(ranks);
        double e = 0.0;
        for (const synth::PartialTrack& p : m.partials)
        {
            const double amp = std::pow(10.0, static_cast<double>(p.ampDb) / 20.0);
            e += amp * amp;
        }
        return e;
    };

    // Energy is what an incoherent sum adds, and the spectrum is the same either
    // way -- so this alone cannot tell coherent from incoherent. What CAN is
    // whether the duplicates were given different identities at all.
    const auto phasesFor = [&organ](std::size_t copies)
    {
        std::vector<model::RegistrationRank> ranks(
            copies, model::RegistrationRank{ core::TonalFamily::Reed,
                                             core::footage::kEight, false });
        const synth::SpectralModel m = model::buildCompositeFromRegistration(ranks);
        std::vector<std::pair<float, std::uint32_t>> ident;
        for (const synth::PartialTrack& p : m.partials)
            ident.emplace_back(p.phase, p.seed);
        return ident;
    };

    const auto one = phasesFor(1);
    const auto two = phasesFor(2);
    REQUIRE(two.size() == one.size() * 2);

    // The second copy must differ from the first in BOTH phase and seed. Identical
    // there is exactly what made them sum in phase.
    std::size_t identical = 0;
    for (std::size_t i = 0; i < one.size(); ++i)
        if (two[i].first == two[i + one.size()].first
            && two[i].second == two[i + one.size()].second)
            ++identical;

    INFO(identical << " of " << one.size() << " partials shared an identity");
    CHECK(identical == 0);

    // And the ranks are detuned from one another, so they beat rather than
    // reinforcing: two ranks at literally the same pitch are one loud rank.
    bool anyPitchDifference = false;
    const synth::SpectralModel m = model::buildCompositeFromRegistration(
        std::vector<model::RegistrationRank>(
            2, model::RegistrationRank{ core::TonalFamily::Reed,
                                        core::footage::kEight, false }));
    for (std::size_t i = 0; i < m.partials.size() / 2; ++i)
        anyPitchDifference = anyPitchDifference
            || m.partials[i].ratioToF0 != m.partials[i + m.partials.size() / 2].ratioToF0;
    CHECK(anyPitchDifference);

    CHECK(energyFor(2) > energyFor(1)); // and drawing more still adds energy
}

TEST_CASE("The order stops are drawn in does not change their tuning",
          "[model][perrank][regression]")
{
    // The rank salt has to separate duplicates WITHOUT depending on the order they
    // arrive in. Salting by position in the list did the first and broke the
    // second, and an organist's registration would have sounded different
    // depending on which drawstop they happened to pull first.
    const std::vector<model::RegistrationRank> forwards{
        model::RegistrationRank{ core::TonalFamily::Principal, core::footage::kEight, false },
        model::RegistrationRank{ core::TonalFamily::Reed,      core::footage::kEight, false },
        model::RegistrationRank{ core::TonalFamily::Reed,      core::footage::kEight, false },
        model::RegistrationRank{ core::TonalFamily::Flute,     core::footage::kFour,  false },
    };
    std::vector<model::RegistrationRank> backwards(forwards.rbegin(), forwards.rend());

    const synth::SpectralModel a = model::buildCompositeFromRegistration(forwards);
    const synth::SpectralModel b = model::buildCompositeFromRegistration(backwards);

    REQUIRE(a.partials.size() == b.partials.size());

    std::vector<std::pair<double, double>> sa, sb;
    for (const synth::PartialTrack& p : a.partials)
        sa.emplace_back(static_cast<double>(p.ratioToF0), static_cast<double>(p.ampDb));
    for (const synth::PartialTrack& p : b.partials)
        sb.emplace_back(static_cast<double>(p.ratioToF0), static_cast<double>(p.ampDb));
    std::sort(sa.begin(), sa.end());
    std::sort(sb.begin(), sb.end());

    double worst = 0.0;
    for (std::size_t i = 0; i < sa.size(); ++i)
        worst = std::max(worst, std::abs(sa[i].first - sb[i].first));

    INFO("worst ratio difference between the two orders: " << worst);
    CHECK(worst < 1.0e-9);
}


namespace
{
/// Energy at one frequency, by Goertzel: a single-bin DFT, which is all this needs.
double goertzelPower(const std::vector<float>& x, double hz, double sr)
{
    const double w    = 2.0 * 3.14159265358979323846 * hz / sr;
    const double coef = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (const float v : x)
    {
        const double s0 = static_cast<double>(v) + coef * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coef * s1 * s2;
}

/// Energy in a narrow band, so a few cents of rank detune do not fall between bins.
double bandPower(const std::vector<float>& x, double hz, double sr)
{
    double e = 0.0;
    for (int k = -4; k <= 4; ++k)
        e += goertzelPower(x, hz + static_cast<double>(k), sr);
    return e;
}
} // namespace

TEST_CASE("A rank sounds at the pitch of the key, not the pitch of its footage",
          "[model][perrank][regression]")
{
    // A rank's partial ratios are ALREADY referenced to 8' unison: the 16' Montre's
    // fundamental is the ratio 0.5, not the ratio 1.0 of a 16' fundamental. So the
    // voice that plays them has to be told 8', and telling it the rank's own footage
    // transposes every partial a second time -- the 16' ranks an octave low, the
    // mixtures up past Nyquist and into silence.
    //
    // Every spectral test passes either way, because the partials are right; and
    // every LEVEL test passes either way, because transposition does not change
    // energy. This is the measurement that does not.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const model::Stop* montre = nullptr;
    for (const model::Stop& s : organ.stops())
        if (s.footage() == core::footage::kSixteen && s.division().value == 1)
        {
            montre = &s;
            break;
        }
    REQUIRE(montre != nullptr);

    const synth::RankVoicing rv = model::buildRankVoicing(organ, montre->id());
    REQUIRE(!rv.spectrum.partials.empty());
    REQUIRE(rv.footage.feet() == Approx(16.0));

    // The loudest partial, so the measurement has the best signal to work with.
    const synth::PartialTrack* strongest = &rv.spectrum.partials.front();
    for (const synth::PartialTrack& p : rv.spectrum.partials)
        if (p.ampDb > strongest->ampDb)
            strongest = &p;

    constexpr core::SampleRate kSr    = 48000.0;
    constexpr std::size_t      kBlock = 512;

    const core::PipeId pipe{ static_cast<std::uint16_t>(rv.stop.value), 60, 1 };

    // Where the partial belongs, and where the double transposition would put it.
    const double correct = static_cast<double>(strongest->ratioToF0)
                         * synth::defaultSoundingFrequencyHz(60, core::footage::kEight);
    const double wrong = static_cast<double>(strongest->ratioToF0)
                       * synth::defaultSoundingFrequencyHz(60, rv.footage);
    REQUIRE(correct > 20.0);
    REQUIRE(std::abs(correct - wrong) > 10.0); // the two really are distinguishable

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
    voice.prepare(kSr, kBlock);
    voice.adoptRank(&rv);
    voice.noteOn(pipe, 100);

    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    float* chans[2] = { l.data(), r.data() };

    for (int b = 0; b < 96; ++b) // ~1 s, past the speech
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        core::AudioBlock blk(chans, 2, kBlock);
        voice.renderAdd(blk);
    }

    std::vector<float> tail;
    tail.reserve(kBlock * 32);
    for (int b = 0; b < 32; ++b) // ~16k samples: bins fine enough, detune still inside
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        core::AudioBlock blk(chans, 2, kBlock);
        voice.renderAdd(blk);
        tail.insert(tail.end(), l.begin(), l.end());
    }

    double energy = 0.0;
    for (const float v : tail)
        energy += static_cast<double>(v) * v;
    REQUIRE(energy > 0.0); // it has to be sounding at all

    const double atCorrect = bandPower(tail, correct, kSr);
    const double atWrong   = bandPower(tail, wrong, kSr);

    INFO(montre->name() << " strongest partial expected at " << correct
                        << " Hz; a double transposition would put it at " << wrong
                        << " Hz. Measured " << atCorrect << " vs " << atWrong);
    CHECK(atCorrect > atWrong * 4.0);
}

TEST_CASE("Every rank speaks evenly across its compass",
          "[model][perrank][voicing]")
{
    // A rank is even from bass to treble. That is the first thing a voicer makes
    // true of a rank of pipes, and it is what an organist assumes when they draw
    // one: a Doublette is a Doublette at both ends of the keyboard.
    //
    // Additive synthesis does not give it for free. As a rank climbs, its partials
    // walk up into the anti-alias fade and out of the band, so the rank quietly
    // loses energy the higher it plays -- and the higher-pitched the rank, the
    // sooner it starts losing. Measured before this was compensated:
    //
    //     Montre 8'       MIDI 48 -36.2 dB -> MIDI 96 -36.8 dB   ( 0.6 dB)
    //     Doublette 2'            -45.5           -> -57.2       (11.8 dB)
    //     Fourniture IV           -53.6           -> -63.1       ( 9.5 dB)
    //
    // The Montre says what the intended behaviour is: its partials mostly stay in
    // band, so it comes out even without anyone doing anything. The upperwork is
    // the same instrument failing to.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    constexpr core::SampleRate kSr    = 48000.0;
    constexpr std::size_t      kBlock = 512;

    const auto levelDbAt = [&](const synth::RankVoicing& rv, int note)
    {
        synth::AdditiveVoice v;
        v.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        v.prepare(kSr, kBlock);
        v.adoptRank(&rv);
        v.noteOn(core::PipeId{ static_cast<std::uint16_t>(rv.stop.value),
                               static_cast<std::uint8_t>(note), 1 }, 100);

        std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
        float* ch[2] = { l.data(), r.data() };
        double e = 0.0;
        std::size_t n = 0;
        for (int b = 0; b < 200; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            core::AudioBlock blk(ch, 2, kBlock);
            v.renderAdd(blk);
            if (b >= 96) // past the speech
                for (const float x : l)
                {
                    e += static_cast<double>(x) * x;
                    ++n;
                }
        }
        return 20.0 * std::log10(std::sqrt(e / static_cast<double>(n)) + 1.0e-12);
    };

    for (const model::Stop& s : organ.stops())
    {
        if (s.division().value != 1)
            continue;

        const synth::RankVoicing rv = model::buildRankVoicing(organ, s.id());
        if (rv.spectrum.partials.empty())
            continue;

        double      lo = 1.0e9;
        double      hi = -1.0e9;
        std::string curve;
        for (const int note : { 48, 60, 72, 84, 96 })
        {
            const double db = levelDbAt(rv, note);
            lo = std::min(lo, db);
            hi = std::max(hi, db);
            curve += " " + std::to_string(note) + "=" + std::to_string(db);
        }

        INFO(s.name() << " spans " << (hi - lo) << " dB across MIDI 48..96 --" << curve);
        // Measured after the tilt reference was fixed:
        //
        //   Bourdon 16 0.0   Nazard 2 2/3 0.1   Bourdon 8  0.3   Flute 4     0.3
        //   Tierce     0.4   Montre 8     0.6   Prestant 4 0.7   Montre 16   1.0
        //   Fourniture 1.1   Doublette 2  2.1   Trompette  2.1   Clairon 4   4.0
        //
        // The reeds are the wide ones, and legitimately so: a Trompette carries
        // three or four times the harmonics of a Montre, so climbing the compass it
        // has far more of them to lose off the top of the band -- which is true of
        // the pipe as well. The flues have no such excuse.
        const double bound = s.family() == core::TonalFamily::Reed ? 4.5 : 3.0;
        CHECK((hi - lo) < bound);
    }
}
