// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Each temperament against its own construction.
//
// A historical temperament is not twelve numbers, it is a rule: a chain of fifths
// in which each fifth is either pure or narrowed by a stated fraction of a stated
// comma. The twelve numbers are the consequence. The library carried the
// consequence, transcribed to one decimal place, and nothing checked it against
// the rule -- so one of them was wrong and stayed wrong.
//
// Measured, the Young table had four of the six fifths Young narrows and three
// that should have been pure tempered anyway; seven notes out by whole twelfths
// of a Pythagorean comma, up to 5.5 cents. Five cents changes which keys sound
// sweet, which is the entire purpose of a well temperament.
//
// So these tests assert the RULE. A table that satisfies its construction is
// right whoever transcribed it, and one that does not says so here rather than in
// a chord.
//

#include "caecilia/core/EngineTypes.h"
#include "caecilia/tuning/Temperament.h"
#include "caecilia/tuning/TemperamentLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;
namespace core   = caecilia::core;
namespace tuning = caecilia::tuning;

namespace
{
const double kPureFifth  = 1200.0 * std::log2(3.0 / 2.0);
const double kPureThird  = 1200.0 * std::log2(5.0 / 4.0);
const double kPythComma  = 12.0 * kPureFifth - 7.0 * 1200.0;
const double kSyntComma  = 4.0 * kPureFifth - 2.0 * 1200.0 - kPureThird;
const double kSchisma    = kPythComma - kSyntComma;

/// Pitch classes along the chain the constructions are written on, Eb up to G#.
constexpr std::array<int, 12> kChain{ 3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1, 8 };
const char* const kChainNames[12] = { "Eb", "Bb", "F", "C", "G", "D", "A",
                                      "E", "B", "F#", "C#", "G#" };

/// Size of the fifth from pitch class @p lo up to @p hi, in cents.
double fifthCents(const tuning::Temperament& t, int lo, int hi)
{
    const int semis = ((hi - lo) % 12 + 12) % 12;
    return static_cast<double>(semis) * 100.0
         + t.centsForPitchClass(hi) - t.centsForPitchClass(lo)
         + (semis == 7 ? 0.0 : 1200.0);
}

/// Size of the major third from pitch class @p lo up to @p hi, in cents.
double thirdCents(const tuning::Temperament& t, int lo, int hi)
{
    const int semis = ((hi - lo) % 12 + 12) % 12;
    return static_cast<double>(semis) * 100.0
         + t.centsForPitchClass(hi) - t.centsForPitchClass(lo);
}

/// How much each of the eleven chain fifths is narrowed from pure, in cents.
std::vector<double> chainNarrowing(core::TemperamentId id)
{
    const tuning::Temperament t = tuning::TemperamentLibrary::get(id);
    std::vector<double> out;
    out.reserve(11);
    for (std::size_t k = 0; k + 1 < kChain.size(); ++k)
        out.push_back(kPureFifth - fifthCents(t, kChain[k], kChain[k + 1]));
    return out;
}

/// Which chain fifths are narrowed by @p amount, named.
std::string narrowedBy(const std::vector<double>& n, double amount, double tol = 1.0e-6)
{
    std::string s;
    for (std::size_t k = 0; k < n.size(); ++k)
        if (std::abs(n[k] - amount) < tol)
        {
            if (!s.empty()) s += " ";
            s += std::string(kChainNames[k]) + "-" + kChainNames[k + 1];
        }
    return s;
}

std::size_t countNear(const std::vector<double>& n, double amount, double tol = 1.0e-6)
{
    std::size_t c = 0;
    for (const double v : n)
        if (std::abs(v - amount) < tol)
            ++c;
    return c;
}
} // namespace

TEST_CASE("The commas are the commas", "[tuning][construction]")
{
    // The constants the constructions are written in terms of. Stated as literals
    // in the library so the tables can be built without a runtime logarithm, and
    // checked here against the definition so a mistyped digit is not silently a
    // different temperament.
    CHECK(kPythComma == Approx(23.4600103846496).margin(1.0e-10));
    CHECK(kSyntComma == Approx(21.5062895967149).margin(1.0e-10));
    CHECK(kSchisma   == Approx(1.9537207879347).margin(1.0e-10));
    CHECK(kPureFifth == Approx(701.9550008653874).margin(1.0e-10));
    CHECK(kPureThird == Approx(386.3137138648348).margin(1.0e-10));
}

TEST_CASE("Equal temperament deviates from itself by nothing", "[tuning][construction]")
{
    const tuning::Temperament t =
        tuning::TemperamentLibrary::get(core::TemperamentId::Equal);
    for (int pc = 0; pc < 12; ++pc)
        CHECK(t.centsForPitchClass(pc) == Approx(0.0).margin(1.0e-9));

    // Which is the same as saying every fifth is narrowed by exactly a twelfth of
    // the Pythagorean comma -- the amount that closes the circle.
    const std::vector<double> n = chainNarrowing(core::TemperamentId::Equal);
    for (const double v : n)
        CHECK(v == Approx(kPythComma / 12.0).margin(1.0e-9));
}

TEST_CASE("Pythagorean keeps every fifth of its chain pure", "[tuning][construction]")
{
    const std::vector<double> n = chainNarrowing(core::TemperamentId::Pythagorean);
    CHECK(countNear(n, 0.0) == 11);

    // And the comma it refused to spread piles up in the one fifth left over.
    const tuning::Temperament t =
        tuning::TemperamentLibrary::get(core::TemperamentId::Pythagorean);
    const double wolf = fifthCents(t, 8, 3); // G# up to Eb
    INFO("wolf fifth " << wolf << " cents");
    CHECK(kPureFifth - wolf == Approx(kPythComma).margin(1.0e-6));
}

TEST_CASE("Quarter-comma meantone buys pure thirds with its fifths",
          "[tuning][construction]")
{
    // The definition: every fifth narrowed by a quarter of the SYNTONIC comma,
    // which is exactly the amount that makes four of them add up to a pure third.
    const std::vector<double> n = chainNarrowing(core::TemperamentId::QuarterMeantone);
    CHECK(countNear(n, kSyntComma / 4.0) == 11);

    // And so they do -- to better than a thousandth of a cent, which is the
    // "sub-0.1 cent" the tables were meant to be and were not.
    const tuning::Temperament t =
        tuning::TemperamentLibrary::get(core::TemperamentId::QuarterMeantone);
    const int thirds[][2] = { {0,4}, {7,11}, {2,6}, {9,1}, {4,8}, {5,9}, {3,7} };
    for (const auto& pair : thirds)
    {
        const double c = thirdCents(t, pair[0], pair[1]);
        INFO("third " << pair[0] << "->" << pair[1] << " = " << c << " cents");
        CHECK(c == Approx(kPureThird).margin(0.001));
    }
}

TEST_CASE("Werckmeister III spends a quarter comma on four fifths",
          "[tuning][construction]")
{
    // C-G, G-D, D-A and B-F#, each by a quarter of the PYTHAGOREAN comma; the
    // other seven pure. Naming WHICH four matters: four narrowed fifths in the
    // wrong places is a different temperament with the same statistics.
    const std::vector<double> n = chainNarrowing(core::TemperamentId::Werckmeister3);
    const std::string tempered = narrowedBy(n, kPythComma / 4.0);

    INFO("narrowed: " << tempered);
    CHECK(tempered == "C-G G-D D-A B-F#");
    CHECK(countNear(n, 0.0) == 7);
}

TEST_CASE("Kirnberger III buys one pure third and hides the schisma",
          "[tuning][construction]")
{
    const std::vector<double> n = chainNarrowing(core::TemperamentId::Kirnberger3);

    INFO("quarter-syntonic: " << narrowedBy(n, kSyntComma / 4.0)
         << " | schisma: " << narrowedBy(n, kSchisma));
    CHECK(narrowedBy(n, kSyntComma / 4.0) == "C-G G-D D-A A-E");
    CHECK(narrowedBy(n, kSchisma) == "F#-C#");
    CHECK(countNear(n, 0.0) == 6);

    // Four quarter-comma fifths in a row is a pure C-E, and that is the whole
    // point of the temperament.
    const tuning::Temperament t =
        tuning::TemperamentLibrary::get(core::TemperamentId::Kirnberger3);
    CHECK(thirdCents(t, 0, 4) == Approx(kPureThird).margin(0.001));
}

TEST_CASE("Young narrows the six fifths Young narrows",
          "[tuning][construction][regression]")
{
    // THE defect. The transcribed table tempered four of these six, left two of
    // them alone, and tempered three that should have been pure -- so seven of the
    // twelve notes were out by whole twelfths of a comma, as much as 5.5 cents.
    const std::vector<double> n = chainNarrowing(core::TemperamentId::Young);
    const std::string tempered = narrowedBy(n, kPythComma / 6.0);

    INFO("narrowed by a sixth comma: " << tempered
         << " | pure: " << narrowedBy(n, 0.0));
    CHECK(tempered == "C-G G-D D-A A-E E-B B-F#");
    CHECK(countNear(n, 0.0) == 5); // the other five OF THE CHAIN; the wolf is not in it
}

TEST_CASE("Every temperament closes its circle", "[tuning][construction]")
{
    // Twelve fifths must equal seven octaves exactly, whatever route the
    // construction takes to get there.
    //
    // Worth being honest about what this can now catch. While the tables were
    // TRANSCRIBED it was the cheapest possible way to find an arithmetic slip in
    // one. Derived from a chain of eleven fifths it is close to a tautology: the
    // twelfth is whatever is left over, so the sum comes out right however wrong
    // the eleven are. Measured -- three separate defects reintroduced in the
    // constructions, and this test noticed none of them.
    //
    // It stays for the day a hand-written table comes back, through
    // Temperament::custom or through someone pasting one over a construction, and
    // because a temperament that does not close is not a temperament at all.
    for (const core::TemperamentId id : tuning::TemperamentLibrary::builtInIds())
    {
        const tuning::Temperament t = tuning::TemperamentLibrary::get(id);

        double total = 0.0;
        for (std::size_t k = 0; k < 12; ++k)
            total += fifthCents(t, kChain[k], kChain[(k + 1) % 12]);

        INFO(tuning::TemperamentLibrary::displayName(id)
             << ": twelve fifths span " << total << " cents");
        CHECK(total == Approx(7.0 * 1200.0).margin(1.0e-6));
    }
}

TEST_CASE("No built-in temperament is a rounded tenth of a cent",
          "[tuning][construction]")
{
    // The tables used to be transcribed to one decimal place, which put every
    // interval up to a twentieth of a cent away from the one its construction
    // asks for. Derived, they are exact -- and this says so in a way that fails if
    // anyone pastes a rounded table back in.
    bool anyFractional = false;
    for (const core::TemperamentId id : tuning::TemperamentLibrary::builtInIds())
    {
        if (id == core::TemperamentId::Equal)
            continue; // legitimately all zeros

        const tuning::Temperament t = tuning::TemperamentLibrary::get(id);
        for (int pc = 0; pc < 12; ++pc)
        {
            const double v = t.centsForPitchClass(pc);
            if (v == 0.0)
                continue;
            if (std::abs(v * 10.0 - std::round(v * 10.0)) > 1.0e-6)
                anyFractional = true;
        }
    }
    CHECK(anyFractional);
}
