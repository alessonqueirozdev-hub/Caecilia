// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

// ---------------------------------------------------------------------------
// caecilia-organ-file
//
// Export the instrument's own organ as a document, or check a document someone
// has written.
//
// The point of the export is not the file, it is the starting point. Caecilia's
// organ file is a format with no examples in the world, and the fastest way to
// learn one is to read a real instrument written in it and change things. So this
// prints the organ that actually ships -- three divisions, twenty-six stops,
// mixtures with their compositions, an enclosed Récit with a tremulant, couplers
// -- in the format a user is being asked to write.
//
// The check reads a document back and reports what it finds, with the line and
// the field for anything wrong. Every diagnostic goes to stderr and the document
// to stdout, so `caecilia-organ-file --export > my.organ.json` is a file and not
// a file with a header in it.
//
// Usage:
//   caecilia-organ-file --export                 write the demo organ to stdout
//   caecilia-organ-file --check <path>           read a document and report
//   caecilia-organ-file --roundtrip              export, read back, compare
//
// Exit code 0 == success, 1 == the document was rejected or the round trip
// disagreed. Nothing here runs on an audio thread.
// ---------------------------------------------------------------------------

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/OrganDefinition.h"
#include "caecilia/model/OrganLoader.h"

#include "common/CliArgs.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace model = caecilia::model;

namespace
{
/// Print every diagnostic to stderr, so stdout stays a clean document.
void report(const model::LoadDiagnostics& d, const char* what)
{
    for (const model::Diagnostic& x : d.entries())
    {
        const char* level = x.severity == model::DiagnosticSeverity::Error   ? "error"
                          : x.severity == model::DiagnosticSeverity::Warning ? "warning"
                                                                             : "note";
        std::fprintf(stderr, "%s: %s: %s %s\n", what, level,
                     x.context.c_str(), x.message.c_str());
    }
}

/// A short census, so `--check` says something useful about a file that is fine.
void census(const model::Organ& organ)
{
    std::fprintf(stderr, "  %s", organ.name().c_str());
    if (!organ.builder().empty())
        std::fprintf(stderr, ", %s", organ.builder().c_str());
    if (organ.year() != 0)
        std::fprintf(stderr, ", %d", static_cast<int>(organ.year()));
    std::fprintf(stderr, "\n  %zu division%s, %zu keyboard%s, %zu rank%s, "
                         "%zu stop%s, %zu coupler%s, %zu windchest%s\n",
                 organ.divisions().size(), organ.divisions().size() == 1 ? "" : "s",
                 organ.manuals().size(),   organ.manuals().size()   == 1 ? "" : "s",
                 organ.ranks().size(),     organ.ranks().size()     == 1 ? "" : "s",
                 organ.stops().size(),     organ.stops().size()     == 1 ? "" : "s",
                 organ.couplers().size(),  organ.couplers().size()  == 1 ? "" : "s",
                 organ.windchests().size(),organ.windchests().size()== 1 ? "" : "s");
}

[[nodiscard]] std::string readFile(const std::string& path, bool& ok)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        ok = false;
        return {};
    }
    ok = true;
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}
} // namespace

int main(int argc, char** argv)
{
    const caecilia::tools::CliArgs args(argc, argv);

    const bool doExport    = args.has("export");
    const bool doRoundTrip = args.has("roundtrip");
    const std::string path = args.value("check");

    if (!doExport && !doRoundTrip && path.empty())
    {
        std::fprintf(stderr,
            "caecilia-organ-file\n"
            "  --export             write the demo organ as a document to stdout\n"
            "  --check <path>       read a document and report what it says\n"
            "  --roundtrip          export, read back and compare\n");
        return 1;
    }

    if (doExport || doRoundTrip)
    {
        const model::Organ           organ = model::buildCaeciliaDemoOrgan();
        const model::OrganDefinition def   = model::OrganLoader::definitionFrom(organ);
        const std::string            text  = model::OrganLoader::serialize(def);

        if (doExport)
            std::fwrite(text.data(), 1, text.size(), stdout);

        if (!doRoundTrip)
            return 0;

        // Read it back and require the SAME BYTES on the way out again. Anything
        // the format cannot carry shows up here as a difference rather than as a
        // quietly different organ.
        const model::ParseResult back =
            model::OrganLoader::parse(text, model::OrganFileFormat::Json, "<export>");
        report(back.diagnostics, "roundtrip");
        if (!back.ok())
        {
            std::fprintf(stderr, "roundtrip: the exported document did not read back\n");
            return 1;
        }

        const model::CompileResult rebuilt = model::OrganLoader::compile(*back.definition);
        report(rebuilt.diagnostics, "roundtrip");
        if (!rebuilt.ok())
        {
            std::fprintf(stderr, "roundtrip: the document did not compile\n");
            return 1;
        }

        const std::string again =
            model::OrganLoader::serialize(model::OrganLoader::definitionFrom(*rebuilt.organ));
        if (again != text)
        {
            std::fprintf(stderr, "roundtrip: the document changed on the way through\n");
            return 1;
        }

        std::fprintf(stderr, "roundtrip: identical (%zu bytes)\n", text.size());
        census(*rebuilt.organ);
        return 0;
    }

    bool              readable = false;
    const std::string text     = readFile(path, readable);
    if (!readable)
    {
        std::fprintf(stderr, "check: cannot read '%s'\n", path.c_str());
        return 1;
    }

    const model::CompileResult loaded = model::OrganLoader::load(text, model::OrganFileFormat::Auto,
                                                                 path);
    report(loaded.diagnostics, "check");
    if (!loaded.ok())
    {
        std::fprintf(stderr, "check: '%s' is not a usable organ\n", path.c_str());
        return 1;
    }

    std::fprintf(stderr, "check: '%s' is a usable organ\n", path.c_str());
    census(*loaded.organ);
    return 0;
}
