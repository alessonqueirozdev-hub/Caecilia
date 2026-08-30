// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Where an organ file is allowed to look for its measured spectra.
 *
 * The model layer resolves nothing: it takes a resolver and knows no filesystem.
 * The plugin supplies one, and what it supplies is a policy -- an organ file is a
 * document that travels, somebody downloads one or is sent one, and a reference
 * that could name an absolute path or climb out with .. would be a document that
 * reads its recipient's disk.
 *
 * So these are as much about what an organ file cannot reach as about what it can.
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

const char* kSpectrum =
    "{ \"fundamentalHz\": 261.63, \"partials\": ["
    "  { \"ratioToF0\": 1.0, \"ampDb\": 0.0 },"
    "  { \"ratioToF0\": 2.0, \"ampDb\": -6.0 } ] }";

/// A directory that cleans itself up, with an organ file inside it.
struct OrganFolder
{
    juce::File dir;
    juce::File organ;

    explicit OrganFolder(const juce::String& spectrumRef)
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("caecilia-spec-" + juce::Uuid().toString()))
    {
        dir.createDirectory();
        organ = dir.getChildFile("organ.organ.json");

        model::OrganDefinition def =
            model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan());
        def.ranks.front().spectrum = spectrumRef.toStdString();
        organ.replaceWithText(juce::String(model::OrganLoader::serialize(def)));
    }

    ~OrganFolder() { dir.deleteRecursively(); }
};

/// @return whether the first rank of the loaded organ is voiced from a measurement.
bool firstRankIsMeasured(const CaeciliaAudioProcessor& processor)
{
    return ! processor.organ().ranks().empty()
        && processor.organ().ranks().front().measuredSpectrum().has_value();
}

} // namespace

TEST_CASE("A spectrum beside the organ file is found", "[plugin][spectrum]")
{
    JuceScope   scope;
    OrganFolder folder("montre.partials.json");
    folder.dir.getChildFile("montre.partials.json").replaceWithText(kSpectrum);

    CaeciliaAudioProcessor processor;
    const model::LoadDiagnostics d = processor.loadOrganFile(folder.organ);

    CHECK_FALSE(d.hasErrors());
    CHECK(firstRankIsMeasured(processor));
}

TEST_CASE("A spectrum in a subfolder is found", "[plugin][spectrum]")
{
    JuceScope   scope;
    OrganFolder folder("spectra/montre.partials.json");
    folder.dir.getChildFile("spectra").createDirectory();
    folder.dir.getChildFile("spectra/montre.partials.json").replaceWithText(kSpectrum);

    CaeciliaAudioProcessor processor;
    CHECK_FALSE(processor.loadOrganFile(folder.organ).hasErrors());
    CHECK(firstRankIsMeasured(processor));
}

TEST_CASE("An organ file cannot climb out of its own folder", "[plugin][spectrum]")
{
    JuceScope   scope;
    OrganFolder folder("../outside.json");

    // A readable file the organ must not reach, one level up from where it lives.
    const juce::File outside = folder.dir.getParentDirectory().getChildFile("outside.json");
    outside.replaceWithText(kSpectrum);

    CaeciliaAudioProcessor processor;
    const model::LoadDiagnostics d = processor.loadOrganFile(folder.organ);
    outside.deleteFile();

    // The organ still loads -- an unreadable spectrum is a warning, and the rank
    // speaks from the recipe -- but the file outside was NOT read. This is the
    // whole reason the resolver checks where the path landed instead of trusting
    // that it started relative.
    CHECK_FALSE(d.hasErrors());
    CHECK_FALSE(firstRankIsMeasured(processor));
}

TEST_CASE("An organ file cannot name an absolute path", "[plugin][spectrum]")
{
    JuceScope scope;

    const juce::File elsewhere = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("caecilia-abs-" + juce::Uuid().toString()
                                                   + ".json");
    elsewhere.replaceWithText(kSpectrum);

    OrganFolder folder(elsewhere.getFullPathName());

    CaeciliaAudioProcessor processor;
    const model::LoadDiagnostics d = processor.loadOrganFile(folder.organ);
    elsewhere.deleteFile();

    CHECK_FALSE(d.hasErrors());
    CHECK_FALSE(firstRankIsMeasured(processor));
}

TEST_CASE("A missing spectrum leaves the organ playable", "[plugin][spectrum]")
{
    JuceScope   scope;
    OrganFolder folder("montre.partials.json"); // named, never written

    CaeciliaAudioProcessor processor;
    const model::LoadDiagnostics d = processor.loadOrganFile(folder.organ);

    CHECK_FALSE(d.hasErrors());
    CHECK_FALSE(d.entries().empty()); // and it says so
    CHECK_FALSE(firstRankIsMeasured(processor));
    CHECK(processor.organ().stops().size() > 0);
}
