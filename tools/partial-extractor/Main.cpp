/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// ceciliae-partial-extractor
//
// Offline CLI: FFT a steady pipe sample into an additive PartialBank (the
// off-line half of the proprietary SpectralModel). Host-side tool; console and
// file I/O and heap use are all fine here.

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/Version.h"

#include "common/CliArgs.h"
#include "common/WavFile.h"
#include "partial-extractor/PartialExtractor.h"

#include <fstream>
#include <iostream>
#include <string>

namespace
{
using namespace ceciliae;

void printUsage(std::string_view program)
{
    std::cout
        << "ceciliae-partial-extractor (ceciliae_core " << core::versionString() << ")\n"
        << "FFT a pipe sample into an additive partial bank.\n\n"
        << "Usage:\n  " << program << " --input <file.wav> [options]\n\n"
        << "Options:\n"
        << "  --input <path>       Source recording (WAV). Required.\n"
        << "  --output <path>      Write the partial bank as JSON here (else stdout).\n"
        << "  --fft-size <n>       STFT size, power of two (default 4096).\n"
        << "  --hop-size <n>       STFT hop in frames (default 1024).\n"
        << "  --max-partials <n>   Maximum partials to track (default 64).\n"
        << "  --f0 <hz>            Expected fundamental in Hz (0 = auto-detect).\n"
        << "  --floor-db <db>      Ignore spectral peaks below this level (default -90).\n"
        << "  --footage-num <n>    Sounding footage numerator (default 8).\n"
        << "  --footage-den <n>    Sounding footage denominator (default 1).\n"
        << "  --help               Show this help.\n";
}

void writeJson(std::ostream& os, const tools::PartialBank& bank)
{
    os << "{\n"
       << "  \"fundamentalHz\": " << bank.fundamentalHz << ",\n"
       << "  \"footage\": { \"num\": " << bank.footage.num
       << ", \"den\": " << bank.footage.den
       << ", \"feet\": " << bank.footage.feet() << " },\n"
       << "  \"partialCount\": " << bank.partials.size() << ",\n"
       << "  \"partials\": [";

    for (std::size_t i = 0; i < bank.partials.size(); ++i)
    {
        const tools::ExtractedPartial& p = bank.partials[i];
        os << (i == 0 ? "\n" : ",\n")
           << "    { \"ratioToF0\": " << p.ratioToF0
           << ", \"ampDb\": " << p.ampDb
           << ", \"phase\": " << p.phase
           << ", \"windSensitivity\": " << p.windSensitivity << " }";
    }

    os << (bank.partials.empty() ? " ]\n" : "\n  ]\n") << "}\n";
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

    tools::PartialExtractorOptions opts;
    opts.fftSize     = static_cast<std::size_t>(args.integer("fft-size").value_or(4096));
    opts.hopSize     = static_cast<std::size_t>(args.integer("hop-size").value_or(1024));
    opts.maxPartials = static_cast<std::size_t>(args.integer("max-partials").value_or(64));
    opts.f0Hint      = args.number("f0").value_or(0.0);
    opts.floorDb     = args.number("floor-db").value_or(-90.0);
    opts.footage     = core::Footage{
        static_cast<std::int32_t>(args.integer("footage-num").value_or(8)),
        static_cast<std::int32_t>(args.integer("footage-den").value_or(1))
    };

    const tools::PartialExtractor extractor(opts);
    const tools::PartialBank      bank = extractor.extract(wav);

    const std::string outPath = args.value("output");
    if (outPath.empty())
    {
        writeJson(std::cout, bank);
    }
    else
    {
        std::ofstream out(outPath, std::ios::trunc);
        if (!out)
        {
            std::cerr << "error: cannot open '" << outPath << "' for writing\n";
            return 4;
        }
        writeJson(out, bank);
        std::cerr << "wrote " << outPath << "\n";
    }

    if (bank.partials.empty())
        std::cerr << "note: partial tracking is not yet implemented (phase 05); "
                     "emitted an empty bank.\n";

    return 0;
}
