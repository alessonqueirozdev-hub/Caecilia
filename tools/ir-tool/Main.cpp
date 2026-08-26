// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

// caecilia-ir-tool
//
// Offline CLI: inspect and condition impulse responses for the convolution
// reverb. Host-side tool; console/file I/O and heap use are all fine here.
//
//   caecilia-ir-tool info      --input hall.wav
//   caecilia-ir-tool normalise --input hall.wav --output hall_norm.wav --peak-db -1
//   caecilia-ir-tool trim      --input hall.wav --output hall_trim.wav

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/Version.h"

#include "common/CliArgs.h"
#include "common/WavFile.h"
#include "ir-tool/ImpulseResponseTool.h"

#include <iostream>
#include <string>

namespace
{
using namespace caecilia;

void printUsage(std::string_view program)
{
    std::cout
        << "caecilia-ir-tool (caecilia_core " << core::versionString() << ")\n"
        << "Inspect and condition impulse responses for the convolution reverb.\n\n"
        << "Usage:\n  " << program << " <command> --input <file.wav> [options]\n\n"
        << "Commands:\n"
        << "  info        Print an IR summary (length, peak, RT60 estimate).\n"
        << "  trim        Trim pre-delay and inaudible tail (phase 03).\n"
        << "  normalise   Scale the IR so its peak hits --peak-db.\n"
        << "  resample    Convert to --rate Hz (phase 03).\n"
        << "  deconvolve  Recover an IR from a sine sweep (phase 95).\n\n"
        << "Options:\n"
        << "  --input <path>       Source IR (WAV). Required.\n"
        << "  --output <path>      Destination for edited IR (editing commands).\n"
        << "  --peak-db <db>       Normalise target peak in dBFS (default -1).\n"
        << "  --trim-db <db>       Onset/tail threshold in dBFS (default -60).\n"
        << "  --rate <hz>          Target sample rate for resample.\n"
        << "  --help               Show this help.\n";
}

tools::IrOperation parseOperation(const std::string& command)
{
    if (command == "trim")       return tools::IrOperation::Trim;
    if (command == "normalise" || command == "normalize")
                                 return tools::IrOperation::Normalise;
    if (command == "resample")   return tools::IrOperation::Resample;
    if (command == "deconvolve") return tools::IrOperation::Deconvolve;
    return tools::IrOperation::Info; // default / "info"
}

void printInfo(const tools::ImpulseResponseInfo& info)
{
    std::cout << "impulse response:\n"
              << "  sampleRate  : " << info.sampleRate << " Hz\n"
              << "  channels    : " << info.numChannels << "\n"
              << "  frames      : " << info.numFrames << "\n"
              << "  length      : " << info.lengthSec << " s\n"
              << "  peak        : " << info.peakDbfs << " dBFS\n"
              << "  rt60 (est.) : " << info.rt60Sec << " s\n"
              << "  onset frame : " << info.onsetFrame << "\n";
}
} // namespace

int main(int argc, char** argv)
{
    using namespace caecilia;
    const tools::CliArgs args(argc, argv);

    if (args.has("help") || argc == 1)
    {
        printUsage(args.program());
        return args.has("help") ? 0 : 1;
    }

    // The command is the first positional (default "info" when omitted).
    const std::string command =
        args.positionals().empty() ? std::string("info") : args.positionals().front();

    const std::string input = args.value("input");
    if (input.empty())
    {
        std::cerr << "error: --input is required\n";
        return 2;
    }

    tools::WavData ir;
    std::string    error;
    if (!tools::WavFile::read(input, ir, &error))
    {
        std::cerr << "error: failed to read '" << input << "': " << error << "\n";
        return 3;
    }

    tools::IrToolOptions opts;
    opts.op               = parseOperation(command);
    opts.targetPeakDb     = args.number("peak-db").value_or(-1.0);
    opts.trimThresholdDb  = args.number("trim-db").value_or(-60.0);
    opts.targetSampleRate = static_cast<core::SampleRate>(args.number("rate").value_or(0.0));

    const tools::ImpulseResponseTool tool(opts);

    // Always report what we are working with.
    printInfo(tool.describe(ir));

    if (opts.op == tools::IrOperation::Info)
        return 0;

    if (!tool.processInPlace(ir, &error))
    {
        std::cerr << "error: " << error << "\n";
        return 5;
    }

    const std::string outPath = args.value("output");
    if (outPath.empty())
    {
        std::cerr << "error: editing commands require --output\n";
        return 6;
    }
    if (!tools::WavFile::write(outPath, ir, &error))
    {
        std::cerr << "error: failed to write '" << outPath << "': " << error << "\n";
        return 7;
    }

    std::cerr << "wrote " << outPath << "\n";
    return 0;
}
