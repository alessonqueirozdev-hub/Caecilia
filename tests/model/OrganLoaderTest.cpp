// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Model / loader tests. OrganLoader::compile turns a hand-authored (or parsed)
// OrganDefinition into an immutable, index-addressable Organ. Cover: dense
// id == index assignment, resolved semantic metadata, exact-rational footage,
// name-reference resolution (division / rank / coupler), the rank -> PipeId
// activation mapping, structural rejection of dangling references, and the
// not-yet-implemented parse/serialize stubs failing loudly.
//

#include "support/TestOrgan.h"

#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/OrganDefinition.h"
#include "caecilia/model/OrganLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace tests = caecilia::tests;

TEST_CASE("compile builds an immutable organ with dense ids", "[model][loader]")
{
    const model::CompileResult result = model::OrganLoader::compile(tests::buildTestDefinition());

    REQUIRE(result.ok());
    CHECK_FALSE(result.diagnostics.hasErrors());
    const model::Organ& organ = *result.organ;

    CHECK(organ.name() == "Test Organ");
    CHECK(organ.builder() == "Caecilia Test Bench");
    CHECK(organ.year() == 2026);

    REQUIRE(organ.stops().size() == tests::stops::count);
    REQUIRE(organ.divisions().size() == 2);
    REQUIRE(organ.ranks().size() == 6);
    REQUIRE(organ.windchests().size() == 1);

    // Every id equals its array index, and the O(1) lookups agree.
    for (std::size_t i = 0; i < organ.stops().size(); ++i)
    {
        CHECK(organ.stops()[i].id().value == static_cast<std::uint16_t>(i));
        const model::Stop* byId = organ.stop(core::StopId{static_cast<std::uint16_t>(i)});
        REQUIRE(byId != nullptr);
        CHECK(byId->id().value == static_cast<std::uint16_t>(i));
    }
    CHECK(organ.stop(core::StopId{999}) == nullptr); // out of range
}

TEST_CASE("compile resolves semantic metadata and exact footage", "[model][loader]")
{
    const model::Organ organ = tests::buildTestOrgan();

    const model::Stop* trumpet = organ.stop(tests::stops::trumpet8);
    REQUIRE(trumpet != nullptr);
    CHECK(trumpet->name() == "Trumpet 8'");
    CHECK(trumpet->family() == core::TonalFamily::Reed);
    CHECK(trumpet->division() == tests::divisions::great);
    CHECK(trumpet->footage() == core::footage::kEight);
    CHECK_FALSE(trumpet->isMutation());

    // The quint keeps its exact rational footage (8/3), so it is correctly
    // classified as a mutation.
    const model::Stop* twelfth = organ.stop(tests::stops::twelfth);
    REQUIRE(twelfth != nullptr);
    CHECK(twelfth->footage() == core::Footage{8, 3});
    CHECK(twelfth->footage() == core::footage::kTwoAndTwoThird);
    CHECK(twelfth->isMutation());
    CHECK(twelfth->pitchClass() == core::PitchClass::Mutation);
}

TEST_CASE("compile resolves coupler name references", "[model][loader]")
{
    const model::Organ organ = tests::buildTestOrgan();

    REQUIRE(organ.couplers().size() == 1);
    const model::Coupler& coupler = organ.couplers()[0];
    CHECK(coupler.name() == "Swell to Great");
    CHECK(coupler.from() == tests::divisions::swell);
    CHECK(coupler.to() == tests::divisions::great);
    CHECK(coupler.isUnison());
    CHECK(coupler.mapNote(60) == 60); // unison: no transposition
}

TEST_CASE("Coupler transposition clamps to the MIDI range", "[model][coupler]")
{
    model::Coupler superOctave;
    superOctave.setOctaveShift(12);
    CHECK_FALSE(superOctave.isUnison());
    CHECK(superOctave.mapNote(60) == 72);
    CHECK(superOctave.mapNote(120) == 127); // clamped, never wraps

    model::Coupler subOctave;
    subOctave.setOctaveShift(-24);
    CHECK(subOctave.mapNote(12) == 0); // clamped at the bottom
}

TEST_CASE("collectPipesForKey activates the pipes an engaged stop sounds", "[model][activation]")
{
    const model::Organ organ = tests::buildTestOrgan();

    const std::array<core::StopId, 1> engaged{tests::stops::principal8};
    std::array<core::PipeId, 8>       activated{};

    const std::size_t written = organ.collectPipesForKey(
        tests::divisions::great, /*note*/ 60,
        std::span<const core::StopId>{engaged},
        std::span<core::PipeId>{activated});

    REQUIRE(written == 1);
    CHECK(activated[0].midiNote == 60);

    // A stop in a DIFFERENT division does not respond to the Great keypress.
    const std::array<core::StopId, 1> swellStop{tests::stops::oboe8};
    const std::size_t none = organ.collectPipesForKey(
        tests::divisions::great, /*note*/ 60,
        std::span<const core::StopId>{swellStop},
        std::span<core::PipeId>{activated});
    CHECK(none == 0);
}

TEST_CASE("the same key in two divisions yields distinct PipeIds", "[model][activation]")
{
    const model::Organ organ = tests::buildTestOrgan();

    // Middle C, once on the Great and once on the Swell.
    const std::array<core::StopId, 1> greatStop{tests::stops::principal8};
    const std::array<core::StopId, 1> swellStop{tests::stops::gedackt8};
    std::array<core::PipeId, 4>       greatPipes{};
    std::array<core::PipeId, 4>       swellPipes{};

    REQUIRE(organ.collectPipesForKey(tests::divisions::great, /*note*/ 60,
                                     std::span<const core::StopId>{greatStop},
                                     std::span<core::PipeId>{greatPipes}) == 1);
    REQUIRE(organ.collectPipesForKey(tests::divisions::swell, /*note*/ 60,
                                     std::span<const core::StopId>{swellStop},
                                     std::span<core::PipeId>{swellPipes}) == 1);

    // Same note, two divisions: the ids must not collide, or a note-off on one
    // manual would release the note on the other.
    CHECK(greatPipes[0].midiNote == swellPipes[0].midiNote);
    CHECK(greatPipes[0].divisionId
          == static_cast<std::uint8_t>(tests::divisions::great.value));
    CHECK(swellPipes[0].divisionId
          == static_cast<std::uint8_t>(tests::divisions::swell.value));
    CHECK_FALSE(greatPipes[0] == swellPipes[0]);

    // The stamp reaches the whole model, not just the two stops probed above:
    // every rank agrees with the division of the stop that draws it.
    bool everyPipeStamped = true;
    for (const model::Stop& s : organ.stops())
    {
        const model::Rank* r = organ.rank(s.rank());
        REQUIRE(r != nullptr);
        const auto expected = static_cast<std::uint8_t>(s.division().value);
        for (const model::Pipe& p : r->pipes())
            everyPipeStamped = everyPipeStamped && p.id.divisionId == expected;
    }
    CHECK(everyPipeStamped);
}

TEST_CASE("the demo organ stamps every rank with its division", "[model][demoorgan]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Pedale, Grand-Orgue and Recit each carry a "Trompette 8", and note 60 is
    // inside all three compasses -- the collision the divisionId byte prevents.
    std::array<core::PipeId, 3> trumpets{};
    std::size_t                 found = 0;

    for (const model::Stop& s : organ.stops())
    {
        if (s.name() != "Trompette 8")
            continue;

        const model::Pipe* p = organ.pipeForKey(s.id(), /*note*/ 60);
        REQUIRE(p != nullptr);
        CHECK(p->id.divisionId == static_cast<std::uint8_t>(s.division().value));

        REQUIRE(found < trumpets.size());
        trumpets[found++] = p->id;
    }

    REQUIRE(found == 3);
    CHECK_FALSE(trumpets[0] == trumpets[1]);
    CHECK_FALSE(trumpets[1] == trumpets[2]);
    CHECK_FALSE(trumpets[0] == trumpets[2]);
}

TEST_CASE("compile rejects a definition with dangling references", "[model][loader]")
{
    model::OrganDefinition def = tests::buildTestDefinition();
    // Point a stop at a rank that does not exist.
    def.stops[0].rank = "NoSuchRank";

    const model::LoadDiagnostics diag = def.validate();
    CHECK(diag.hasErrors());

    const model::CompileResult result = model::OrganLoader::compile(def);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.organ.has_value());
    CHECK(result.diagnostics.hasErrors());
}

TEST_CASE("A well-formed definition validates cleanly", "[model][loader]")
{
    const model::LoadDiagnostics diag = tests::buildTestDefinition().validate();
    CHECK_FALSE(diag.hasErrors());
}

TEST_CASE("parse and serialize are stubs that fail loudly", "[model][loader]")
{
    // The document reader is not implemented yet; it must report an error
    // rather than silently return an empty organ.
    const model::ParseResult parsed = model::OrganLoader::parse("{ \"organ\": true }");
    CHECK_FALSE(parsed.ok());
    CHECK(parsed.diagnostics.hasErrors());

    const model::CompileResult loaded = model::OrganLoader::load("anything at all");
    CHECK_FALSE(loaded.ok());
    CHECK_FALSE(loaded.organ.has_value());

    // The writer is likewise unimplemented and returns empty text.
    CHECK(model::OrganLoader::serialize(tests::buildTestDefinition()).empty());
}
