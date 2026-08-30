// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Taking a registration back.
 *
 * registration::RegistrationHistory has been built and tested since it was
 * written, and reached by nothing. These are the tests for the wiring: what gets
 * recorded, what does not, and what an undo actually gives back.
 *
 * The distinction that matters throughout is between a GESTURE and a consequence.
 * Drawing a stop is a move; restoring a session, adopting an organ and an undo
 * itself are not, and a history that recorded them would either be full of noise
 * or unable to go anywhere.
 */

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/OrganLoader.h"
#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

using namespace caecilia;
using caecilia::plugin::CaeciliaAudioProcessor;

namespace
{

struct JuceScope
{
    juce::ScopedJuceInitialiser_GUI gui;
};

} // namespace

TEST_CASE("A drawn stop can be taken back", "[plugin][undo]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    // The organ opens on a registration of its own, and that is the root: there is
    // nothing before it to go back to.
    const std::uint64_t opening = processor.drawnStops();
    CHECK_FALSE(processor.canUndoRegistration());

    processor.toggleStop(core::StopId{ 2 });
    const std::uint64_t drawn = processor.drawnStops();
    REQUIRE(drawn != opening);
    REQUIRE(processor.canUndoRegistration());

    processor.undoRegistration();
    CHECK(processor.drawnStops() == opening);

    // And forward again, which is the half that makes undo safe to press: an
    // organist who takes something back and changes their mind has not lost it.
    REQUIRE(processor.canRedoRegistration());
    processor.redoRegistration();
    CHECK(processor.drawnStops() == drawn);
}

TEST_CASE("An undo is not itself a move", "[plugin][undo]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    const std::uint64_t opening = processor.drawnStops();
    processor.toggleStop(core::StopId{ 2 });
    processor.toggleStop(core::StopId{ 5 });

    // Two moves back reaches the opening. If an undo were recorded as a move of
    // its own, the second one would take back the first and the history would
    // oscillate between two states forever instead of walking backwards.
    processor.undoRegistration();
    processor.undoRegistration();
    CHECK(processor.drawnStops() == opening);
    CHECK_FALSE(processor.canUndoRegistration());
}

TEST_CASE("A coupler belongs to the same registration", "[plugin][undo]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    processor.toggleStop(core::StopId{ 2 });
    const std::uint64_t withStop = processor.drawnStops();

    processor.toggleCoupler(0);
    REQUIRE(processor.drawnCouplers() != 0u);

    // One step back takes back the coupler and leaves the stop, because they were
    // two separate gestures. What must NOT happen is the stops moving with it: an
    // undo that gave back a sound the organist never had would be worse than none.
    processor.undoRegistration();
    CHECK(processor.drawnCouplers() == 0u);
    CHECK(processor.drawnStops() == withStop);
}

TEST_CASE("Restoring a session is not a move to take back", "[plugin][undo]")
{
    JuceScope scope;

    juce::MemoryBlock session;
    {
        CaeciliaAudioProcessor first;
        first.toggleStop(core::StopId{ 2 });
        first.getStateInformation(session);
    }

    CaeciliaAudioProcessor second;
    second.setStateInformation(session.getData(), static_cast<int>(session.getSize()));

    // Reopening a project is where the organist STARTS, not something they did.
    // An undo here would take them back to an organ they never played this
    // session -- the previous document's registration, or the factory plenum.
    CHECK_FALSE(second.canUndoRegistration());
}

TEST_CASE("A new organ starts a new history", "[plugin][undo]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    processor.toggleStop(core::StopId{ 2 });
    REQUIRE(processor.canUndoRegistration());

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("caecilia-undo-" + juce::Uuid().toString()
                                              + ".organ.json");
    file.replaceWithText(juce::String(model::OrganLoader::serialize(
        model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan()))));
    const bool ok = ! processor.loadOrganFile(file).hasErrors();
    file.deleteFile();
    REQUIRE(ok);

    // Every node in the old history holds stop ids of the old organ, and an id is
    // a position in a table that has just been replaced. Undoing across an organ
    // change would not give back the sound the organist had -- it would draw
    // whatever now sits in those slots.
    CHECK_FALSE(processor.canUndoRegistration());
    CHECK_FALSE(processor.canRedoRegistration());
}
