// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Changing the instrument while somebody is playing it.
 *
 * Loading an organ rebuilds everything: the voice pool, the wind, the buses, the
 * channel map. An organist does that between pieces. A host does it whenever it
 * feels like restoring state, which can be mid-block, mid-note, and while the
 * console still believes six keys are down.
 *
 * Nothing had asked what happens then. The core suite has held-note tests, but
 * they hold notes across a REGISTRATION change, which keeps the same organ; this
 * is the one where the pipes themselves are replaced under the fingers.
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

/// A valid organ that is visibly NOT the built-in one.
///
/// Renamed, and two ranks shorter. Loading a byte-identical copy of the organ
/// already playing would hide any defect where the swap does not take effect --
/// everything would look right because nothing had to change.
constexpr const char* kSwapOrganName = "Swap Test Organ";

struct TempOrganFile
{
    juce::File file;

    TempOrganFile()
        : file(juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("caecilia-swap-" + juce::Uuid().toString() + ".organ.json"))
    {
        model::OrganDefinition def =
            model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan());
        def.name = kSwapOrganName;

        // Drop the last two stops, so the stop table really is a different shape
        // and not just a different label on the same one.
        if (def.stops.size() > 2)
            def.stops.resize(def.stops.size() - 2);

        file.replaceWithText(juce::String(model::OrganLoader::serialize(def)));
    }

    ~TempOrganFile() { file.deleteFile(); }
};

/// An organ whose manual starts at middle C.
///
/// Nothing else here distinguishes a rebuilt engine from a stale one: two organs
/// that differ only in their names render identically, so a test that loads one
/// cannot tell whether the load reached the engine at all. A different COMPASS
/// can be heard -- a key below it has no pipe.
constexpr int kHighCompassLow = 60;

juce::File writeHighCompassOrgan()
{
    model::OrganDefinition def =
        model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan());
    def.name = "High Compass Organ";
    for (model::DivisionDef& d : def.divisions)
        d.lowNote = kHighCompassLow;
    for (model::RankDef& r : def.ranks)
        r.lowNote = kHighCompassLow;

    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("caecilia-compass-" + juce::Uuid().toString()
                                           + ".organ.json");
    f.replaceWithText(juce::String(model::OrganLoader::serialize(def)));
    return f;
}

/// A one-manual organ, written from scratch rather than trimmed from the demo.
///
/// The point is that its DIVISIONS are different, not just fewer stops: a play
/// division, a channel map and a compass are all positions in a table that this
/// organ replaces with a shorter one.
juce::File writeSingleManualOrgan()
{
    model::OrganDefinition def;
    def.name = "One Manual";

    model::WindchestDef w;
    w.name = "Chest";
    def.windchests.push_back(w);

    model::RankDef r;
    r.name      = "Montre";
    r.windchest = "Chest";
    def.ranks.push_back(r);

    model::DivisionDef d;
    d.name        = "Manual";
    d.midiChannel = 0;
    def.divisions.push_back(d);

    model::StopDef s;
    s.name     = "Montre 8";
    s.division = "Manual";
    s.rank     = "Montre";
    def.stops.push_back(s);

    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("caecilia-one-" + juce::Uuid().toString()
                                           + ".organ.json");
    f.replaceWithText(juce::String(model::OrganLoader::serialize(def)));
    return f;
}

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 256;

/// Prepare the processor the way a host does.
///
/// prepareToPlay is the CALLBACK; a host calls setRateAndBufferSizeDetails first,
/// and that is what makes getSampleRate() answer. Calling only the callback leaves
/// the processor believing it has no sample rate -- which is not academic here,
/// because adoptOrgan asks getSampleRate() whether it is worth rebuilding the
/// engine, and silently did not.
void prepare(CaeciliaAudioProcessor& processor)
{
    processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    processor.prepareToPlay(kSampleRate, kBlock);
}


/// Render @p blocks, optionally opening with @p message; return the peak.
float render(CaeciliaAudioProcessor& processor, int blocks,
             const juce::MidiMessage* message = nullptr)
{
    juce::AudioBuffer<float> audio(2, kBlock);
    float peak = 0.0f;

    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi;
        if (i == 0 && message != nullptr)
            midi.addEvent(*message, 0);

        processor.processBlock(audio, midi);
        peak = juce::jmax(peak, audio.getMagnitude(0, kBlock));
    }
    return peak;
}

} // namespace

TEST_CASE("An organ can be changed under a held note", "[plugin][organ]")
{
    JuceScope              scope;
    TempOrganFile          organFile;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // A key is down and a pipe is speaking.
    const juce::MidiMessage on = juce::MidiMessage::noteOn(1, 60, 0.8f);
    REQUIRE(render(processor, 24, &on) > 0.0f);

    // The organist changes instrument without lifting their hand -- or a host
    // restores a project while the sustain is down. Either way the pipes this note
    // is sounding are about to stop existing.
    REQUIRE_FALSE(processor.loadOrganFile(organFile.file).hasErrors());
    REQUIRE(processor.organ().name() == juce::String(kSwapOrganName).toStdString());

    // The note that was held belonged to an organ that is gone. It must not go on
    // sounding out of a voice pool that has been rebuilt, and the note-off for it
    // -- which is still coming, because the key is still down -- must not find
    // anything to be surprised by.
    const juce::MidiMessage off = juce::MidiMessage::noteOff(1, 60);
    render(processor, 8, &off);

    // And the instrument still plays. A swap that leaves it mute would be a much
    // quieter failure than a crash, and much easier to ship.
    const juce::MidiMessage again = juce::MidiMessage::noteOn(1, 62, 0.8f);
    CHECK(render(processor, 24, &again) > 0.0f);
}

TEST_CASE("Reverting to the built-in organ mid-note leaves it playable", "[plugin][organ]")
{
    JuceScope              scope;
    TempOrganFile          organFile;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    REQUIRE_FALSE(processor.loadOrganFile(organFile.file).hasErrors());

    const juce::MidiMessage on = juce::MidiMessage::noteOn(1, 60, 0.8f);
    REQUIRE(render(processor, 24, &on) > 0.0f);

    // The revert is the escape hatch for a file that turned out to be wrong, so it
    // is the one an organist reaches for while something is going audibly wrong --
    // which is exactly when it must not make things worse.
    processor.loadBuiltInOrgan();
    render(processor, 8);
    REQUIRE(processor.organ().name() != juce::String(kSwapOrganName).toStdString());

    const juce::MidiMessage again = juce::MidiMessage::noteOn(1, 64, 0.8f);
    CHECK(render(processor, 24, &again) > 0.0f);
}

TEST_CASE("An organ that fails to load leaves the one playing untouched", "[plugin][organ]")
{
    JuceScope              scope;
    TempOrganFile          organFile;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // Starting from a LOADED organ, not the built-in one. Written the other way
    // round this test could not fail: falling back to the built-in organ is
    // invisible when the built-in organ is what was already playing, which is
    // precisely the mutation it is here to catch.
    REQUIRE_FALSE(processor.loadOrganFile(organFile.file).hasErrors());

    const juce::MidiMessage on = juce::MidiMessage::noteOn(1, 60, 0.8f);
    REQUIRE(render(processor, 24, &on) > 0.0f);

    // A document that will not parse. The instrument that is currently sounding
    // must not be disturbed by it at all: a mistyped filename is not a reason to
    // stop the organ in the middle of a phrase.
    const juce::File bad = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("caecilia-bad-" + juce::Uuid().toString() + ".json");
    bad.replaceWithText("{ this is not an organ ]");

    const model::LoadDiagnostics d = processor.loadOrganFile(bad);
    bad.deleteFile();

    CHECK(d.hasErrors());
    CHECK(processor.organPath() == organFile.file.getFullPathName());
    CHECK(processor.organ().name() == juce::String(kSwapOrganName).toStdString());
    CHECK(render(processor, 8) > 0.0f); // still speaking, note never released
}

TEST_CASE("Loading an organ reaches the engine, not just the model", "[plugin][organ]")
{
    JuceScope scope;

    const juce::MidiMessage low = juce::MidiMessage::noteOn(1, 48, 0.8f);

    // Two processors rather than one, and not for tidiness: an organ's release and
    // its reverb ring for seconds, so measuring silence on an instrument that has
    // just played the same note measures the tail of that note. The first sounded
    // this key at all; the second must not.
    {
        CaeciliaAudioProcessor asBuilt;
        prepare(asBuilt);
        REQUIRE(render(asBuilt, 24, &low) > 0.0f);
    }

    CaeciliaAudioProcessor processor;
    prepare(processor);

    const juce::File file = writeHighCompassOrgan();
    const bool       ok   = ! processor.loadOrganFile(file).hasErrors();
    file.deleteFile();
    REQUIRE(ok);

    // On an organ whose manual starts at middle C, a key below it has no pipe. That
    // is the only thing here a stale engine cannot fake: two organs differing by a
    // name render identically, so a load that never reached the voice pool would
    // look exactly like one that did.
    CHECK(render(processor, 24, &low) == 0.0f);

    const juce::MidiMessage inRange = juce::MidiMessage::noteOn(1, 72, 0.8f);
    CHECK(render(processor, 24, &inRange) > 0.0f);
}

TEST_CASE("A smaller organ does not leave the console on a division that is gone",
          "[plugin][organ]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // The instrument starts on a three-division organ, playing the Grand-Orgue,
    // which is division one. The organ that replaces it has a single division, and
    // that is division zero.
    const core::DivisionId wasPlaying = processor.playDivision();

    const juce::File file = writeSingleManualOrgan();
    const bool       ok   = ! processor.loadOrganFile(file).hasErrors();
    file.deleteFile();
    REQUIRE(ok);

    REQUIRE(processor.organ().divisions().size() == 1);

    // A play division carried across from the old organ is an index into a table
    // that no longer has that row: the console would be playing a manual this
    // organ does not have, which is silence with no error anywhere.
    bool playable = false;
    for (const model::Division& d : processor.organ().divisions())
        if (d.id() == processor.playDivision())
            playable = true;
    CHECK(playable);
    CHECK(processor.playDivision().value != wasPlaying.value);

    const juce::MidiMessage note = juce::MidiMessage::noteOn(1, 60, 0.8f);
    CHECK(render(processor, 24, &note) > 0.0f);
}
