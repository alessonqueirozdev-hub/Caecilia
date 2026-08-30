// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief The first tests that run against the plugin itself rather than the core.
 *
 * Everything under @c caecilia_core has been testable from the beginning because
 * it links nothing but the standard library. The plugin has not, and the gap grew
 * teeth as the plugin gained logic of its own: MIDI learn, adopting a new organ,
 * and what a session does and does not remember. Every defect found in that layer
 * so far was found by reading it, which is not a method that scales.
 *
 * This target links the plugin's shared code and drives @c CaeciliaAudioProcessor
 * directly -- no host, no editor, no audio device. What it can reach is the whole
 * message-thread surface: state, loading, registration and the console commands.
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

/// A MessageManager for the duration of one test.
///
/// An AudioProcessor is a message-thread object even with no window: it owns a
/// ValueTree with listeners and JUCE asserts if the manager is not there.
struct JuceScope
{
    juce::ScopedJuceInitialiser_GUI gui;
};

/// A real, valid organ document on disk, deleted when the test ends.
///
/// The instrument's own organ written out through the file format, so this is not
/// a hand-made fixture that only resembles one: it is the document a user gets
/// from `caecilia-organ-file --export`.
struct TempOrganFile
{
    juce::File file;

    TempOrganFile()
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("caecilia-test-" + juce::Uuid().toString() + ".organ.json"))
    {
        const std::string document = model::OrganLoader::serialize(
            model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan()));
        file.replaceWithText(juce::String(document));
    }

    ~TempOrganFile() { file.deleteFile(); }
};

/// Save a session, then restore it into a fresh processor.
///
/// Round-tripping through a SECOND processor rather than reading the blob keeps
/// these tests independent of how the state is serialised -- what is asserted is
/// what a host actually does, which is hand the bytes back to a new instance.
juce::MemoryBlock save(CaeciliaAudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return block;
}

} // namespace

TEST_CASE("A session remembers which organ it was written on", "[plugin][organ]")
{
    JuceScope     scope;
    TempOrganFile organFile;

    CaeciliaAudioProcessor first;
    REQUIRE_FALSE(first.loadOrganFile(organFile.file).hasErrors());
    REQUIRE(first.organPath() == organFile.file.getFullPathName());

    const juce::MemoryBlock session = save(first);

    CaeciliaAudioProcessor second;
    second.setStateInformation(session.getData(), static_cast<int>(session.getSize()));

    CHECK(second.organPath() == organFile.file.getFullPathName());
    CHECK(second.unresolvedOrganPath().isEmpty());
}

TEST_CASE("Saving a project whose organ file is missing does not lose it", "[plugin][organ]")
{
    JuceScope scope;

    juce::String path;
    juce::MemoryBlock session;
    {
        TempOrganFile organFile;
        path = organFile.file.getFullPathName();

        CaeciliaAudioProcessor first;
        REQUIRE_FALSE(first.loadOrganFile(organFile.file).hasErrors());
        session = save(first);
    } // and now the file is gone, exactly as a moved or unmounted one would be

    // Opening it keeps the organ that was playing, which was already right: a
    // project must not silently start sounding like a different instrument.
    CaeciliaAudioProcessor second;
    second.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    CHECK(second.organPath().isEmpty());
    CHECK(second.unresolvedOrganPath() == path);

    // What was NOT right: saving from there rewrote the project to the built-in
    // organ, and the reference to the user's file was gone for good. Open a
    // project on a machine where the drive is not mounted, press save, and you
    // had lost which organ it was written on.
    const juce::MemoryBlock resaved = save(second);

    CaeciliaAudioProcessor third;
    third.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
    CHECK(third.unresolvedOrganPath() == path);
}

TEST_CASE("Choosing the built-in organ releases the missing one", "[plugin][organ]")
{
    JuceScope scope;

    juce::MemoryBlock session;
    {
        TempOrganFile          organFile;
        CaeciliaAudioProcessor first;
        REQUIRE_FALSE(first.loadOrganFile(organFile.file).hasErrors());
        session = save(first);
    }

    CaeciliaAudioProcessor second;
    second.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
    REQUIRE(second.unresolvedOrganPath().isNotEmpty());

    // Holding the path is for a file that is missing, not for one the organist has
    // decided against. Reverting is a decision, and keeping the old reference past
    // it would be second-guessing them -- the next save would quietly point the
    // project back at an organ they just left.
    second.loadBuiltInOrgan();
    CHECK(second.unresolvedOrganPath().isEmpty());

    const juce::MemoryBlock resaved = save(second);
    CaeciliaAudioProcessor  third;
    third.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
    CHECK(third.unresolvedOrganPath().isEmpty());
    CHECK(third.organPath().isEmpty());
}

TEST_CASE("Adopting an organ does not carry the old one's registration", "[plugin][organ]")
{
    JuceScope     scope;
    TempOrganFile organFile;

    CaeciliaAudioProcessor processor;

    // Draw something, so there is a registration that could be carried across.
    processor.toggleStop(core::StopId{ 0 });
    REQUIRE(processor.drawnStops() != 0u);

    REQUIRE_FALSE(processor.loadOrganFile(organFile.file).hasErrors());

    // A stop id, a coupler index and a combination are positions in tables that
    // have just been replaced. Carrying one across draws whatever now happens to
    // sit in that slot, which is a different stop with the same number.
    CHECK(processor.drawnCouplers() == 0u);
    CHECK(processor.organ().name() == model::buildCaeciliaDemoOrgan().name());
}
