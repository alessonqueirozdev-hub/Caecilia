/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/tuning/TemperamentLibrary.h"

namespace ceciliae::tuning
{
namespace
{

using core::TemperamentId;

// ---------------------------------------------------------------------------
// Built-in cent-deviation tables (from equal temperament, C == pitch class 0).
//
// Index order: C, C#, D, D#/Eb, E, F, F#, G, G#/Ab, A, A#/Bb, B.
//
// These are the nominal published deviations for each temperament; A4 is pinned
// to the reference pitch downstream by TuningTable::build, so only the relative
// shape of each table matters here.
// TODO(phase9): replace with high-precision (sub-0.1 cent) reference tables and
// add per-temperament A-anchoring metadata once the tuning validation harness
// (v0.9) is in place.
// ---------------------------------------------------------------------------

constexpr std::array<double, 12> kEqual{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

// Quarter-comma meantone (Aron), Eb/G# accidentals.
constexpr std::array<double, 12> kQuarterMeantone{
    0.0, -24.0, -6.8, 10.3, -13.7, 3.4, -20.5, -3.4, -27.4, -10.3, 6.8, -17.1};

// Werckmeister III (Werckmeister I / "correct temperament no. 1").
constexpr std::array<double, 12> kWerckmeister3{
    0.0, -9.8, -7.8, -5.9, -9.8, -2.0, -11.7, -3.9, -7.8, -11.7, -3.9, -7.8};

// Kirnberger III.
constexpr std::array<double, 12> kKirnberger3{
    0.0, -9.8, -6.8, -5.9, -13.7, -2.0, -9.8, -3.4, -7.8, -10.3, -3.9, -11.7};

// Pythagorean (pure fifths; wolf between G# and Eb).
constexpr std::array<double, 12> kPythagorean{
    0.0, 13.7, 3.9, -5.9, 7.8, -2.0, 11.7, 2.0, 15.6, 5.9, -3.9, 9.8};

// Thomas Young (1799), well temperament.
constexpr std::array<double, 12> kYoung{
    0.0, -6.1, -3.9, -2.0, -7.8, -0.1, -6.3, -2.0, -4.1, -5.9, -0.2, -7.9};

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
        case TemperamentId::Equal:           return make(id, kEqual, "Equal");
        case TemperamentId::QuarterMeantone: return make(id, kQuarterMeantone, "Quarter-comma meantone");
        case TemperamentId::Werckmeister3:   return make(id, kWerckmeister3, "Werckmeister III");
        case TemperamentId::Kirnberger3:     return make(id, kKirnberger3, "Kirnberger III");
        case TemperamentId::Pythagorean:     return make(id, kPythagorean, "Pythagorean");
        case TemperamentId::Young:           return make(id, kYoung, "Young (1799)");
        case TemperamentId::Custom:
        default:
            // Custom tables are supplied via Temperament::custom(); fall back to
            // equal temperament so an unconfigured Custom id still sounds sane.
            return make(TemperamentId::Equal, kEqual, "Equal");
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
        case TemperamentId::Young:           return "Young (1799)";
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

} // namespace ceciliae::tuning
