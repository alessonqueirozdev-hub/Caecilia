// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Reading and writing an organ file.
//
// OrganLoader::compile has always turned an OrganDefinition into a working Organ
// -- windchests, ranks, pipes, divisions, manuals, stops, couplers. What was
// missing was the half that gets a definition off a disk: parse() reported "not
// yet implemented" and serialize() returned an empty string. So the instrument
// could only ever be the one organ compiled into the binary, which is a larger
// gap than any voicing refinement -- it is the difference between a virtual organ
// and a platform for them.
//
// What is checked here is the whole journey: a document becomes a definition,
// the definition compiles to an organ that sounds the right pipes, and writing it
// back produces a document that reads as the same thing. Plus the diagnostics,
// because a file a person edits by hand will be wrong, and an error message that
// names the field and the line is most of what makes it fixable.
//

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/registration/FactoryGenerals.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/OrganDefinition.h"
#include "caecilia/model/OrganLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace registration = caecilia::registration;

namespace
{
/// Single quotes to double, so a JSON document reads as one in C++ source. See
/// JsonTest.cpp: MSVC's traditional preprocessor cannot see a raw string inside a
/// macro argument.
std::string j(std::string s)
{
    for (char& c : s)
        if (c == '\'')
            c = '"';
    return s;
}

/// A small but complete organ: two chests, three ranks, two divisions, three
/// stops (one compound), one coupler.
std::string sampleDocument()
{
    return j(
        "{\n"
        "  'name': 'Test Organ',\n"
        "  'builder': 'Nobody',\n"
        "  'year': 1890,\n"
        "  'windchests': [\n"
        "    {'name': 'Main',  'pressurePa': 820},\n"
        "    {'name': 'Swell', 'pressurePa': 735, 'tremulant': true}\n"
        "  ],\n"
        "  'ranks': [\n"
        "    {'name': 'Principal', 'family': 'Principal', 'footage': 8,\n"
        "     'windchest': 'Main', 'lowNote': 36, 'highNote': 96,\n"
        "     'voicing': {'brightness': 0.8, 'chiff': 0.2}},\n"
        "    {'name': 'Quint',     'family': 'Flute', 'footage': [8, 3],\n"
        "     'windchest': 'Main'},\n"
        "    {'name': 'Oboe',      'family': 'Reed',  'footage': 8,\n"
        "     'windchest': 'Swell', 'pan': -0.4, 'distanceM': 12}\n"
        "  ],\n"
        "  'divisions': [\n"
        "    {'name': 'Great', 'kind': 'Manual', 'manual': 0},\n"
        "    {'name': 'Swell', 'kind': 'Manual', 'manual': 1, 'enclosed': true,\n"
        "     'tremulant': true, 'midiChannel': 1}\n"
        "  ],\n"
        "  'stops': [\n"
        "    {'name': 'Principal 8', 'family': 'Principal', 'footage': 8,\n"
        "     'division': 'Great', 'rank': 'Principal', 'role': 'Foundation'},\n"
        "    {'name': 'Nazard 2 2/3', 'family': 'Flute', 'footage': [8, 3],\n"
        "     'division': 'Great', 'rank': 'Quint', 'pitchClass': 'Mutation'},\n"
        "    {'name': 'Mixture III', 'family': 'Mixture', 'footage': 2,\n"
        "     'division': 'Swell', 'rank': 'Oboe', 'pitchClass': 'Compound',\n"
        "     'mixture': [2, [8, 3], 1]}\n"
        "  ],\n"
        "  'couplers': [\n"
        "    {'name': 'Swell/Great', 'from': 'Swell', 'to': 'Great'}\n"
        "  ]\n"
        "}");
}

/// Every diagnostic, joined, for an INFO line.
std::string describe(const model::LoadDiagnostics& d)
{
    std::string out;
    for (const model::Diagnostic& x : d.entries())
    {
        if (!out.empty())
            out += " | ";
        out += x.context + ": " + x.message;
    }
    return out;
}
} // namespace

TEST_CASE("A document becomes an organ", "[model][organfile]")
{
    const model::ParseResult parsed =
        model::OrganLoader::parse(sampleDocument(), model::OrganFileFormat::Auto,
                                  "test.organ.json");

    INFO(describe(parsed.diagnostics));
    REQUIRE(parsed.ok());

    const model::OrganDefinition& def = *parsed.definition;
    CHECK(def.name == "Test Organ");
    CHECK(def.builder == "Nobody");
    CHECK(def.year == 1890);
    REQUIRE(def.windchests.size() == 2);
    REQUIRE(def.ranks.size() == 3);
    REQUIRE(def.divisions.size() == 2);
    REQUIRE(def.stops.size() == 3);
    REQUIRE(def.couplers.size() == 1);

    // A footage written as a bare number and one written as a ratio.
    CHECK(def.ranks[0].footageNum == 8);
    CHECK(def.ranks[0].footageDen == 1);
    CHECK(def.ranks[1].footageNum == 8);
    CHECK(def.ranks[1].footageDen == 3);

    // Fields that were absent kept their defaults; fields that were present did not.
    CHECK(def.ranks[0].voicing.brightness == Approx(0.8f));
    CHECK(def.ranks[0].voicing.chiff == Approx(0.2f));
    CHECK(def.ranks[0].voicing.windSensitivity
          == Approx(model::VoicingDef{}.windSensitivity));
    CHECK(def.ranks[2].pan == Approx(-0.4f));

    CHECK(def.windchests[1].tremulant);
    CHECK(def.divisions[1].enclosed);
    CHECK(def.divisions[1].midiChannel == 1);

    // A compound stop's constituent footages, in both spellings.
    REQUIRE(def.stops[2].mixture.size() == 3);
    CHECK(def.stops[2].mixture[0] == std::make_pair(2, 1));
    CHECK(def.stops[2].mixture[1] == std::make_pair(8, 3));
    CHECK(def.stops[2].mixture[2] == std::make_pair(1, 1));

    // And it compiles into an instrument that can actually be played.
    const model::CompileResult built = model::OrganLoader::compile(def);
    INFO(describe(built.diagnostics));
    REQUIRE(built.ok());
    CHECK(built.organ->stops().size() == 3);
    CHECK(built.organ->divisions().size() == 2);
    CHECK(built.organ->ranks().size() == 3);
    CHECK(built.organ->couplers().size() == 1);
    CHECK(built.organ->name() == "Test Organ");

    // The names resolved to ids: the Oboe sits on the Swell chest, not the Main.
    CHECK(built.organ->ranks()[2].windchest().value == 1);
    CHECK(built.organ->windchests()[1].nominalPressurePa == Approx(735.0f));
}

TEST_CASE("load parses and compiles in one step", "[model][organfile]")
{
    const model::CompileResult r = model::OrganLoader::load(sampleDocument());
    INFO(describe(r.diagnostics));
    REQUIRE(r.ok());
    CHECK(r.organ->stops().size() == 3);
}

TEST_CASE("What is written reads back as the same organ", "[model][organfile]")
{
    const model::ParseResult first = model::OrganLoader::parse(sampleDocument());
    REQUIRE(first.ok());

    const std::string written = model::OrganLoader::serialize(*first.definition);
    REQUIRE_FALSE(written.empty());

    const model::ParseResult second =
        model::OrganLoader::parse(written, model::OrganFileFormat::Json, "written");
    INFO("written document:\n" << written << "\n" << describe(second.diagnostics));
    REQUIRE(second.ok());

    // Writing the second one again must give byte-identical text: that is the
    // strongest round-trip statement available, and it fails if any field is lost,
    // reordered or reformatted on the way through.
    CHECK(model::OrganLoader::serialize(*second.definition) == written);

    // And the organ it compiles to is the same instrument.
    const model::CompileResult a = model::OrganLoader::compile(*first.definition);
    const model::CompileResult b = model::OrganLoader::compile(*second.definition);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a.organ->stops().size() == b.organ->stops().size());
    for (std::size_t i = 0; i < a.organ->stops().size(); ++i)
    {
        CHECK(a.organ->stops()[i].name() == b.organ->stops()[i].name());
        CHECK(a.organ->stops()[i].footage() == b.organ->stops()[i].footage());
        CHECK(a.organ->stops()[i].family() == b.organ->stops()[i].family());
    }
}

TEST_CASE("A written document says only what the builder chose",
          "[model][organfile]")
{
    // An organ file is a document a person reads. A rank that spells out eight
    // voicing parameters it never chose buries the two it did.
    model::OrganDefinition def;
    def.name = "Minimal";
    def.windchests.push_back({ "Main", 812.0f, false });

    model::RankDef rank;
    rank.name      = "Principal";
    rank.windchest = "Main";
    def.ranks.push_back(rank);

    model::DivisionDef div;
    div.name = "Great";
    def.divisions.push_back(div);

    model::StopDef stop;
    stop.name     = "Principal 8";
    stop.division = "Great";
    stop.rank     = "Principal";
    def.stops.push_back(stop);

    const std::string written = model::OrganLoader::serialize(def);
    INFO(written);

    // The defaults are absent...
    CHECK(written.find("pressurePa") == std::string::npos); // 812 IS the default
    CHECK(written.find("voicing") == std::string::npos);
    CHECK(written.find("lowNote") == std::string::npos);
    CHECK(written.find("tremulant") == std::string::npos);
    CHECK(written.find("builder") == std::string::npos);
    CHECK(written.find("year") == std::string::npos);
    CHECK(written.find("couplers") == std::string::npos);

    // ...and what identifies the thing is present, defaulted or not.
    CHECK(written.find("\"name\"") != std::string::npos);
    CHECK(written.find("\"family\"") != std::string::npos);
    CHECK(written.find("\"footage\"") != std::string::npos);
    CHECK(written.find("\"division\"") != std::string::npos);
    CHECK(written.find("\"rank\"") != std::string::npos);

    // And a document that says little still reads back as the same organ.
    const model::ParseResult back = model::OrganLoader::parse(written);
    INFO(describe(back.diagnostics));
    REQUIRE(back.ok());
    CHECK(model::OrganLoader::serialize(*back.definition) == written);
}

TEST_CASE("A malformed document says what and where", "[model][organfile]")
{
    // A file a person edits by hand will be wrong. An error that names the field
    // and the line is most of what makes it fixable; "parse failed" is not.
    const auto reject = [](const std::string& doc)
    {
        const model::ParseResult r = model::OrganLoader::parse(doc, model::OrganFileFormat::Json,
                                                               "organ.json");
        INFO("expected rejection of:\n" << doc);
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.diagnostics.hasErrors());
        return describe(r.diagnostics);
    };

    // Not JSON at all.
    CHECK_FALSE(reject("{").empty());

    // A field of the wrong type, named in the diagnostic.
    const std::string wrongType = reject(j(
        "{'name': 'X', 'windchests': [{'name': 'Main', 'pressurePa': 'loud'}]}"));
    INFO(wrongType);
    CHECK(wrongType.find("windchests[0].pressurePa") != std::string::npos);
    CHECK(wrongType.find("number") != std::string::npos);

    // An enum token that is not one -- defaulting past this would give an organ
    // that is quietly not the one described.
    const std::string badFamily = reject(j(
        "{'name': 'X',\n"
        " 'windchests': [{'name': 'Main'}],\n"
        " 'ranks': [{'name': 'R', 'family': 'Trombone', 'windchest': 'Main'}]}"));
    INFO(badFamily);
    CHECK(badFamily.find("ranks[0].family") != std::string::npos);
    CHECK(badFamily.find("Trombone") != std::string::npos);
    CHECK(badFamily.find("line 3") != std::string::npos);

    // A whole number that is not whole.
    const std::string fractional = reject(j(
        "{'name': 'X', 'divisions': [{'name': 'G', 'lowNote': 36.5}]}"));
    CHECK(fractional.find("divisions[0].lowNote") != std::string::npos);

    // A footage that is neither a number nor a pair.
    const std::string badFootage = reject(j(
        "{'name': 'X',\n"
        " 'windchests': [{'name': 'Main'}],\n"
        " 'ranks': [{'name': 'R', 'footage': 'eight', 'windchest': 'Main'}]}"));
    CHECK(badFootage.find("ranks[0].footage") != std::string::npos);

    // An array where an object belongs.
    CHECK_FALSE(reject(j("{'name': 'X', 'ranks': [42]}")).empty());
    CHECK_FALSE(reject(j("{'name': 'X', 'ranks': {}}")).empty());
}

TEST_CASE("Structural mistakes are caught at parse, not only at compile",
          "[model][organfile]")
{
    // A dangling reference is a mistake in the document, so the document reader
    // reports it -- a caller that parses without compiling still learns.
    const model::ParseResult r = model::OrganLoader::parse(j(
        "{'name': 'X',\n"
        " 'windchests': [{'name': 'Main'}],\n"
        " 'ranks': [{'name': 'R', 'windchest': 'Nowhere'}],\n"
        " 'divisions': [{'name': 'G'}],\n"
        " 'stops': [{'name': 'S', 'division': 'G', 'rank': 'R'}]}"));

    INFO(describe(r.diagnostics));
    CHECK_FALSE(r.ok());
    CHECK(r.diagnostics.hasErrors());
}

TEST_CASE("A format with no reader says so rather than guessing",
          "[model][organfile]")
{
    // Auto-detection is deliberately crude: a document that does not open with a
    // brace is not JSON, and there is no YAML reader to fall back to.
    const model::ParseResult yaml =
        model::OrganLoader::parse("name: Test Organ\n", model::OrganFileFormat::Auto);
    CHECK_FALSE(yaml.ok());
    CHECK(yaml.diagnostics.hasErrors());

    const model::ParseResult asked =
        model::OrganLoader::parse(sampleDocument(), model::OrganFileFormat::Yaml);
    CHECK_FALSE(asked.ok());

    // And the writer says the same about writing one, rather than emitting JSON
    // under a YAML name.
    model::OrganDefinition def;
    def.name = "X";
    CHECK(model::OrganLoader::serialize(def, model::OrganFileFormat::Yaml).empty());
}

TEST_CASE("The format can express the organ this instrument actually ships",
          "[model][organfile][regression]")
{
    // The strongest thing that can be said about a file format: the one organ
    // that exists survives a round trip through it.
    //
    // The demo organ is built in C++ by DemoOrgan.cpp -- three divisions,
    // twenty-six stops, mixtures with their compositions, an enclosed Récit with
    // a tremulant, couplers. If the document schema could not carry all of that,
    // then it is a format for toy organs and nobody should be asked to write
    // against it. This is the test that says otherwise, and it is the reason
    // definitionFrom exists at all.
    const model::Organ           original = model::buildCaeciliaDemoOrgan();
    const model::OrganDefinition def      = model::OrganLoader::definitionFrom(original);

    const std::string written = model::OrganLoader::serialize(def);
    REQUIRE_FALSE(written.empty());

    const model::ParseResult parsed =
        model::OrganLoader::parse(written, model::OrganFileFormat::Json, "caecilia.organ.json");
    INFO(describe(parsed.diagnostics));
    REQUIRE(parsed.ok());

    const model::CompileResult rebuilt = model::OrganLoader::compile(*parsed.definition);
    INFO(describe(rebuilt.diagnostics));
    REQUIRE(rebuilt.ok());

    const model::Organ& copy = *rebuilt.organ;

    CHECK(copy.name()    == original.name());
    CHECK(copy.builder() == original.builder());
    CHECK(copy.year()    == original.year());
    REQUIRE(copy.windchests().size() == original.windchests().size());
    REQUIRE(copy.ranks().size()      == original.ranks().size());
    REQUIRE(copy.stops().size()      == original.stops().size());
    REQUIRE(copy.divisions().size()  == original.divisions().size());
    REQUIRE(copy.manuals().size()    == original.manuals().size());
    REQUIRE(copy.couplers().size()   == original.couplers().size());

    for (std::size_t i = 0; i < original.windchests().size(); ++i)
    {
        INFO("windchest " << i << " '" << original.windchests()[i].name << "'");
        CHECK(copy.windchests()[i].name == original.windchests()[i].name);
        CHECK(copy.windchests()[i].nominalPressurePa
              == Approx(original.windchests()[i].nominalPressurePa));
        CHECK(copy.windchests()[i].hasTremulant == original.windchests()[i].hasTremulant);
    }

    for (std::size_t i = 0; i < original.ranks().size(); ++i)
    {
        const model::Rank& a = original.ranks()[i];
        const model::Rank& b = copy.ranks()[i];
        INFO("rank " << i << " '" << a.name() << "'");
        CHECK(b.name()      == a.name());
        CHECK(b.family()    == a.family());
        CHECK(b.engine()    == a.engine());
        CHECK(b.footage()   == a.footage());
        CHECK(b.windchest().value == a.windchest().value);
        CHECK(b.lowNote()   == a.lowNote());
        CHECK(b.highNote()  == a.highNote());
        CHECK(b.pipeCount() == a.pipeCount()); // the pipes the compass generates
        CHECK(b.voicing().brightness == Approx(a.voicing().brightness));
        CHECK(b.voicing().chiffAmount == Approx(a.voicing().chiffAmount));
    }

    for (std::size_t i = 0; i < original.stops().size(); ++i)
    {
        const model::Stop& a = original.stops()[i];
        const model::Stop& b = copy.stops()[i];
        INFO("stop " << i << " '" << a.name() << "'");
        CHECK(b.name()       == a.name());
        CHECK(b.family()     == a.family());
        CHECK(b.footage()    == a.footage());
        CHECK(b.pitchClass() == a.pitchClass());
        CHECK(b.role()       == a.role());
        CHECK(b.division().value == a.division().value);
        CHECK(b.rank().value     == a.rank().value);
        // The mixture compositions, which are what make a Fourniture a Fourniture.
        REQUIRE(b.mixtureComposition().size() == a.mixtureComposition().size());
        for (std::size_t m = 0; m < a.mixtureComposition().size(); ++m)
            CHECK(b.mixtureComposition()[m] == a.mixtureComposition()[m]);
    }

    for (std::size_t i = 0; i < original.divisions().size(); ++i)
    {
        const model::Division& a = original.divisions()[i];
        const model::Division& b = copy.divisions()[i];
        INFO("division " << i << " '" << a.name() << "'");
        CHECK(b.name()        == a.name());
        CHECK(b.kind()        == a.kind());
        CHECK(b.isEnclosed()  == a.isEnclosed());
        CHECK(b.hasTremulant()== a.hasTremulant());
        CHECK(b.lowNote()     == a.lowNote());
        CHECK(b.highNote()    == a.highNote());
        CHECK(b.stopCount()   == a.stopCount());
    }

    for (std::size_t i = 0; i < original.manuals().size(); ++i)
    {
        INFO("manual " << i);
        CHECK(copy.manuals()[i].division.value == original.manuals()[i].division.value);
        CHECK(copy.manuals()[i].manualIndex    == original.manuals()[i].manualIndex);
        CHECK(copy.manuals()[i].midiChannel    == original.manuals()[i].midiChannel);
    }

    for (std::size_t i = 0; i < original.couplers().size(); ++i)
    {
        const model::Coupler& a = original.couplers()[i];
        const model::Coupler& b = copy.couplers()[i];
        INFO("coupler " << i << " '" << a.name() << "'");
        CHECK(b.name() == a.name());
        CHECK(b.from().value == a.from().value);
        CHECK(b.to().value   == a.to().value);
        CHECK(b.octaveShiftSemitones() == a.octaveShiftSemitones());
        CHECK(b.kind() == a.kind());
    }

    // And the document is stable: exporting the rebuilt organ gives the same text.
    CHECK(model::OrganLoader::serialize(model::OrganLoader::definitionFrom(copy)) == written);
}

TEST_CASE("An organ from a document opens with stops drawn and pistons set",
          "[model][organfile][regression]")
{
    // What a user's own organ has to survive. defaultOpeningRegistration and
    // resolveFactoryGenerals were written against the demo organ, and the plugin
    // calls both the moment a document is loaded: if either returns nothing, the
    // instrument opens SILENT and with an empty combination memory, which reads as
    // a broken plugin rather than as an unlucky organ file.
    const model::CompileResult built = model::OrganLoader::load(sampleDocument());
    INFO(describe(built.diagnostics));
    REQUIRE(built.ok());
    const model::Organ& organ = *built.organ;

    // A manual to play on, and not the pedal division if there is a choice.
    const core::DivisionId primary = model::primaryManual(organ);
    CHECK(organ.division(primary) != nullptr);
    CHECK(organ.division(primary)->kind() != model::DivisionKind::Pedal);

    // Something is drawn, and every id it names is a stop this organ has.
    const std::vector<core::StopId> opening =
        model::defaultOpeningRegistration(organ, primary);
    INFO("opening registration draws " << opening.size() << " stops");
    CHECK_FALSE(opening.empty());
    for (const core::StopId id : opening)
    {
        INFO("stop id " << id.value);
        CHECK(organ.stop(id) != nullptr);
    }

    // And the factory pistons resolve against THIS organ rather than against the
    // one they were written for. An empty row is a legitimate answer for a small
    // instrument, but every bit that IS set has to name a stop that exists.
    std::array<std::uint64_t, 8> generals{};
    const std::size_t written = registration::resolveFactoryGenerals(organ, generals);
    INFO(written << " factory pistons resolved");
    for (std::size_t i = 0; i < written && i < generals.size(); ++i)
        for (int b = 0; b < 64; ++b)
            if ((generals[i] & (std::uint64_t{ 1 } << b)) != 0)
            {
                INFO("piston " << i << " draws stop id " << b);
                CHECK(organ.stop(core::StopId{ static_cast<std::uint16_t>(b) }) != nullptr);
            }
}

TEST_CASE("An empty document is an empty organ, not an error", "[model][organfile]")
{
    // Every section is optional. A document with nothing but a name is a valid,
    // silent instrument -- which is what someone starting a new organ file writes
    // first, and they should not have to fight the reader to get there.
    const model::ParseResult r = model::OrganLoader::parse(j("{'name': 'Nothing Yet'}"));
    INFO(describe(r.diagnostics));
    REQUIRE(r.ok());
    CHECK(r.definition->name == "Nothing Yet");
    CHECK(r.definition->ranks.empty());
    CHECK(r.definition->stops.empty());
}
