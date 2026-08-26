// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Registration-brain tests: the queryable relational core. Cover the StopSet set
// algebra (union / intersection / difference / toggle), the shared SelectorParser
// grammar resolving intent BY FAMILY / pitch / division against a live OrganSpec,
// and command-sourced UNDO/REDO (state = fold of the command log; a StateDelta is
// the exact inverse of the transition that produced it).
//

#include "support/TestOrgan.h"

#include "caecilia/registration/RegistrationCommand.h"
#include "caecilia/registration/RegistrationState.h"
#include "caecilia/registration/Selector.h"
#include "caecilia/registration/SelectorParser.h"

#include <string>
#include "caecilia/registration/StateDelta.h"
#include "caecilia/registration/StopQuery.h"
#include "caecilia/registration/StopSet.h"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

namespace core  = caecilia::core;
namespace reg   = caecilia::registration;
namespace tests = caecilia::tests;

namespace
{
reg::StopSet setOf(std::vector<core::StopId> ids)
{
    return reg::StopSet{std::span<const core::StopId>{ids}};
}

/// Fold a single command into a state (the RegistrationEngine's core primitive).
/// Kept minimal — enough to demonstrate the undo/redo semantics against real types.
reg::RegistrationState applyCommand(reg::RegistrationState state,
                                    const reg::RegistrationCommand& cmd,
                                    const caecilia::model::Organ& organ)
{
    using Kind = reg::RegistrationCommand::Kind;
    switch (cmd.kind)
    {
        case Kind::EngageStop:    state.engage(cmd.stop); break;
        case Kind::DisengageStop: state.disengage(cmd.stop); break;
        case Kind::ToggleStop:    state.toggle(cmd.stop); break;
        case Kind::EngageSelector:
        {
            const reg::StopSet matched = cmd.selector.resolve(organ, state);
            for (const core::StopId id : matched.ids())
                state.engage(id);
            break;
        }
        case Kind::Clear: state.clearAll(); break;
        default: break; // other kinds are out of scope for this fixture
    }
    return state;
}
} // namespace

TEST_CASE("StopSet is a sorted, deduplicated set with in-place algebra", "[registration][stopset]")
{
    // Construction sorts and removes duplicates.
    reg::StopSet s = setOf({{3}, {1}, {2}, {2}, {1}});
    REQUIRE(s.size() == 3);
    CHECK(s.ids()[0].value == 1);
    CHECK(s.ids()[1].value == 2);
    CHECK(s.ids()[2].value == 3);

    // Single-element mutators report whether they changed the set.
    CHECK(s.insert(core::StopId{4}));
    CHECK_FALSE(s.insert(core::StopId{4}));
    CHECK(s.contains(core::StopId{4}));
    CHECK(s.erase(core::StopId{4}));
    CHECK_FALSE(s.contains(core::StopId{4}));
    CHECK(s.toggle(core::StopId{9}));       // now present
    CHECK_FALSE(s.toggle(core::StopId{9})); // toggled back off

    // Set algebra.
    const reg::StopSet a = setOf({{1}, {2}, {3}});
    const reg::StopSet b = setOf({{3}, {4}, {5}});
    CHECK((a | b) == setOf({{1}, {2}, {3}, {4}, {5}}));
    CHECK((a & b) == setOf({{3}}));
    CHECK((a - b) == setOf({{1}, {2}}));
}

TEST_CASE("Selectors resolve registration intent by family", "[registration][selector]")
{
    const caecilia::model::Organ organ = tests::buildTestOrgan();
    REQUIRE(organ.stops().size() == tests::stops::count);

    const reg::RegistrationState state; // nothing engaged
    const reg::SelectorParser    parser;

    const auto resolve = [&](const char* text)
    {
        const reg::SelectorParser::Result r = parser.parse(text);
        REQUIRE(r.ok);
        return r.selector.resolve(organ, state);
    };

    // family:reed -> the two reed stops (Trumpet, Oboe), and nothing else.
    const reg::StopSet reeds = resolve("family:reed");
    CHECK(reeds == setOf({tests::stops::trumpet8, tests::stops::oboe8}));

    // Principals and flutes partition the flue chorus.
    CHECK(resolve("family:principal") == setOf({tests::stops::principal8, tests::stops::octave4}));
    CHECK(resolve("family:flute") == setOf({tests::stops::twelfth, tests::stops::gedackt8}));

    // Intersection narrows to one division: only the Swell reed remains.
    CHECK(resolve("family:reed & div:swell") == setOf({tests::stops::oboe8}));

    // Union combines two families.
    CHECK(resolve("family:reed | family:flute")
          == setOf({tests::stops::twelfth, tests::stops::trumpet8,
                    tests::stops::oboe8, tests::stops::gedackt8}));

    // Complement is "everything but".
    CHECK(resolve("!family:reed")
          == setOf({tests::stops::principal8, tests::stops::octave4,
                    tests::stops::twelfth, tests::stops::gedackt8}));

    // Exact-rational footage selection: only the 2 2/3' quint matches.
    CHECK(resolve("pitch:8/3") == setOf({tests::stops::twelfth}));
    // ...as does selecting by mutation pitch class.
    CHECK(resolve("class:mutation") == setOf({tests::stops::twelfth}));

    // Whole division.
    CHECK(resolve("div:great")
          == setOf({tests::stops::principal8, tests::stops::octave4,
                    tests::stops::twelfth, tests::stops::trumpet8}));
}

TEST_CASE("A selector can name one stop, by the id everything else names it by",
          "[registration][selector]")
{
    // The grammar could say "the reeds on the Swell" and could not say "that one".
    // Every other surface in this instrument identifies a stop by id -- the host
    // parameters, the console, the saved session, the coupler jamb -- so a binding
    // that means THAT drawstop had to go through a name substring, which is
    // ambiguous by construction: this organ has a Trumpet and an Oboe, and a real
    // one has the same Trompette 8 on two divisions.
    const caecilia::model::Organ organ = tests::buildTestOrgan();
    const reg::RegistrationState state;
    const reg::SelectorParser    parser;

    const auto resolve = [&](const std::string& text)
    {
        const reg::SelectorParser::Result r = parser.parse(text);
        REQUIRE(r.ok);
        return r.selector.resolve(organ, state);
    };

    // One stop, exactly.
    for (const caecilia::model::Stop& s : organ.stops())
    {
        const reg::StopSet one = resolve("id:" + std::to_string(s.id().value));
        INFO("id:" << s.id().value << " (" << s.name() << ")");
        CHECK(one == setOf({s.id()}));
    }

    // It composes with the rest of the algebra like any other term.
    CHECK(resolve("id:" + std::to_string(tests::stops::trumpet8.value) + " | family:flute")
          == setOf({tests::stops::twelfth, tests::stops::trumpet8, tests::stops::gedackt8}));
    CHECK(resolve("!id:" + std::to_string(tests::stops::trumpet8.value)).size()
          == tests::stops::count - 1);

    // An id no stop carries resolves to nothing rather than to everything.
    CHECK(resolve("id:60000").empty());

    // And a malformed id is an error the user hears about, not a term that
    // quietly constrains nothing and matches the whole organ.
    CHECK_FALSE(parser.parse("id:").ok);
    CHECK_FALSE(parser.parse("id:reed").ok);
    CHECK_FALSE(parser.parse("id:12x").ok);
    CHECK_FALSE(parser.parse("id:1234567").ok);
}

TEST_CASE("SelectorParser handles the empty query and rejects bad input", "[registration][parser]")
{
    const caecilia::model::Organ organ = tests::buildTestOrgan();
    const reg::RegistrationState state;
    const reg::SelectorParser    parser;

    // The grammar is a versioned public contract.
    CHECK(reg::SelectorParser::version() == reg::GrammarVersion{1, 0});
    CHECK(reg::grammarVersionString() == "1.0");

    // Empty input is the universal atom: it matches every stop.
    const reg::SelectorParser::Result empty = parser.parse("   ");
    REQUIRE(empty.ok);
    CHECK(empty.selector.resolve(organ, state).size() == tests::stops::count);

    // Unknown values and dangling operators are reported, not thrown.
    const reg::SelectorParser::Result badFamily = parser.parse("family:bogus");
    CHECK_FALSE(badFamily.ok);
    CHECK_FALSE(badFamily.error.empty());

    const reg::SelectorParser::Result dangling = parser.parse("family:reed &");
    CHECK_FALSE(dangling.ok);
}

TEST_CASE("Registration supports command-sourced undo and redo", "[registration][history]")
{
    const caecilia::model::Organ organ = tests::buildTestOrgan();
    const reg::SelectorParser    parser;

    // The command log and the snapshot history (state == fold of the log).
    std::vector<reg::RegistrationState> history;
    history.emplace_back(); // s0: empty registration

    // Command 1: draw one stop.
    history.push_back(applyCommand(history.back(),
                                   reg::RegistrationCommand::engageStop(tests::stops::principal8),
                                   organ));

    // Command 2: draw all reeds by selector.
    const reg::SelectorParser::Result reeds = parser.parse("family:reed");
    REQUIRE(reeds.ok);
    history.push_back(applyCommand(history.back(),
                                   reg::RegistrationCommand::engageSelector(reeds.selector),
                                   organ));

    const reg::RegistrationState& s0 = history[0];
    const reg::RegistrationState& s1 = history[1];
    const reg::RegistrationState& s2 = history[2];

    CHECK(s0.drawnCount() == 0);
    CHECK(s1.drawnCount() == 1);
    CHECK(s1.isEngaged(tests::stops::principal8));
    CHECK(s2.drawnCount() == 3);
    CHECK(s2.isEngaged(tests::stops::principal8));
    CHECK(s2.isEngaged(tests::stops::trumpet8));
    CHECK(s2.isEngaged(tests::stops::oboe8));

    // Re-folding the same commands is deterministic (value equality).
    reg::RegistrationState expectedS1;
    expectedS1.engage(tests::stops::principal8);
    CHECK(s1 == expectedS1);

    reg::RegistrationState expectedS2 = expectedS1;
    expectedS2.engage(tests::stops::trumpet8);
    expectedS2.engage(tests::stops::oboe8);
    CHECK(s2 == expectedS2);

    // The forward transition (redo) and its inverse (undo) are exact mirrors —
    // this is the StateDelta the audio seam applies as a click-free crossfade.
    const reg::StateDelta forward = reg::computeDelta(s1, s2);
    CHECK(forward.numEngage == 2);
    CHECK(forward.numDisengage == 0);

    const reg::StateDelta undo = reg::computeDelta(s2, s1);
    CHECK(undo.numDisengage == 2);
    CHECK(undo.numEngage == 0);

    // Undo one step: the restored state equals the earlier snapshot exactly.
    reg::RegistrationState afterUndo = history[1];
    CHECK(afterUndo == expectedS1);

    // Redo: re-applying command 2 returns precisely to s2.
    const reg::RegistrationState afterRedo =
        applyCommand(afterUndo, reg::RegistrationCommand::engageSelector(reeds.selector), organ);
    CHECK(afterRedo == s2);

    // Clear collapses back to the empty registration.
    const reg::RegistrationState cleared =
        applyCommand(s2, reg::RegistrationCommand::clear(), organ);
    CHECK(cleared.drawnCount() == 0);
    CHECK(reg::computeDelta(s2, cleared).numDisengage == 3);
}
