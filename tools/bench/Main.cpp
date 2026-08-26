// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// caecilia-bench — how much of a CPU core does the engine actually need?
//
// The number that matters for a virtual organ is not "milliseconds per block"
// but the REAL-TIME FACTOR: the fraction of a core consumed to render one
// second of audio. Above 1.0 the host xruns; a usable instrument wants a wide
// margin, because the host has a mixer, other plugins and a scheduler to feed
// as well.
//
// The worst case for this engine is a full Tutti held as a dense chord: every
// drawn rank contributes partials to EVERY sounding voice, so cost is roughly
// (partials per voice) x (voices) x (sample rate).
//
// Usage:
//   caecilia-bench [--sr 48000] [--block 512] [--seconds 2] [--repeats 5]
//
// Measurement discipline, because the first version of this tool had none and
// its numbers were not reproducible:
//
//   * FTZ/DAZ is ON. The plugin renders every sample inside ScopedNoDenormals,
//     so measuring without it measures a machine the user never hears. A release
//     tail decays through the denormal range and on x86 every denormal operation
//     traps to microcode -- enough to make one sustained voice look like 144% of
//     a core.
//   * A warm-up pass runs first and is discarded, so the figure is not paying for
//     cold caches, lazy page faults and the first-touch of the partial arrays.
//   * Each scenario is timed N times and the MINIMUM is reported. A minimum is
//     the right statistic here: every source of noise on a desktop only ever adds
//     time, so the fastest run is the one least contaminated. Reporting a mean
//     measures the operating system.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/Kernels.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"

#include "caecilia/core/IWindSupply.h"

#include "common/CliArgs.h"
#include "common/DenormalGuard.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
/// A reservoir pinned at one deviation, so `--wind` can measure the instrument
/// under load rather than at rest.
///
/// It is the loaded case that costs: the wind's brightness axis is a spectral
/// tilt, and applying it is an exp2 per partial per block that a reservoir sitting
/// at its nominal pressure does not pay for. "How much does the organ cost while
/// the wind is giving way" is therefore a different question from the one the rest
/// of this tool answers, and it is the one that decides whether the CPU governor
/// has anything to do.
class PinnedWind final : public core::IWindSupply
{
public:
    explicit PinnedWind(float deviation) noexcept : dev_(deviation) {}

    [[nodiscard]] float nominalPressurePa(core::WindchestId) const noexcept override
    {
        return 800.0f;
    }
    [[nodiscard]] float pressureAt(core::WindchestId, std::size_t) const noexcept override
    {
        return 800.0f * (1.0f + dev_);
    }
    [[nodiscard]] float pressureDeviation(core::WindchestId, std::size_t) const noexcept override
    {
        return dev_;
    }
    [[nodiscard]] core::WindchestId chestForPipe(core::PipeId) const noexcept override
    {
        return core::WindchestId{};
    }
    void registerDemand(core::WindchestId, float) noexcept override {}
    void step(std::size_t) noexcept override {}
    void setChestTremulantEnabled(core::WindchestId, bool) noexcept override {}
    void setChestTremulantShape(core::WindchestId, float, float) noexcept override {}

private:
    float dev_ = 0.0f;
};

model::RegistrationRank rank(core::TonalFamily fam, double feet, bool compound = false)
{
    return model::RegistrationRank{ fam, model::footageFromFeet(feet), compound };
}

/// The demo instrument's registrations, softest to loudest.
struct Scenario
{
    const char*                             name;
    std::vector<model::RegistrationRank>    ranks;
    int                                     voices;
};

std::vector<Scenario> scenarios()
{
    const std::vector<model::RegistrationRank> single = {
        rank(core::TonalFamily::Flute, 8),
    };
    const std::vector<model::RegistrationRank> plenum = {
        rank(core::TonalFamily::Principal, 16), rank(core::TonalFamily::Principal, 8),
        rank(core::TonalFamily::Flute, 8),      rank(core::TonalFamily::Principal, 4),
        rank(core::TonalFamily::Principal, 2),  rank(core::TonalFamily::Mixture, 2, true),
    };
    std::vector<model::RegistrationRank> tutti = plenum;
    for (const auto& extra : {
             rank(core::TonalFamily::Flute, 16),     rank(core::TonalFamily::Flute, 4),
             rank(core::TonalFamily::Flute, 8.0 / 3.0), rank(core::TonalFamily::Flute, 8.0 / 5.0),
             rank(core::TonalFamily::String, 8),     rank(core::TonalFamily::String, 8),
             rank(core::TonalFamily::Reed, 16),      rank(core::TonalFamily::Reed, 8),
             rank(core::TonalFamily::Reed, 4),       rank(core::TonalFamily::Reed, 8),
             rank(core::TonalFamily::Mixture, 2, true),
             rank(core::TonalFamily::Principal, 8),  rank(core::TonalFamily::Flute, 8),
             rank(core::TonalFamily::Principal, 16), rank(core::TonalFamily::Reed, 8),
             rank(core::TonalFamily::Flute, 4),      rank(core::TonalFamily::Reed, 4),
             rank(core::TonalFamily::Principal, 4),  rank(core::TonalFamily::Flute, 8),
             rank(core::TonalFamily::String, 8),
         })
        tutti.push_back(extra);

    return {
        { "single stop, 1 note",     single, 1  },
        { "plenum, 4-note chord",    plenum, 4  },
        { "tutti, 1 note",           tutti,  1  },
        { "tutti, 4-note chord",     tutti,  4  },
        { "tutti, 10-note chord",    tutti,  10 },
        { "tutti, 32 voices",        tutti,  32 },
    };
}
} // namespace

int main(int argc, char** argv)
{
    const caecilia::tools::CliArgs args(argc, argv);
    const caecilia::tools::DenormalGuard denormalGuard; // must outlive every render

    const double sampleRate = args.number("sr").value_or(48000.0);
    const auto   blockSize  = static_cast<std::size_t>(args.integer("block").value_or(512));
    const double seconds    = args.number("seconds").value_or(2.0);
    const int    repeats    = static_cast<int>(args.integer("repeats").value_or(5));

    // One voice per (rank, note) -- what the plugin runs since ARCH-001 -- rather
    // than one composite voice per note. The partial count is the same and the
    // memory access pattern is not: 260 small banks instead of 10 large ones.
    const bool   perRank    = args.has("perrank");

    // Alternate the scalar reference and the vector backend inside one timing loop
    // and report the ratio. Two separate processes on a busy machine measured the
    // machine.
    const bool   ab         = args.has("ab");

    // Bind a reservoir at this normalised deviation (0 = nominal, -0.10 = ten
    // percent down, which is about what the demo organ's own chord test measures
    // under a heavy registration).
    const auto   windDev    = static_cast<float>(args.number("wind").value_or(0.0));

    std::printf("caecilia-bench  %.0f Hz, block %zu, %.1f s x %d runs (min reported)%s, %s\n\n",
                sampleRate, blockSize, seconds, repeats,
                caecilia::tools::DenormalGuard::available() ? ", FTZ/DAZ on"
                                                            : ", NO FTZ on this arch",
                perRank ? "one voice per (rank, note)" : "one composite voice per note");
    if (windDev != 0.0f)
        std::printf("wind pinned %.1f%% off nominal\n", 100.0 * windDev);
    if (ab)
    {
        std::printf("%-24s %8s %8s %10s %10s %10s %10s\n",
                    "scenario", "partials", "voices", "scalar", "sse2", "avx2", "speed-up");
        std::printf("%-24s %8s %8s %10s %10s %10s %10s\n",
                    "------------------------", "--------", "------", "---------",
                    "---------", "---------", "---------");
    }
    else
    {
        std::printf("%-24s %8s %8s %10s %8s %10s\n",
                    "scenario", "partials", "voices", "RT factor", "spread", "cores free");
        std::printf("%-24s %8s %8s %10s %8s %10s\n",
                    "------------------------", "--------", "------", "---------",
                    "--------", "----------");
    }

    const auto totalFrames = static_cast<std::size_t>(sampleRate * seconds);

    for (const Scenario& s : scenarios())
    {
        const synth::SpectralModel composite =
            model::buildCompositeFromRegistration(
                std::span<const model::RegistrationRank>(s.ranks));
        const std::size_t partials = composite.partials.size();

        const PinnedWind wind(windDev);

        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;
        if (windDev != 0.0f)
            ctx.wind = &wind;

        std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
        std::vector<synth::RankVoicing>                    voicings;

        if (perRank)
        {
            // The registration's ranks, each voiced on its own, exactly as
            // buildRankVoicing does for the plugin. The composite above is still
            // built -- it is what the `partials` column reports, and the two carry
            // the same partials by construction.
            for (const model::RegistrationRank& r : s.ranks)
            {
                synth::RankVoicing rv;
                rv.family  = r.family;
                rv.footage = r.footage;
                rv.spectrum =
                    model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(&r, 1));
                voicings.push_back(std::move(rv));
            }

            voices.reserve(static_cast<std::size_t>(s.voices) * voicings.size());
            for (int v = 0; v < s.voices; ++v)
            {
                const auto note = static_cast<core::MidiNote>(43 + v * 4);
                for (std::size_t k = 0; k < voicings.size(); ++k)
                {
                    auto voice = std::make_unique<synth::AdditiveVoice>();
                    voice->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
                    voice->prepare(sampleRate, blockSize);
                    voice->setContext(ctx);
                    voice->adoptRank(&voicings[k]);
                    voice->noteOn(core::PipeId{ static_cast<std::uint16_t>(k), note }, 100);
                    voices.push_back(std::move(voice));
                }
            }
        }
        else
        {
            voices.reserve(static_cast<std::size_t>(s.voices));
            for (int v = 0; v < s.voices; ++v)
            {
                auto voice = std::make_unique<synth::AdditiveVoice>();
                voice->bank().setMaxPartials(partials < 16 ? 16 : partials);
                voice->prepare(sampleRate, blockSize);
                voice->setContext(ctx);
                voice->seedFrom(composite);
                // Spread the chord over the compass so the anti-aliasing gate sees a
                // realistic mix of low and high notes.
                const auto note = static_cast<core::MidiNote>(43 + v * 4);
                voice->noteOn(core::PipeId{0, note},
                              100);
                voices.push_back(std::move(voice));
            }
        }

        std::vector<float> l(blockSize, 0.0f), r(blockSize, 0.0f);
        float* chans[2] = { l.data(), r.data() };

        auto renderOnce = [&] {
            for (std::size_t pos = 0; pos + blockSize <= totalFrames; pos += blockSize)
            {
                std::fill(l.begin(), l.end(), 0.0f);
                std::fill(r.begin(), r.end(), 0.0f);
                core::AudioBlock block(chans, 2, blockSize);
                for (auto& voice : voices)
                    voice->renderAdd(block);
            }
        };

        renderOnce(); // warm-up: discarded

        const auto timeOne = [&](caecilia::dsp::kernels::Backend backend)
        {
            caecilia::dsp::kernels::selectBackend(backend);
            const auto start = std::chrono::steady_clock::now();
            renderOnce();
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
        };

        if (ab)
        {
            namespace kern = caecilia::dsp::kernels;

            double bestScalar = std::numeric_limits<double>::infinity();
            double bestSse    = std::numeric_limits<double>::infinity();
            double bestAvx    = std::numeric_limits<double>::infinity();

            const bool haveAvx =
                kern::selectBackend(kern::Backend::Avx2) == kern::Backend::Avx2;

            for (int run = 0; run < repeats; ++run)
            {
                // Forward, then back, so no backend systematically gets the warm
                // cache or the cold one.
                bestScalar = std::min(bestScalar, timeOne(kern::Backend::Scalar));
                bestSse    = std::min(bestSse,    timeOne(kern::Backend::Sse2));
                if (haveAvx) bestAvx = std::min(bestAvx, timeOne(kern::Backend::Avx2));
                if (haveAvx) bestAvx = std::min(bestAvx, timeOne(kern::Backend::Avx2));
                bestSse    = std::min(bestSse,    timeOne(kern::Backend::Sse2));
                bestScalar = std::min(bestScalar, timeOne(kern::Backend::Scalar));
            }
            kern::selectBackend(kern::bestAvailableBackend());

            const double fastest = haveAvx ? std::min(bestSse, bestAvx) : bestSse;
            std::printf("%-24s %8zu %8zu %9.3fx %9.3fx %9.3fx %9.2fx\n",
                        s.name, partials, voices.size(),
                        bestScalar / seconds, bestSse / seconds,
                        haveAvx ? bestAvx / seconds : 0.0,
                        fastest > 0.0 ? bestScalar / fastest : 0.0);
            continue;
        }

        double best  = std::numeric_limits<double>::infinity();
        double worst = 0.0;
        for (int run = 0; run < repeats; ++run)
        {
            const double elapsed = timeOne(caecilia::dsp::kernels::activeBackend());
            best  = std::min(best, elapsed);
            worst = std::max(worst, elapsed);
        }

        const double rtFactor = best / seconds;
        const double spread   = best > 0.0 ? (worst - best) / best : 0.0;

        std::printf("%-24s %8zu %8zu %9.3fx %7.1f%% %9.1f%%\n",
                    s.name, partials, voices.size(), rtFactor,
                    100.0 * spread, 100.0 * (1.0 - rtFactor));
    }

    std::printf("\nRT factor is the fraction of one core needed for real time.\n"
                "Under 0.25 leaves the host comfortable room; over 1.0 xruns.\n"
                "spread = (slowest - fastest) / fastest across the timed runs. Above a\n"
                "few percent the machine is busy and the numbers are not comparable.\n");
    return 0;
}
