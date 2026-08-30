// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Are the historical temperaments audible, and are they right?
 *
 * The temperament tables are constructed from first principles and tested as
 * numbers: the Pythagorean comma, the syntonic comma, the schisma, the chain of
 * fifths. What no test asked was whether any of it reaches a pipe. A temperament
 * that is computed correctly and never applied sounds exactly like equal
 * temperament, and the tests would all still pass.
 *
 * So these render notes and MEASURE the intervals between them, against the
 * intervals those temperaments are defined by:
 *
 *   quarter-comma meantone   major third   386.31 cents (a pure 5:4)
 *   Pythagorean              fifth         701.955 cents (a pure 3:2)
 *   equal                    major third   400, fifth 700, by construction
 *
 * The per-rank detune cancels: it is one constant applied to every note of a
 * rank, so it shifts both ends of an interval equally and drops out of the
 * difference. That is what makes this measurable at all.
 *
 * Every interval below measures about half a cent WIDE of its theoretical value,
 * consistently and in every temperament, and that is not error: TuningModel
 * applies a stretch, so octaves and everything inside them come out slightly wide
 * exactly as they do on a real instrument. The deviation grows with the size of
 * the interval, which is the signature of a stretch rather than of a bias.
 */

#include "caecilia/tuning/TemperamentLibrary.h"
#include "caecilia/tuning/TuningModel.h"

#include "support/Spectrum.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace caecilia;
using namespace caecilia::testing;

namespace
{

constexpr int kC4  = 60;
constexpr int kE4  = 64;
constexpr int kG4  = 67;

/// A voice sounding @p note under @p id, measured.
double soundingHz(const synth::RankVoicing& voicing, core::TemperamentId id, int note)
{
    tuning::TuningModel model;
    model.configure(tuning::TemperamentLibrary::get(id), 440.0);

    synth::VoiceContext ctx;
    ctx.tuning  = &model;
    ctx.footage = voicing.footage;
    ctx.family  = voicing.family;

    const std::vector<float> audio = renderRankNote(voicing, note, 0.5, 48000.0, &ctx);
    return estimateHz(audio, noteHz(note));
}

/// The interval @p lowNote -> @p highNote as this instrument actually sounds it.
double measuredCents(core::TemperamentId id, int lowNote, int highNote)
{
    const model::Organ       organ = model::buildCaeciliaDemoOrgan();
    const synth::RankVoicing v     = voicingNamed(organ, "Montre 8");
    REQUIRE_FALSE(v.spectrum.partials.empty());

    return centsBetween(soundingHz(v, id, lowNote), soundingHz(v, id, highNote));
}

} // namespace

TEST_CASE("Equal temperament sounds equal", "[tuning][acoustics]")
{
    // The control. If this does not come out at 400 and 700 the measurement is
    // wrong and nothing below it means anything.
    CHECK_THAT(measuredCents(core::TemperamentId::Equal, kC4, kE4),
               Catch::Matchers::WithinAbs(400.0, 1.0));
    CHECK_THAT(measuredCents(core::TemperamentId::Equal, kC4, kG4),
               Catch::Matchers::WithinAbs(700.0, 1.0));
}

TEST_CASE("Meantone's major third is pure, and audibly so", "[tuning][acoustics]")
{
    // The whole point of quarter-comma meantone: every fifth is narrowed by a
    // quarter of the syntonic comma so that four of them stacked land on a PURE
    // major third, 5:4, 386.31 cents. Fourteen cents below equal -- which is the
    // difference between a third that beats and one that does not, and is why
    // this temperament exists.
    const double third = measuredCents(core::TemperamentId::QuarterMeantone, kC4, kE4);

    CHECK_THAT(third, Catch::Matchers::WithinAbs(386.31, 1.5));
    CHECK(third < 395.0);   // and unmistakably not equal temperament
}

TEST_CASE("Pythagorean's fifth is pure", "[tuning][acoustics]")
{
    // Built from pure 3:2 fifths, so C-G is 701.955 cents: two above equal, and
    // the reason its thirds are so wide -- 408 rather than 400.
    //
    // One cent, not the one-and-a-half the others get. The whole effect here is
    // two cents, so a looser window would admit equal temperament and the test
    // would pass on an instrument that ignored the temperament entirely. Verified
    // by disconnecting the tuning and watching this fail.
    CHECK_THAT(measuredCents(core::TemperamentId::Pythagorean, kC4, kG4),
               Catch::Matchers::WithinAbs(701.955, 1.0));

    CHECK_THAT(measuredCents(core::TemperamentId::Pythagorean, kC4, kE4),
               Catch::Matchers::WithinAbs(407.82, 1.0));
}

TEST_CASE("Werckmeister III is neither of them", "[tuning][acoustics]")
{
    // The compromise that made playing in every key bearable: the four fifths of
    // the C chain each narrowed by a quarter of the Pythagorean comma, leaving C-E
    // at 390 -- between meantone's pure 386 and equal's 400 -- so the home keys
    // stay sweet without the remote ones howling.
    CHECK_THAT(measuredCents(core::TemperamentId::Werckmeister3, kC4, kE4),
               Catch::Matchers::WithinAbs(390.22, 1.0));
    CHECK_THAT(measuredCents(core::TemperamentId::Werckmeister3, kC4, kG4),
               Catch::Matchers::WithinAbs(696.09, 1.0));
}

TEST_CASE("A temperament reaches the pipes at all", "[tuning][acoustics]")
{
    // The failure this whole file exists to catch. A temperament that is computed
    // correctly and never wired to a voice sounds exactly like equal temperament,
    // and every numeric test of the tables would still pass.
    const double equal    = measuredCents(core::TemperamentId::Equal,           kC4, kE4);
    const double meantone = measuredCents(core::TemperamentId::QuarterMeantone, kC4, kE4);

    CHECK(equal - meantone > 10.0);
}

