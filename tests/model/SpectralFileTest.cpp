// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Reading a measured pipe spectrum, and voicing a rank from it.
 *
 * caecilia-partial-extractor has been able to FFT a real pipe into a partial bank
 * since it was written, and nothing could read the file it produced -- so every
 * rank in every organ sounded from the procedural recipe however carefully anyone
 * had measured a real one. These cover the other half of that path.
 */

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/OrganLoader.h"
#include "caecilia/model/SpectralModelFile.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>
#include <string>

using namespace caecilia;

namespace
{

/// MSVC's traditional preprocessor mangles a raw string inside a macro argument,
/// so documents are written with single quotes and translated here.
std::string j(std::string text)
{
    for (char& c : text)
        if (c == '\'')
            c = '"';
    return text;
}

/// A two-partial measurement: a fundamental and an octave 6 dB below it.
std::string simpleSpectrum(float peakDb = 0.0f)
{
    return j("{ 'fundamentalHz': 261.63, 'partialCount': 2, 'partials': ["
             "  { 'ratioToF0': 1.0, 'ampDb': " + std::to_string(peakDb)
             + ", 'phase': 0.0, 'windSensitivity': 0.4 },"
               "  { 'ratioToF0': 2.0, 'ampDb': " + std::to_string(peakDb - 6.0f)
             + ", 'phase': 0.0, 'windSensitivity': 0.5 } ] }");
}

/// A one-rank organ whose rank optionally names a spectrum.
model::OrganDefinition organNaming(const std::string& spectrumRef)
{
    model::OrganDefinition def;
    def.name = "Measured";

    model::WindchestDef w;
    w.name = "Chest";
    def.windchests.push_back(w);

    model::RankDef r;
    r.name      = "Montre";
    r.windchest = "Chest";
    r.spectrum  = spectrumRef;
    def.ranks.push_back(r);

    model::DivisionDef d;
    d.name = "Manual";
    def.divisions.push_back(d);

    model::StopDef s;
    s.name     = "Montre 8";
    s.division = "Manual";
    s.rank     = "Montre";
    def.stops.push_back(s);

    return def;
}

/// A resolver backed by a map, which is what the model layer is meant to take:
/// it knows nothing about filesystems, so a test does not need one either.
model::ResourceResolver mapResolver(std::map<std::string, std::string> files)
{
    return [files](std::string_view path) -> std::optional<std::string>
    {
        const auto it = files.find(std::string(path));
        return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
}

} // namespace

TEST_CASE("A measured spectrum reads back what was measured", "[model][spectrum]")
{
    const model::SpectralModelLoad loaded = model::loadSpectralModel(simpleSpectrum(), "montre.json");

    REQUIRE(loaded.ok());
    REQUIRE(loaded.model->partials.size() == 2);
    CHECK(loaded.model->fundamentalHz == 261.63f);
    CHECK(loaded.model->partials[1].ratioToF0 == 2.0f);
    CHECK(loaded.model->partials[1].ampDb == -6.0f);
    CHECK(loaded.model->partials[1].windSensitivity == 0.5f);

    // What today's extractor does not write. An absent onset is no stagger and an
    // absent brightness track is none, which is what a measurement that did not
    // look for them implies -- not a reason to refuse the file.
    CHECK(loaded.model->partials[0].onsetSeconds == 0.0f);
}

TEST_CASE("A spectrum without a fundamental is refused", "[model][spectrum]")
{
    // Not defaulted to anything. A spectrum whose fundamental is unknown cannot be
    // folded onto another footage, and guessing one would silently transpose
    // somebody's measurement.
    const auto loaded = model::loadSpectralModel(j("{ 'partials': [ { 'ratioToF0': 1, 'ampDb': 0 } ] }"),
                                          "x.json");
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.diagnostics.hasErrors());
}

TEST_CASE("A spectrum with no partials is refused", "[model][spectrum]")
{
    const auto loaded = model::loadSpectralModel(j("{ 'fundamentalHz': 440, 'partials': [] }"), "x.json");
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.diagnostics.hasErrors());
}

TEST_CASE("A partialCount that disagrees is a warning, not a refusal", "[model][spectrum]")
{
    const auto loaded = model::loadSpectralModel(
        j("{ 'fundamentalHz': 440, 'partialCount': 9,"
          "  'partials': [ { 'ratioToF0': 1, 'ampDb': 0 } ] }"), "x.json");

    // The array is the timbre and the count is a convenience beside it. A file
    // somebody hand-edited and did not finish still loads -- and says so, because
    // silence there would hide the edit.
    CHECK(loaded.ok());
    CHECK_FALSE(loaded.diagnostics.hasErrors());
    CHECK_FALSE(loaded.diagnostics.entries().empty());
}

TEST_CASE("A rank can be voiced from a measurement", "[model][spectrum]")
{
    const model::CompileResult result =
        model::OrganLoader::compile(organNaming("montre.json"),
                                    mapResolver({ { "montre.json", simpleSpectrum() } }));

    REQUIRE(result.ok());
    REQUIRE(result.organ->ranks().size() == 1);

    const model::Rank& rank = result.organ->ranks().front();
    REQUIRE(rank.measuredSpectrum().has_value());
    CHECK(rank.measuredSpectrum()->partials.size() == 2);
    CHECK(rank.spectrumFile() == "montre.json");
}

TEST_CASE("A spectrum that cannot be opened leaves the rank speaking", "[model][spectrum]")
{
    const model::CompileResult result =
        model::OrganLoader::compile(organNaming("montre.json"), mapResolver({}));

    // A warning, not an error. The rank still sounds -- from the procedural recipe
    // -- and refusing a whole organ because one measurement was left behind would
    // be the worse trade. Saying nothing would be worse still: an organist would
    // wonder why their measured Montre sounds like everyone else's.
    REQUIRE(result.ok());
    CHECK_FALSE(result.diagnostics.hasErrors());
    CHECK_FALSE(result.diagnostics.entries().empty());
    CHECK_FALSE(result.organ->ranks().front().measuredSpectrum().has_value());

    // And the reference survives, so a document that travelled without its spectra
    // does not quietly lose them on the first save.
    CHECK(result.organ->ranks().front().spectrumFile() == "montre.json");
}

TEST_CASE("A caller with no resolver is told, not ignored", "[model][spectrum]")
{
    const model::CompileResult result = model::OrganLoader::compile(organNaming("montre.json"));

    REQUIRE(result.ok());
    CHECK_FALSE(result.diagnostics.entries().empty());
}

TEST_CASE("The spectrum reference survives a round trip", "[model][spectrum]")
{
    const model::CompileResult built =
        model::OrganLoader::compile(organNaming("ranks/montre.json"),
                                    mapResolver({ { "ranks/montre.json", simpleSpectrum() } }));
    REQUIRE(built.ok());

    const std::string document =
        model::OrganLoader::serialize(model::OrganLoader::definitionFrom(*built.organ));

    const model::ParseResult reparsed = model::OrganLoader::parse(document);
    REQUIRE(reparsed.ok());
    CHECK(reparsed.definition->ranks.front().spectrum == "ranks/montre.json");
}

TEST_CASE("A measurement contributes its shape, not its level", "[model][spectrum]")
{
    // The same spectrum recorded twenty decibels louder. Absolute level is
    // whatever the person with the microphone chose; it is not this rank's place
    // in the organ, and if it were, drawing a measured Montre beside a procedural
    // Bourdon would sound like whichever of them was recorded louder.
    const auto quiet = model::OrganLoader::compile(
        organNaming("s.json"), mapResolver({ { "s.json", simpleSpectrum(0.0f) } }));
    const auto loud = model::OrganLoader::compile(
        organNaming("s.json"), mapResolver({ { "s.json", simpleSpectrum(20.0f) } }));

    REQUIRE(quiet.ok());
    REQUIRE(loud.ok());

    const synth::RankVoicing a = model::buildRankVoicing(*quiet.organ, core::StopId{ 0 });
    const synth::RankVoicing b = model::buildRankVoicing(*loud.organ,  core::StopId{ 0 });

    REQUIRE(a.spectrum.partials.size() == b.spectrum.partials.size());
    REQUIRE_FALSE(a.spectrum.partials.empty());
    for (std::size_t i = 0; i < a.spectrum.partials.size(); ++i)
        CHECK(a.spectrum.partials[i].ampDb == b.spectrum.partials[i].ampDb);
}

TEST_CASE("A measured rank does not sound like the procedural one", "[model][spectrum]")
{
    // The point of the whole path. Two partials at 0 and -6 dB is not what the
    // Principal recipe produces, and if the voicing came out the same the
    // measurement would be being read and thrown away.
    const auto measured = model::OrganLoader::compile(
        organNaming("s.json"), mapResolver({ { "s.json", simpleSpectrum() } }));
    const auto procedural = model::OrganLoader::compile(organNaming(""));

    REQUIRE(measured.ok());
    REQUIRE(procedural.ok());

    const synth::RankVoicing m = model::buildRankVoicing(*measured.organ,   core::StopId{ 0 });
    const synth::RankVoicing p = model::buildRankVoicing(*procedural.organ, core::StopId{ 0 });

    CHECK(m.spectrum.partials.size() != p.spectrum.partials.size());
}
