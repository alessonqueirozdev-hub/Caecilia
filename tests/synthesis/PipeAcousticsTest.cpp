// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Does the rendered audio obey the acoustics of the pipe it claims to be?
 *
 * Every spectral test in this tree checked a SpectralModel -- a DESCRIPTION of a
 * timbre. These check the sound, against facts about organ pipes that hold whether
 * or not anybody has a recording to compare with: a stopped cylindrical pipe
 * suppresses its even harmonics and an open one does not; a principal carries a
 * full falling series; a reed carries far more upper energy than a flute; a 16'
 * stop sounds an octave below an 8'.
 *
 * They are what a listening comparison would rest on, and unlike a listening
 * comparison they can run in CI.
 *
 * The first of them found a real one. Every flute in every organ was synthesised
 * as STOPPED, because makeSpectralFlute's `stopped` parameter defaulted to true
 * and nothing ever passed false -- so a Flûte octaviante, which is open by
 * definition and one of the brightest flutes on a French organ, came out as a
 * hollow bourdon with no even harmonics at all. Measured at -121 dB where it
 * should be near -20.
 */

#include "caecilia/model/OrganLoader.h"

#include "support/Spectrum.h"

#include <catch2/catch_test_macros.hpp>

using namespace caecilia;
using namespace caecilia::testing;

namespace
{

constexpr int kMiddleC = 60;

/// The fundamental a stop sounds at @p note, given its footage.
double soundingHz(const synth::RankVoicing& v, int note)
{
    return noteHz(note) * (8.0 / v.footage.feet());
}

struct Measured
{
    std::vector<float> audio;
    double             f0 = 0.0;

    [[nodiscard]] double h(int n) const { return harmonicDb(audio, f0, n); }
};

Measured measure(const model::Organ& organ, std::string_view stopName, int note = kMiddleC)
{
    const synth::RankVoicing v = voicingNamed(organ, stopName);
    REQUIRE_FALSE(v.spectrum.partials.empty());
    return { renderRankNote(v, note), soundingHz(v, note) };
}

} // namespace

TEST_CASE("A stopped pipe has no even harmonics", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Measured     bourdon = measure(organ, "Bourdon 8");

    // The defining acoustic fact about a stopped cylindrical pipe: the closed end
    // is a pressure antinode, so only odd multiples of the fundamental fit. A
    // Bourdon with a second harmonic is not a Bourdon.
    CHECK(bourdon.h(3) > -40.0);           // the odd ones are there
    CHECK(bourdon.h(2) < bourdon.h(3) - 40.0);
    CHECK(bourdon.h(4) < bourdon.h(3) - 40.0);
}

TEST_CASE("An open pipe has them", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Measured     flute = measure(organ, "Flûte octaviante 4");

    // Octaviante means it overblows at the octave, which a stopped pipe cannot do.
    // Its second harmonic is not merely present, it is the strongest thing above
    // the fundamental -- that is what makes a harmonic flute bright where a
    // bourdon is hollow.
    CHECK(flute.h(2) > -30.0);
    CHECK(flute.h(2) > flute.h(3));
}

TEST_CASE("The two kinds of flute do not sound the same", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // On a real organ this difference is unmistakable at any distance. It was
    // ZERO here until a rank could say which kind it was: both recipes existed,
    // and only one of them was reachable.
    const Measured stopped = measure(organ, "Bourdon 8");
    const Measured open    = measure(organ, "Flûte octaviante 4");

    CHECK(open.h(2) - stopped.h(2) > 60.0);
}

TEST_CASE("A principal carries a full falling series", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Measured     montre = measure(organ, "Montre 8");

    // Every harmonic present, each below the one before it. This is what makes a
    // diapason chorus build: the partials of the 8' line up with the fundamentals
    // of the 4' and the 2'.
    CHECK(montre.h(2) > -20.0);
    CHECK(montre.h(3) > -25.0);
    CHECK(montre.h(4) > -30.0);
    CHECK(montre.h(2) > montre.h(4));
    CHECK(montre.h(4) > montre.h(8));
}

TEST_CASE("A reed is brighter than a flute, well above it", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const Measured trompette = measure(organ, "Trompette 8");
    const Measured bourdon   = measure(organ, "Bourdon 8");

    // A beating reed drives a resonator hard: the energy in the upper partials is
    // what a Trompette IS, and it is the reason a chorus reed can crown a full
    // organ that flues alone cannot.
    double reedTop = 0.0;
    double fluteTop = 0.0;
    for (int n = 4; n <= 8; ++n)
    {
        reedTop  += std::pow(10.0, trompette.h(n) / 10.0);
        fluteTop += std::pow(10.0, bourdon.h(n)   / 10.0);
    }
    const double brighterBy = 10.0 * std::log10(reedTop / fluteTop);

    // Ten decibels is what this asserts and roughly thirteen is what it measures.
    //
    // On a real organ the gap is larger -- a Trompette's spectrum peaks somewhere
    // around its fourth to eighth harmonic while a Bourdon has essentially a
    // fundamental and a weak third, which is tens of decibels apart, not thirteen.
    // The reed RECIPE agrees: it adds four decibels to every odd harmonic and six
    // more across exactly this band, which should put the fifth harmonic within a
    // few decibels of the fundamental. The rendered audio puts it fifteen below.
    //
    // So this asserts the direction, which is certainly right, and not the size,
    // which is under investigation: the gap between what a SpectralModel describes
    // and what the synthesiser fed by it produces is its own question, and a
    // number invented to make this line pass would bury it.
    CHECK(brighterBy > 10.0);
}

TEST_CASE("Footage is pitch: a 16' sounds an octave below an 8'", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const synth::RankVoicing sixteen = voicingNamed(organ, "Bourdon 16");
    const synth::RankVoicing eight   = voicingNamed(organ, "Bourdon 8");
    REQUIRE_FALSE(sixteen.spectrum.partials.empty());
    REQUIRE_FALSE(eight.spectrum.partials.empty());

    const std::vector<float> low  = renderRankNote(sixteen, kMiddleC);
    const std::vector<float> high = renderRankNote(eight,   kMiddleC);

    const double c4 = noteHz(kMiddleC);
    const double c3 = c4 / 2.0;

    // The same key, an octave apart. Measured rather than asserted from the
    // footage arithmetic, because the arithmetic is what is being checked.
    CHECK(magnitudeAt(low,  c3) > magnitudeAt(low,  c4));
    CHECK(magnitudeAt(high, c4) > magnitudeAt(high, c3));
}

TEST_CASE("The top of the compass does not alias", "[synthesis][acoustics]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // The worst case this instrument can be asked for: its brightest rank at the
    // top of its compass, where the upper partials of a 2' rank run past Nyquist.
    // Anything that folds back comes down as an inharmonic tone under the note --
    // the sound of a cheap synthesiser, and the one artefact an organist would
    // notice immediately in the treble.
    const synth::RankVoicing v = voicingNamed(organ, "Doublette 2");
    REQUIRE_FALSE(v.spectrum.partials.empty());

    const int    top   = 96;
    const auto   audio = renderRankNote(v, top);
    const double f0    = soundingHz(v, top);

    // Half an octave below the fundamental there is no harmonic of anything, so
    // whatever sits there arrived by folding.
    const double below = magnitudeAt(audio, f0 * 0.7);
    const double at    = magnitudeAt(audio, f0);
    REQUIRE(at > 0.0);
    CHECK(20.0 * std::log10(below / at) < -60.0);
}

TEST_CASE("An organ file can say which kind of flute it has", "[synthesis][acoustics]")
{
    // The path a user actually takes. The two above measure the organ built into
    // the binary; this one measures a document, because a key that works in C++
    // and not in the file format would help nobody.
    const auto build = [](bool stopped)
    {
        model::OrganDefinition def;
        def.name = "One flute";

        model::WindchestDef w;
        w.name = "Chest";
        def.windchests.push_back(w);

        model::RankDef r;
        r.name      = "Flûte 8";
        r.family    = "Flute";
        r.windchest = "Chest";
        r.stopped   = stopped;
        def.ranks.push_back(r);

        model::DivisionDef d;
        d.name = "Manual";
        def.divisions.push_back(d);

        model::StopDef s;
        s.name     = "Flûte 8";
        s.family   = "Flute";
        s.division = "Manual";
        s.rank     = "Flûte 8";
        def.stops.push_back(s);

        // Through the WRITER and the READER, not straight into compile: the point
        // is that the document carries it.
        const std::string text = model::OrganLoader::serialize(def);
        model::CompileResult compiled = model::OrganLoader::load(text);
        REQUIRE(compiled.ok());
        return compiled;
    };

    const model::CompileResult stoppedOrgan = build(true);
    const model::CompileResult openOrgan    = build(false);

    const Measured hollow = { renderRankNote(model::buildRankVoicing(*stoppedOrgan.organ,
                                                                     core::StopId{ 0 }), kMiddleC),
                              noteHz(kMiddleC) };
    const Measured bright = { renderRankNote(model::buildRankVoicing(*openOrgan.organ,
                                                                     core::StopId{ 0 }), kMiddleC),
                              noteHz(kMiddleC) };

    CHECK(hollow.h(2) < -60.0);
    CHECK(bright.h(2) > -30.0);
}
