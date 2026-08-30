// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief The example organ that ships with the project still opens.
 *
 * examples/caecilia-demo.organ.json is what a user copies to start from, and the
 * only concrete answer to "where do I get an organ file". An example that has
 * quietly stopped parsing is worse than no example: it teaches the format wrong
 * and it fails in the user's hands rather than here.
 *
 * The strong check is the last one. The file is not merely valid -- it is exactly
 * what the exporter produces today, byte for byte, so a change to the format that
 * nobody regenerated it for fails the build instead of shipping.
 */

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/OrganLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace caecilia;

namespace
{

std::string exampleText()
{
    const std::string path = std::string(CAECILIA_EXAMPLES_DIR) + "/caecilia-demo.organ.json";
    std::ifstream     in(path, std::ios::binary);
    REQUIRE(in.good());

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("The shipped example organ loads without a single diagnostic", "[model][example]")
{
    const model::CompileResult result =
        model::OrganLoader::load(exampleText(), model::OrganFileFormat::Auto,
                                 "examples/caecilia-demo.organ.json");

    // Not merely "no errors": no WARNINGS either. This is the document a user
    // copies, so anything the loader would grumble about is something they would
    // inherit and reproduce.
    for (const model::Diagnostic& d : result.diagnostics.entries())
        INFO(d.context << ": " << d.message);
    CHECK(result.diagnostics.entries().empty());
    REQUIRE(result.ok());
}

TEST_CASE("The example is the instrument's own organ", "[model][example]")
{
    const model::CompileResult result =
        model::OrganLoader::load(exampleText(), model::OrganFileFormat::Auto, "example");
    REQUIRE(result.ok());

    const model::Organ& built = model::buildCaeciliaDemoOrgan();
    const model::Organ& read  = *result.organ;

    // The counts the documentation quotes. A user reading ORGAN_FILE.md is told
    // this file is a real three-manual specification of a given size, and that
    // claim should not be able to drift.
    CHECK(read.name()             == built.name());
    CHECK(read.stops().size()     == built.stops().size());
    CHECK(read.ranks().size()     == built.ranks().size());
    CHECK(read.divisions().size() == built.divisions().size());
    CHECK(read.couplers().size()  == built.couplers().size());
    CHECK(read.windchests().size()== built.windchests().size());
}

TEST_CASE("The example is exactly what the exporter writes", "[model][example]")
{
    // The guard that keeps it from rotting. Add a key to the format, change a
    // default, rename a field -- and this fails until the example is regenerated:
    //
    //     caecilia-organ-file --export > examples/caecilia-demo.organ.json
    //
    // Which is the whole point: the example must always be a document the current
    // loader produces, not one an older one did.
    const std::string exported = model::OrganLoader::serialize(
        model::OrganLoader::definitionFrom(model::buildCaeciliaDemoOrgan()));

    CHECK(exported == exampleText());
}
