/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/model/Organ.h"
#include "ceciliae/model/OrganDefinition.h"
#include "ceciliae/model/OrganLoader.h"

/**
 * @file TestOrgan.h
 * @brief A tiny, deterministic two-manual organ used across the unit suite.
 *
 * The registration and model tests both need a real compiled @c model::Organ
 * with a known, stable layout to assert against. Building it once here keeps the
 * fixtures identical and the stop ids fixed, so the selector-resolution and
 * loader tests reference the same instrument by the named constants below.
 *
 * Layout (ids equal array indices, per the loader's identity convention):
 *
 *   Divisions : 0 Great, 1 Swell
 *   Windchest : 0 Main
 *   Stops     : 0 Principal 8'   Great  Principal  8'      Unison    Foundation
 *               1 Octave 4'      Great  Principal  4'      Octave    Chorus
 *               2 Twelfth 2 2/3' Great  Flute      2 2/3'  Mutation  Color
 *               3 Trumpet 8'     Great  Reed       8'      Unison    Chorus
 *               4 Oboe 8'        Swell  Reed       8'      Unison    Solo
 *               5 Gedackt 8'     Swell  Flute      8'      Unison    Foundation
 *   Couplers  : 0 Swell to Great (unison, Swell -> Great)
 *
 * By family:  Principal = {0,1}, Flute = {2,5}, Reed = {3,4}.
 */

namespace ceciliae::tests
{

/// Stable stop ids of @ref buildTestDefinition (index == id, see the file docs).
namespace stops
{
inline constexpr core::StopId principal8{0};
inline constexpr core::StopId octave4{1};
inline constexpr core::StopId twelfth{2};
inline constexpr core::StopId trumpet8{3};
inline constexpr core::StopId oboe8{4};
inline constexpr core::StopId gedackt8{5};
inline constexpr std::size_t  count = 6;
} // namespace stops

/// Stable division ids of @ref buildTestDefinition.
namespace divisions
{
inline constexpr core::DivisionId great{0};
inline constexpr core::DivisionId swell{1};
} // namespace divisions

/// @return The hand-authored definition of the fixture instrument.
[[nodiscard]] inline model::OrganDefinition buildTestDefinition()
{
    using model::CouplerDef;
    using model::DivisionDef;
    using model::RankDef;
    using model::StopDef;
    using model::WindchestDef;

    model::OrganDefinition def;
    def.name    = "Test Organ";
    def.builder = "Ceciliae Test Bench";
    def.year    = 2026;

    WindchestDef chest;
    chest.name       = "Main";
    chest.pressurePa = 735.0f;
    def.windchests.push_back(chest);

    // Two manual divisions.
    DivisionDef great;
    great.name = "Great";
    great.kind = "Manual";
    DivisionDef swell;
    swell.name = "Swell";
    swell.kind = "Manual";
    def.divisions.push_back(great);
    def.divisions.push_back(swell);

    // One rank per stop (a straight, non-unit specification).
    const auto addRank = [&](const char* name, const char* family,
                             std::int32_t num, std::int32_t den)
    {
        RankDef r;
        r.name       = name;
        r.family     = family;
        r.footageNum = num;
        r.footageDen = den;
        r.windchest  = "Main";
        def.ranks.push_back(r);
    };
    addRank("Principal8", "Principal", 8, 1);
    addRank("Octave4",    "Principal", 4, 1);
    addRank("Twelfth",    "Flute",     8, 3);
    addRank("Trumpet8",   "Reed",      8, 1);
    addRank("Oboe8",      "Reed",      8, 1);
    addRank("Gedackt8",   "Flute",     8, 1);

    // Stops (order fixes the ids referenced by the tests).
    const auto addStop = [&](const char* name, const char* family,
                             std::int32_t num, std::int32_t den, const char* pitch,
                             const char* role, const char* division, const char* rank)
    {
        StopDef s;
        s.name       = name;
        s.family     = family;
        s.footageNum = num;
        s.footageDen = den;
        s.pitchClass = pitch;
        s.role       = role;
        s.division   = division;
        s.rank       = rank;
        def.stops.push_back(s);
    };
    addStop("Principal 8'",    "Principal", 8, 1, "Unison",   "Foundation", "Great", "Principal8");
    addStop("Octave 4'",       "Principal", 4, 1, "Octave",   "Chorus",     "Great", "Octave4");
    addStop("Twelfth 2 2/3'",  "Flute",     8, 3, "Mutation", "Color",      "Great", "Twelfth");
    addStop("Trumpet 8'",      "Reed",      8, 1, "Unison",   "Chorus",     "Great", "Trumpet8");
    addStop("Oboe 8'",         "Reed",      8, 1, "Unison",   "Solo",       "Swell", "Oboe8");
    addStop("Gedackt 8'",      "Flute",     8, 1, "Unison",   "Foundation", "Swell", "Gedackt8");

    // A single unison inter-manual coupler, Swell -> Great.
    CouplerDef coupler;
    coupler.name        = "Swell to Great";
    coupler.from        = "Swell";
    coupler.to          = "Great";
    coupler.octaveShift = 0;
    coupler.kind        = "InterManual";
    def.couplers.push_back(coupler);

    return def;
}

/// @return The compiled fixture organ. Aborts the calling test if it fails to
///         compile (which would indicate a broken loader, not a broken test).
[[nodiscard]] inline model::Organ buildTestOrgan()
{
    model::CompileResult result = model::OrganLoader::compile(buildTestDefinition());
    // The fixture is deliberately valid; return an empty organ only if the loader
    // regressed, in which case the consuming test's assertions will fail loudly.
    return result.organ ? std::move(*result.organ) : model::Organ{};
}

} // namespace ceciliae::tests
