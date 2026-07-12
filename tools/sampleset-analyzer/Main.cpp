/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// ceciliae-sampleset-analyzer
//
// Offline CLI: extract loop points and pitch from one recorded pipe sample.
// Links ceciliae_core for the shared vocabulary (PipeId, Footage, ...). It is a
// host-side tool, so console/file I/O and heap use are all fine here; none of
// this runs on the audio thread.

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/Version.h"

#include "common/CliArgs.h"
#include "common/WavFile.h"
#include "sampleset-analyzer/SampleSetAnalyzer.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
using namespace ceciliae;

void printUsage(std::string_view program)
{
    std::cout
        << "ceciliae-sampleset-analyzer (ceciliae_core " << core::versionString() << ")\n"
        << "Extract loop points and pitch from a recorded pipe sample.\n\n"
        << "Usage:\n  " << program << " --input <file.wav> [options]\n\n"
        << "Options:\n"
        << "  --input <path>       Source recording (WAV: PCM16/24/32 or float32). Required.\n"
        << "  --output <path>      Write the analysis as JSON to this file (else stdout).\n"
        << "  --rank <n>           Rank id to stamp on the PipeId (default 0).\n"
        << "  --note <0-127>       MIDI note to stamp on the PipeId (default 60).\n"
        << "  --f0 <hz>            Expected fundamental in Hz (seeds the estimator).\n"
        << "  --min-loop-ms <ms>   Shortest acceptable loop length (default 150).\n"
        << "  --crossfade-ms <ms>  Suggested loop crossfade length (default 40).\n"
        << "  --help               Show this help.\n";
}

// Tiny hand-rolled JSON writer: the tools deliberately carry no JSON dependency.
void writeJson(std::ostream& os, const tools::SampleAnalysis& a)
{
    os << "{\n"
       << "  \"pipe\": { \"rank\": " << a.pipe.rankId
       << ", \"note\": " << static_cast<int>(a.pipe.midiNote) << " },\n"
       << "  \"sampleRate\": " << a.sampleRate << ",\n"
       << "  \"numFrames\": " << a.numFrames << ",\n"
       << "  \"numChannels\": " << a.numChannels << ",\n"
       << "  \"peakDbfs\": " << a.peakDbfs << ",\n"
       << "  \"pitch\": { \"frequencyHz\": " << a.pitch.frequencyHz
       << ", \"confidence\": " << a.pitch.confidence
       << ", \"nearestNote\": " << static_cast<int>(a.pitch.nearestNote)
       << ", \"centsFromEqualTemper\": " << a.pitch.centsFromEqualTemper << " },\n"
       << "  \"sustain\": { \"startFrame\": " << a.sustain.startFrame
       << ", \"endFrame\": " << a.sustain.endFrame << " },\n"
       << "  \"loop\": { \"startFrame\": " << a.loop.startFrame
       << ", \"endFrame\": " << a.loop.endFrame
       << ", \"crossfadeMs\": " << a.loop.crossfadeMs
       << ", \"seamError\": " << a.loop.seamError
       << ", \"valid\": " << (a.loop.valid ? "true" : "false") << " },\n"
       << "  \"attackEndSec\": " << a.attackEndSec << "\n"
       << "}\n";
}
} // namespace

int main(int argc, char** argv)
{
    using namespace ceciliae;
    const tools::CliArgs args(argc, argv);

    if (args.has("help") || argc == 1)
    {
        printUsage(args.program());
        return args.has("help") ? 0 : 1;
    }

    const std::string input = args.value("input");
    if (input.empty())
    {
        std::cerr << "error: --input is required\n";
        return 2;
    }

    tools::WavData wav;
    std::string    error;
    if (!tools::WavFile::read(input, wav, &error))
    {
        std::cerr << "error: failed to read '" << input << "': " << error << "\n";
        return 3;
    }

    tools::SampleSetAnalyzerOptions opts;
    opts.pipe.rankId  = static_cast<std::uint16_t>(args.integer("rank").value_or(0));
    opts.pipe.midiNote = static_cast<std::uint8_t>(args.integer("note").value_or(60));
    opts.f0Hint       = args.number("f0").value_or(0.0);
    opts.minLoopMs    = args.number("min-loop-ms").value_or(150.0);
    opts.crossfadeMs  = args.number("crossfade-ms").value_or(40.0);

    const tools::SampleSetAnalyzer analyzer(opts);
    const tools::SampleAnalysis    analysis = analyzer.analyze(wav);

    const std::string outPath = args.value("output");
    if (outPath.empty())
    {
        writeJson(std::cout, analysis);
    }
    else
    {
        std::ofstream out(outPath, std::ios::trunc);
        if (!out)
        {
            std::cerr << "error: cannot open '" << outPath << "' for writing\n";
            return 4;
        }
        writeJson(out, analysis);
        std::cerr << "wrote " << outPath << "\n";
    }

    if (!analysis.loop.valid)
        std::cerr << "note: loop detection is not yet implemented (phase 04); "
                     "no loop was committed.\n";

    return 0;
}
