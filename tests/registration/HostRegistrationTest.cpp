// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The pure-core half of VST-001: turning one registration into one truth.
//
// The plugin has been carrying three disagreeing registrations — a StopId-keyed
// `engaged_` bitmap, a family+footage `currentRanks_` list the console writes,
// and whichever of them prepareToPlay happened to rebuild from. The arithmetic
// that collapses them lives here rather than in the plugin target, because
// nothing in the plugin target can be reached by a test.
//
// Two of these cases are guard rails rather than assertions about today: the
// stop count against the mask width, which fires the day somebody grows the
// organ past the reserved parameter pool, and primaryManual's pedal exclusion,
// which is invisible on this instrument and wrong on a plausible one.
//

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/OrganDefinition.h"
#include "caecilia/model/OrganLoader.h"
#include "caecilia/model/Stop.h"
#include "caecilia/registration/StopSet.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace reg   = caecilia::registration;

namespace
{
model::RegistrationRank rank(core::TonalFamily fam, double feet, bool compound = false)
{
    return model::RegistrationRank{ fam, model::footageFromFeet(feet), compound };
}
} // namespace

TEST_CASE("A registration round-trips through a 64-bit mask", "[registration][host]")
{
    // The mask is what the audio thread will read once per block instead of
    // taking a lock: one 64-bit compare against what is already sounding. It only
    // earns that if it is exactly the set it came from.
    std::vector<core::StopId> ids;
    for (const std::uint16_t v : { 0, 1, 7, 25, 26, 63 })
        ids.push_back(core::StopId{ v });

    const reg::StopSet original{ ids };
    REQUIRE(original.fitsInMask());

    const std::uint64_t mask = original.toMask();
    const reg::StopSet  back = reg::StopSet::fromMask(mask);

    CHECK(back == original);
    CHECK(back.toMask() == mask);

    // Empty and full are the two ends that off-by-ones live at.
    CHECK(reg::StopSet{}.toMask() == 0u);
    CHECK(reg::StopSet::fromMask(0u).empty());
    CHECK(reg::StopSet::fromMask(~std::uint64_t{0}).size() == reg::StopSet::kMaskCapacity);
}

TEST_CASE("A stop beyond the mask is dropped, and says so", "[registration][host]")
{
    // Silently widening the mask would break the single-compare contract the
    // audio thread depends on, so an id it cannot hold is dropped — but never
    // quietly: fitsInMask is what a caller checks before trusting the round trip.
    std::vector<core::StopId> ids{ core::StopId{ 3 }, core::StopId{ 64 }, core::StopId{ 200 } };
    const reg::StopSet set{ ids };

    CHECK_FALSE(set.fitsInMask());
    CHECK(set.toMask() == (std::uint64_t{1} << 3));
    CHECK(reg::StopSet::fromMask(set.toMask()).size() == 1);
}

TEST_CASE("The demo organ still fits the reserved parameter pool",
          "[registration][host][guardrail]")
{
    // Not an assertion about the instrument — a tripwire on the design. The host
    // stop-parameter pool is sized to the mask, so the day this organ grows past
    // 64 stops, either the pool grows with it or stops silently stop being
    // automatable. This is what makes that a build failure rather than a mystery.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    INFO(organ.stops().size() << " stops");
    CHECK(organ.stops().size() == 26);
    CHECK(organ.stops().size() <= reg::StopSet::kMaskCapacity);
}

TEST_CASE("The primary manual is never the pedalboard", "[registration][host][regression]")
{
    // The heuristic this replaces picks whichever division carries the most stops,
    // full stop. Assert that against the demo organ and the test proves nothing:
    // its Grand-Orgue already has twice the Pédale's stops, so the exclusion never
    // has to do anything. So build the instrument where it does — a large Pédale
    // and a small manual, which is not exotic at all on a big romantic organ.
    //
    // Getting this wrong routes a controller's manual keyboard to the pedalboard:
    // the notes land two octaves down on a thirty-note compass, and everything
    // above it is dropped as out of range.
    model::OrganDefinition def;
    def.name = "Pedal-heavy";

    model::WindchestDef chest;
    chest.name       = "Main";
    chest.pressurePa = 735.0f;
    def.windchests.push_back(chest);

    model::DivisionDef pedal;
    pedal.name = "Pedale";
    pedal.kind = "Pedal";
    model::DivisionDef manual;
    manual.name = "Manual";
    manual.kind = "Manual";
    def.divisions.push_back(pedal);
    def.divisions.push_back(manual);

    const auto add = [&](const char* name, const char* division, int n)
    {
        model::RankDef r;
        r.name       = name;
        r.family     = "Principal";
        r.footageNum = n;
        r.footageDen = 1;
        r.windchest  = "Main";
        def.ranks.push_back(r);

        model::StopDef s;
        s.name       = name;
        s.family     = "Principal";
        s.footageNum = n;
        s.footageDen = 1;
        s.pitchClass = "Unison";
        s.role       = "Foundation";
        s.division   = division;
        s.rank       = name;
        def.stops.push_back(s);
    };

    // Four on the pedals, one on the manual: the pedals win on count alone.
    add("Ped A", "Pedale", 16);
    add("Ped B", "Pedale", 8);
    add("Ped C", "Pedale", 4);
    add("Ped D", "Pedale", 2);
    add("Man A", "Manual", 8);

    const model::CompileResult result = model::OrganLoader::compile(def);
    REQUIRE(result.ok());
    const model::Organ& organ = *result.organ;

    const core::DivisionId chosen = model::primaryManual(organ);

    const model::Division* found = nullptr;
    for (const model::Division& d : organ.divisions())
        if (d.id() == chosen)
            found = &d;

    REQUIRE(found != nullptr);
    INFO("chose " << found->name() << " with " << found->stopCount() << " stops");
    CHECK(found->kind() != model::DivisionKind::Pedal);
    CHECK(found->name() == "Manual");
}

TEST_CASE("The primary manual is the richest manual", "[registration][host]")
{
    // And on a normal instrument it is still the one with the most stops.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const core::DivisionId manual = model::primaryManual(organ);

    const model::Division* found = nullptr;
    for (const model::Division& d : organ.divisions())
        if (d.id() == manual)
            found = &d;

    REQUIRE(found != nullptr);
    INFO("chose division " << found->name());
    CHECK(found->kind() != model::DivisionKind::Pedal);
    for (const model::Division& d : organ.divisions())
        if (d.kind() != model::DivisionKind::Pedal)
            CHECK(d.stopCount() <= found->stopCount());
}

TEST_CASE("The opening registration is a plenum on the primary manual",
          "[registration][host]")
{
    // This becomes the host parameters' DEFAULT value, which means it is also what
    // "reset to default" gives back and what a document written before the
    // parameters existed restores to. It has to be a real registration, on the
    // right keyboard, and never empty.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const core::DivisionId manual = model::primaryManual(organ);
    const std::vector<core::StopId> drawn = model::defaultOpeningRegistration(organ);

    REQUIRE_FALSE(drawn.empty());
    CHECK(std::is_sorted(drawn.begin(), drawn.end(),
                         [](core::StopId a, core::StopId b) { return a.value < b.value; }));

    bool anyPrincipal = false;
    for (const core::StopId id : drawn)
    {
        const model::Stop* s = nullptr;
        for (const model::Stop& candidate : organ.stops())
            if (candidate.id() == id)
                s = &candidate;
        REQUIRE(s != nullptr);
        CHECK(s->division() == manual);
        anyPrincipal = anyPrincipal || s->family() == core::TonalFamily::Principal;
    }
    CHECK(anyPrincipal); // a plenum without a principal is not a plenum
}

TEST_CASE("Ranks resolve to stops deterministically", "[registration][host]")
{
    // The console speaks family+footage; the parameters and the model speak
    // StopId. If the translation were not deterministic a saved session would
    // reopen on a different instrument.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const std::vector<model::RegistrationRank> ranks{
        rank(core::TonalFamily::Principal, 8),
        rank(core::TonalFamily::Flute, 8),
    };

    const auto first  = model::resolveRanksToStops(organ, ranks);
    const auto second = model::resolveRanksToStops(organ, ranks);
    CHECK(first == second);
    CHECK_FALSE(first.empty());
    CHECK(std::is_sorted(first.begin(), first.end(),
                         [](core::StopId a, core::StopId b) { return a.value < b.value; }));
}

TEST_CASE("Two identical ranks resolve to two different stops",
          "[registration][host][regression]")
{
    // A Gambe and its Céleste are both {String, 8'}. Resolving them onto the same
    // stop twice would collapse the pair — and the beat between them is the whole
    // reason a céleste exists.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Find a family+footage the instrument really has twice.
    core::TonalFamily doubledFamily = core::TonalFamily::String;
    core::Footage     doubledFootage = core::footage::kEight;
    int               count = 0;
    for (const model::Stop& s : organ.stops())
        if (s.family() == doubledFamily && s.footage() == doubledFootage && !s.isCompound())
            ++count;

    if (count < 2)
    {
        WARN("the demo organ has no doubled family+footage; skipping");
        return;
    }

    const std::vector<model::RegistrationRank> pair{
        model::RegistrationRank{ doubledFamily, doubledFootage, false },
        model::RegistrationRank{ doubledFamily, doubledFootage, false },
    };
    const auto ids = model::resolveRanksToStops(organ, pair);

    INFO("resolved " << ids.size() << " ids");
    CHECK(ids.size() == 2);
    if (ids.size() == 2)
        CHECK(ids[0].value != ids[1].value);
}

TEST_CASE("A rank the instrument does not have resolves to nothing",
          "[registration][host]")
{
    // Substituting the nearest stop would make a registration that does not exist
    // on this organ sound like one that does — a typo that plays.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const std::vector<model::RegistrationRank> absent{ rank(core::TonalFamily::String, 16) };
    // Parenthesised: Catch2 decomposes the top-level comparison, and an
    // unparenthesised && of two of them reads to it as a chained comparison.
    for (const model::Stop& s : organ.stops())
        REQUIRE_FALSE((s.family() == core::TonalFamily::String
                       && s.footage() == core::footage::kSixteen));

    CHECK(model::resolveRanksToStops(organ, absent).empty());
}

TEST_CASE("The whole instrument drawn at once fits the composite budget",
          "[registration][host][guardrail]")
{
    // The plugin pre-allocates its composite partial storage; drawing every stop
    // is the worst case it must survive without reallocating on the audio thread.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<core::StopId> all;
    all.reserve(organ.stops().size());
    for (const model::Stop& s : organ.stops())
        all.push_back(s.id());

    const auto composite = model::buildRegistrationCompositeSpectrum(organ, all);
    INFO(composite.partials.size() << " partials with every stop drawn");
    CHECK(composite.partials.size() > 0);
    CHECK(composite.partials.size() <= 1024); // kMaxCompositePartials
}
