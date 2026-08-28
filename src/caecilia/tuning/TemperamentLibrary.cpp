// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/tuning/TemperamentLibrary.h"

#include <array>
#include <cstddef>

namespace caecilia::tuning
{
namespace
{

using core::TemperamentId;

// ---------------------------------------------------------------------------
// The temperaments, built from their constructions.
//
// Every one of these is a CHAIN OF FIFTHS in which each fifth is either pure or
// narrowed by a stated fraction of a stated comma. That is what a temperament IS
// -- the twelve cent deviations are its consequence, not its definition -- and
// deriving them here rather than transcribing them has three consequences worth
// having: the values are exact instead of rounded to a tenth of a cent, the
// defining property of each becomes something a test can assert, and a whole
// class of transcription error becomes unrepresentable.
//
// That class of error was present. Measured against its own construction, the
// Young table had four of the six fifths Young narrows, and three that should
// have been pure tempered anyway; seven of its twelve notes were out by whole
// twelfths of a Pythagorean comma, up to 5.5 cents. The five that were right
// (C, D, E, G, A) matched to four hundredths, so it was that temperament badly
// transcribed rather than a different one. Five and a half cents is not
// inaudible: it changes which keys sound sweet, which is the whole purpose of a
// well temperament.
//
// A4 is pinned to the reference pitch downstream by TuningTable::build, so only
// the relative shape of a table matters; C is used as the origin here because it
// is where every one of these constructions starts.
// ---------------------------------------------------------------------------

/// A pure fifth, 1200*log2(3/2). Verified against std::log2 by the test suite.
constexpr double kPureFifth = 701.9550008653874;

/// Twelve pure fifths overshoot seven octaves by this much.
constexpr double kPythagoreanComma = 23.4600103846496;

/// Four pure fifths overshoot two octaves and a pure third by this much.
constexpr double kSyntonicComma = 21.5062895967149;

/// The difference between the two commas: the amount Kirnberger hides in one
/// fifth, and small enough that the fifth carrying it is all but pure.
constexpr double kSchisma = kPythagoreanComma - kSyntonicComma;

/// The chain the constructions are written along, Eb up to G#. Eleven fifths for
/// twelve notes; the twelfth, G#-Eb, is the wolf and is whatever is left over.
constexpr std::array<int, 12> kChainPitchClass{
    3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1, 8 };
//  Eb  Bb  F  C  G  D  A  E   B  F#  C#  G#

/// Index of C in @ref kChainPitchClass: where every construction starts.
constexpr std::size_t kChainOrigin = 3;

/**
 * @brief Turn a construction into the twelve cent deviations from equal.
 * @param narrowing How much each of the eleven chain fifths is narrowed from
 *                  pure, in cents. Zero is a pure fifth.
 *
 * Walks out from C in both directions, accumulating each fifth's deviation from
 * the 700 cents an equal-tempered fifth would have.
 */
[[nodiscard]] std::array<double, 12> fromChain(
    const std::array<double, 11>& narrowing) noexcept
{
    std::array<double, 12> cents{};

    double up = 0.0;
    for (std::size_t k = kChainOrigin; k + 1 < kChainPitchClass.size(); ++k)
    {
        up += (kPureFifth - narrowing[k]) - 700.0;
        cents[static_cast<std::size_t>(kChainPitchClass[k + 1])] = up;
    }

    double down = 0.0;
    for (std::size_t k = kChainOrigin; k > 0; --k)
    {
        down -= (kPureFifth - narrowing[k - 1]) - 700.0;
        cents[static_cast<std::size_t>(kChainPitchClass[k - 1])] = down;
    }

    cents[0] = 0.0; // C, the origin
    return cents;
}

/// Every fifth narrowed by the same amount: the meantone family, and equal.
[[nodiscard]] std::array<double, 11> uniform(double narrowing) noexcept
{
    std::array<double, 11> n{};
    n.fill(narrowing);
    return n;
}

/// Equal: every fifth narrowed by a twelfth of the Pythagorean comma, which is
/// exactly what it takes to close the circle.
[[nodiscard]] std::array<double, 12> equalTable() noexcept
{
    return fromChain(uniform(kPythagoreanComma / 12.0));
}

/// Pythagorean: every fifth pure. The comma piles up in the G#-Eb wolf.
[[nodiscard]] std::array<double, 12> pythagoreanTable() noexcept
{
    return fromChain(uniform(0.0));
}

/// Quarter-comma meantone (Aron): every fifth narrowed by a quarter of the
/// SYNTONIC comma, which is exactly what makes the major thirds pure.
[[nodiscard]] std::array<double, 12> quarterMeantoneTable() noexcept
{
    return fromChain(uniform(kSyntonicComma / 4.0));
}

/// Werckmeister III: C-G, G-D, D-A and B-F# each narrowed by a quarter of the
/// PYTHAGOREAN comma; the other seven pure.
[[nodiscard]] std::array<double, 12> werckmeister3Table() noexcept
{
    const double q = kPythagoreanComma / 4.0;
    //                Eb-Bb Bb-F F-C  C-G  G-D  D-A  A-E  E-B  B-F# F#-C# C#-G#
    return fromChain({ 0.0,  0.0, 0.0, q,   q,   q,   0.0, 0.0, q,   0.0,  0.0 });
}

/// Kirnberger III: C-G, G-D, D-A and A-E each narrowed by a quarter of the
/// SYNTONIC comma -- which makes C-E pure -- and the schisma tucked into F#-C#.
[[nodiscard]] std::array<double, 12> kirnberger3Table() noexcept
{
    const double q = kSyntonicComma / 4.0;
    //                Eb-Bb Bb-F F-C  C-G  G-D  D-A  A-E  E-B  B-F# F#-C#     C#-G#
    return fromChain({ 0.0,  0.0, 0.0, q,   q,   q,   q,   0.0, 0.0, kSchisma, 0.0 });
}

/// Young: the six fifths from C round to F# each narrowed by a SIXTH of the
/// Pythagorean comma; the other six pure.
[[nodiscard]] std::array<double, 12> youngTable() noexcept
{
    const double s = kPythagoreanComma / 6.0;
    //                Eb-Bb Bb-F F-C  C-G  G-D  D-A  A-E  E-B  B-F# F#-C# C#-G#
    return fromChain({ 0.0,  0.0, 0.0, s,   s,   s,   s,   s,   s,   0.0,  0.0 });
}

Temperament make(TemperamentId id, const std::array<double, 12>& table, const char* name) noexcept
{
    Temperament t{};
    t.id = id;
    t.centsFromEqual = table;
    t.name = name;
    return t;
}

} // namespace

Temperament TemperamentLibrary::get(TemperamentId id) noexcept
{
    switch (id)
    {
        case TemperamentId::Equal:           return make(id, equalTable(), "Equal");
        case TemperamentId::QuarterMeantone: return make(id, quarterMeantoneTable(), "Quarter-comma meantone");
        case TemperamentId::Werckmeister3:   return make(id, werckmeister3Table(), "Werckmeister III");
        case TemperamentId::Kirnberger3:     return make(id, kirnberger3Table(), "Kirnberger III");
        case TemperamentId::Pythagorean:     return make(id, pythagoreanTable(), "Pythagorean");
        case TemperamentId::Young:           return make(id, youngTable(), "Young");
        case TemperamentId::Custom:
        default:
            // Custom tables are supplied via Temperament::custom(); fall back to
            // equal temperament so an unconfigured Custom id still sounds sane.
            return make(TemperamentId::Equal, equalTable(), "Equal");
    }
}

bool TemperamentLibrary::isBuiltIn(TemperamentId id) noexcept
{
    return id != TemperamentId::Custom;
}

const char* TemperamentLibrary::displayName(TemperamentId id) noexcept
{
    switch (id)
    {
        case TemperamentId::Equal:           return "Equal";
        case TemperamentId::QuarterMeantone: return "Quarter-comma meantone";
        case TemperamentId::Werckmeister3:   return "Werckmeister III";
        case TemperamentId::Kirnberger3:     return "Kirnberger III";
        case TemperamentId::Pythagorean:     return "Pythagorean";
        case TemperamentId::Young:           return "Young";
        case TemperamentId::Custom:          return "Custom";
    }
    return "Unknown";
}

std::array<core::TemperamentId, TemperamentLibrary::kBuiltInCount> TemperamentLibrary::builtInIds() noexcept
{
    return {TemperamentId::Equal,
            TemperamentId::QuarterMeantone,
            TemperamentId::Werckmeister3,
            TemperamentId::Kirnberger3,
            TemperamentId::Pythagorean,
            TemperamentId::Young};
}

} // namespace caecilia::tuning
