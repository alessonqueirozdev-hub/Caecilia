// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Asking for stops by description, through the parser that is tested.
 *
 * There were two selector implementations: registration::SelectorParser, a
 * versioned grammar with a test suite, used by the factory generals; and a simpler
 * one in JavaScript in the console, used by the omnibar -- which is to say, the
 * one every organist actually typed into was the one with no tests and the smaller
 * grammar.
 *
 * These test the seam that lets the console use the real one. What they check is
 * mostly what the JavaScript could not do at all: typed predicates, parentheses,
 * set difference, and `engaged`.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <algorithm>

using namespace caecilia;
using caecilia::plugin::CaeciliaAudioProcessor;

namespace
{

struct JuceScope
{
    juce::ScopedJuceInitialiser_GUI gui;
};

bool contains(const std::vector<core::StopId>& ids, core::StopId id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

TEST_CASE("A typed predicate picks out one family", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    const auto result = processor.selectStops("family:reed");
    CHECK(result.error.isEmpty());
    REQUIRE_FALSE(result.stops.empty());

    // Every one of them, not merely most: a selector that over-matches draws stops
    // the organist did not ask for, which on an organ is a wrong sound rather than
    // a wrong list.
    for (const core::StopId id : result.stops)
    {
        const model::Stop* stop = nullptr;
        for (const model::Stop& s : processor.organ().stops())
            if (s.id() == id)
                stop = &s;
        REQUIRE(stop != nullptr);
        CHECK(stop->family() == core::TonalFamily::Reed);
    }
}

TEST_CASE("engaged means what is drawn right now", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    // The one the console's own grammar could not express at all, and the reason
    // this is resolved against the LIVE registration rather than an empty one.
    const auto opening = processor.selectStops("engaged");
    CHECK(opening.error.isEmpty());

    const std::size_t before = opening.stops.size();

    // Draw something that was not drawn.
    core::StopId spare{ 0 };
    for (const model::Stop& s : processor.organ().stops())
        if (! processor.isStopEngaged(s.id()))
        {
            spare = s.id();
            break;
        }
    processor.toggleStop(spare);

    const auto after = processor.selectStops("engaged");
    CHECK(after.stops.size() == before + 1);
    CHECK(contains(after.stops, spare));
}

TEST_CASE("Parentheses and set difference are real operators", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    // `div:` matches the division's NAME, case-insensitively and by substring --
    // not its kind. "grand" finds the Grand-Orgue; "pedal" would find nothing on
    // this organ, whose pedal division is called Pédale and whose é is not an e.
    const auto all   = processor.selectStops("family:reed | family:mixture");
    const auto minus = processor.selectStops("(family:reed | family:mixture) - div:grand");

    CHECK(all.error.isEmpty());
    CHECK(minus.error.isEmpty());
    REQUIRE_FALSE(all.stops.empty());

    // A difference is a subset, and a strict one here because the Grand-Orgue has
    // both a reed and a mixture.
    for (const core::StopId id : minus.stops)
        CHECK(contains(all.stops, id));
    CHECK(minus.stops.size() < all.stops.size());

    for (const core::StopId id : minus.stops)
        for (const model::Stop& s : processor.organ().stops())
            if (s.id() == id)
                for (const model::Division& d : processor.organ().divisions())
                    if (d.id() == s.division())
                        CHECK(d.name().find("Grand") == std::string::npos);
}

TEST_CASE("A query that will not parse says why and where", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    const auto result = processor.selectStops("family:reed & (pitch:8");

    // Not an empty set. Returning nothing for a typo makes a mistyped expression
    // look like an organ with no reeds, and the organist would go looking at the
    // organ.
    REQUIRE(result.error.isNotEmpty());
    CHECK(result.stops.empty());
    CHECK(result.errorPos >= 0);
}

TEST_CASE("An unknown division matches nothing rather than everything", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    // A well-formed question with no answer on this organ. It must come back
    // empty and WITHOUT an error, because there is nothing wrong with the
    // expression -- and it must certainly not fall back to matching everything,
    // which on a drawstop selector would be a tutti nobody asked for.
    const auto result = processor.selectStops("div:bombardewerk");
    CHECK(result.error.isEmpty());
    CHECK(result.stops.empty());
}

TEST_CASE("Drawing by description is one gesture, not fourteen", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    const std::uint64_t before = processor.drawnStops();

    const std::size_t drew = processor.drawSelector("family:reed");
    REQUIRE(drew > 1);

    // Additive: it draws what was asked for and leaves the rest alone. An omnibar
    // that cleared the registration first would make every query a general cancel.
    CHECK((processor.drawnStops() & before) == before);
    for (const core::StopId id : processor.selectStops("family:reed").stops)
        CHECK(processor.isStopEngaged(id));

    // And ONE entry in the history. An organist who draws the reeds by
    // description and thinks better of it takes back the gesture, not the stops
    // one at a time -- which is what a loop over toggleStop would have left them.
    REQUIRE(processor.canUndoRegistration());
    processor.undoRegistration();
    CHECK(processor.drawnStops() == before);
}

TEST_CASE("Drawing an expression that will not parse changes nothing", "[plugin][selector]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    const std::uint64_t before = processor.drawnStops();

    CHECK(processor.drawSelector("family:reed & (pitch:8") == 0u);
    CHECK(processor.drawnStops() == before);

    // And it is not a move: there is nothing to take back, because nothing
    // happened. A history entry here would make undo do nothing visible once.
    CHECK_FALSE(processor.canUndoRegistration());
}
