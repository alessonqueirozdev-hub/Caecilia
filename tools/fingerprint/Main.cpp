// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// caecilia-fingerprint — a numeric signature of how the instrument SOUNDS.
//
// Unit tests prove individual behaviours. They cannot tell you that a large
// refactor left the instrument sounding the same, because "the same" is a
// property of the rendered audio, not of any one function. This tool renders a
// fixed set of musically meaningful cases and prints a stable set of numbers for
// each: level, dynamics, brightness, stereo image.
//
// The workflow is: run it and keep the output BEFORE a risky change, run it
// again after, diff the two. Every number that moved is a question to answer,
// and every number that did not is a guarantee earned.
//
// KNOW THE NOISE FLOOR BEFORE READING A DIFF. Ranks a fraction of a cent apart
// beat with a period of tens of seconds, so a window shorter than that reports
// which instant of a beat it caught rather than a level. Measured by rendering
// these cases from two builds differing ONLY in the start phases of their
// partials -- a change that cannot alter any level:
//
//     window     worst |delta| RMS      second worst
//       6 s            1.51 dB             1.36 dB
//      15 s            0.65 dB             0.49 dB
//      30 s            0.55 dB             0.13 dB
//      60 s            0.35 dB             0.31 dB
//
// Single-rank cases read identically at every window; the residue is entirely in
// the multi-rank ones. So: on a single-rank case, any movement at all is real. On
// a multi-rank case at the default window, treat under half a dB as noise, and
// run --seconds 60 before calling anything a regression.
//
// It exists specifically for the one-voice-per-(rank,note) migration, where the
// whole voicing path is rebuilt underneath a sound that is already tuned.
//
// Usage:
//   caecilia-fingerprint [--sr 48000] [--block 512] [--seconds 30]
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/Kernels.h"
#include "caecilia/dsp/Biquad.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"

#include "common/CliArgs.h"
#include "common/DenormalGuard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace core  = caecilia::core;
namespace dsp   = caecilia::dsp;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
model::RegistrationRank rank(core::TonalFamily fam, double feet, bool compound = false)
{
    return model::RegistrationRank{ fam, model::footageFromFeet(feet), compound };
}

/// One rendered case: what was drawn, and what was played.
struct Case
{
    const char*                          name;
    std::vector<model::RegistrationRank> ranks;
    std::vector<int>                     notes;   ///< MIDI notes held together.
};

std::vector<model::RegistrationRank> plenum()
{
    return { rank(core::TonalFamily::Principal, 16), rank(core::TonalFamily::Principal, 8),
             rank(core::TonalFamily::Flute, 8),      rank(core::TonalFamily::Principal, 4),
             rank(core::TonalFamily::Principal, 2),  rank(core::TonalFamily::Mixture, 2, true) };
}

std::vector<model::RegistrationRank> tutti()
{
    auto v = plenum();
    for (const auto& extra : {
             rank(core::TonalFamily::Flute, 16),        rank(core::TonalFamily::Flute, 4),
             rank(core::TonalFamily::Flute, 8.0 / 3.0), rank(core::TonalFamily::Flute, 8.0 / 5.0),
             rank(core::TonalFamily::String, 8),        rank(core::TonalFamily::String, 8),
             rank(core::TonalFamily::Reed, 16),         rank(core::TonalFamily::Reed, 8),
             rank(core::TonalFamily::Reed, 4),          rank(core::TonalFamily::Mixture, 2, true),
             rank(core::TonalFamily::Principal, 8),     rank(core::TonalFamily::Reed, 8) })
        v.push_back(extra);
    return v;
}

std::vector<Case> cases()
{
    return {
        // Softest end of the range: one quiet flute, one note.
        { "soft flute 4', C4",        { rank(core::TonalFamily::Flute, 4) },     { 60 } },
        { "single Principal 8', C4",  { rank(core::TonalFamily::Principal, 8) }, { 60 } },
        // Bass: where envelope length and 16' weight show up.
        { "Principal 16', C2",        { rank(core::TonalFamily::Principal, 16) }, { 36 } },
        // Reeds: the formant character.
        { "Trompette 8', C4",         { rank(core::TonalFamily::Reed, 8) },      { 60 } },
        // Strings: the celeste beat.
        { "Gambe + Celeste 8', A3",   { rank(core::TonalFamily::String, 8),
                                        rank(core::TonalFamily::String, 8) },    { 57 } },
        // Mutations: the quint and tierce must land on the right harmonics.
        { "Nazard 2 2/3', C4",        { rank(core::TonalFamily::Flute, 8.0 / 3.0) }, { 60 } },
        { "Tierce 1 3/5', C4",        { rank(core::TonalFamily::Flute, 8.0 / 5.0) }, { 60 } },
        // Mixtures across the compass: this is where break-back shows.
        { "Fourniture IV, C3",        { rank(core::TonalFamily::Mixture, 2, true) }, { 48 } },
        { "Fourniture IV, C6",        { rank(core::TonalFamily::Mixture, 2, true) }, { 84 } },
        // The chorus, as it is actually played.
        { "plenum, C major triad",    plenum(),                                  { 60, 64, 67 } },
        { "plenum, C2 pedal point",   plenum(),                                  { 36 } },
        // Full organ, the worst case for level and for CPU.
        { "tutti, C major triad",     tutti(),                                   { 60, 64, 67 } },
        { "tutti, 10-note cluster",   tutti(), { 36, 43, 48, 52, 55, 60, 64, 67, 72, 76 } },
        // Top of the compass, where the anti-aliasing gate and the treble tilt bite.
        { "tutti, C6",                tutti(),                                   { 84 } },
    };
}

/// Energy in four bands, as a fraction of the total. A stable, FFT-free way to
/// say "the timbre moved" without pretending to be a spectrum analyser.
struct Bands { double low, lowMid, highMid, high; };

Bands bandEnergy(const std::vector<float>& x, core::SampleRate sr, std::size_t from)
{
    auto energyThrough = [&](const dsp::BiquadCoeffs& c) {
        dsp::Biquad f;
        f.setCoeffs(c);
        std::vector<float> tmp(x.begin(), x.end());
        f.processBlock(tmp.data(), tmp.size());
        double e = 0.0;
        for (std::size_t i = from; i < tmp.size(); ++i)
            e += static_cast<double>(tmp[i]) * tmp[i];
        return e;
    };

    Bands b{};
    b.low     = energyThrough(dsp::BiquadCoeffs::lowpass(sr, 200.0f, 0.707f));
    b.lowMid  = energyThrough(dsp::BiquadCoeffs::bandpass(sr, 600.0f, 0.9f));
    b.highMid = energyThrough(dsp::BiquadCoeffs::bandpass(sr, 2200.0f, 0.9f));
    b.high    = energyThrough(dsp::BiquadCoeffs::highpass(sr, 5000.0f, 0.707f));

    const double total = b.low + b.lowMid + b.highMid + b.high;
    if (total > 0.0)
    {
        b.low /= total; b.lowMid /= total; b.highMid /= total; b.high /= total;
    }
    return b;
}
} // namespace

int main(int argc, char** argv)
{
    // Same thread mode the plugin renders in, so the fingerprint describes the
    // instrument a listener actually hears.
    const caecilia::tools::DenormalGuard denormalGuard;

    const caecilia::tools::CliArgs args(argc, argv);
    const double sampleRate = args.number("sr").value_or(48000.0);
    const auto   blockSize  = static_cast<std::size_t>(args.integer("block").value_or(512));

    // Near-unison partials from different ranks beat against each other and each
    // partial drifts, so a short window samples one instant of a moving target.
    // The default six seconds averages the fast beats -- a celeste, a rank detune
    // of a couple of cents -- but not the slowest: two ranks a fifth of a cent
    // apart at 1 kHz beat with a period of eight seconds, and a window shorter
    // than that reports whichever half of the cycle it happened to catch. Raise
    // --seconds when a diff has to tell a real level change from a beat the
    // window failed to average over.
    const double seconds     = args.number("seconds").value_or(30.0);

    // Which oscillator backend to render through. The equivalence gate between the
    // scalar reference and the vector path is a DIFF OF THIS TABLE, not sample
    // equality: the vector path advances the phasor in strides, so the rounding
    // trajectories separate by design, and any REQUIRE(a[n] == b[n]) over a long
    // render would fail for no musical reason. The fingerprint is quantised far
    // coarser than that divergence and is byte-deterministic, so it catches a real
    // difference and ignores the inevitable one.
    {
        const std::string backend = args.value("backend", "auto");
        namespace kern = caecilia::dsp::kernels;
        if (backend == "scalar")
            kern::selectBackend(kern::Backend::Scalar);
        else if (backend == "sse2")
            kern::selectBackend(kern::Backend::Sse2);
        else if (backend == "avx2")
            kern::selectBackend(kern::Backend::Avx2);
        else
            kern::selectBackend(kern::bestAvailableBackend());
    }
    const auto   total       = static_cast<std::size_t>(sampleRate * seconds);
    const auto   measureFrom =
        static_cast<std::size_t>(sampleRate * std::min(1.5, seconds * 0.25));

    std::printf("caecilia-fingerprint  %.0f Hz, block %zu, %.1f s per case\n\n",
                sampleRate, blockSize, seconds);
    std::printf("%-28s %5s %9s %9s %7s %6s   %5s %5s %5s %5s\n",
                "case", "prtls", "rms dBFS", "peak dBFS", "crest", "corr",
                "low", "lmid", "hmid", "high");
    std::printf("%-28s %5s %9s %9s %7s %6s   %5s %5s %5s %5s\n",
                "----------------------------", "-----", "---------", "---------",
                "-------", "------", "-----", "-----", "-----", "-----");

    for (const Case& c : cases())
    {
        const synth::SpectralModel composite =
            model::buildCompositeFromRegistration(
                std::span<const model::RegistrationRank>(c.ranks));

        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;

        std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
        for (const int note : c.notes)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(std::max<std::size_t>(composite.partials.size(), 16));
            v->prepare(sampleRate, blockSize);
            v->setContext(ctx);
            v->seedFrom(composite);
            v->noteOn(core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 }, 100);
            voices.push_back(std::move(v));
        }

        std::vector<float> l(total, 0.0f), r(total, 0.0f);
        for (std::size_t pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            float* chans[2] = { l.data() + pos, r.data() + pos };
            core::AudioBlock block(chans, 2, blockSize);
            for (auto& v : voices)
                v->renderAdd(block);
        }

        // Level, dynamics and stereo image over the steady portion.
        double sumSq = 0.0, peak = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        for (std::size_t i = measureFrom; i < total; ++i)
        {
            const double a = l[i], b = r[i];
            sumSq += a * a + b * b;
            peak   = std::max({ peak, std::fabs(a), std::fabs(b) });
            sumLR += a * b; sumLL += a * a; sumRR += b * b;
        }
        const auto n   = static_cast<double>((total - measureFrom) * 2);
        const double rms = std::sqrt(sumSq / n);
        const double corr = (sumLL > 0.0 && sumRR > 0.0)
                          ? sumLR / std::sqrt(sumLL * sumRR) : 1.0;
        const auto db = [](double v) { return 20.0 * std::log10(v > 1e-12 ? v : 1e-12); };

        const Bands bands = bandEnergy(l, sampleRate, measureFrom);

        std::printf("%-28s %5zu %9.2f %9.2f %7.2f %6.3f   %5.3f %5.3f %5.3f %5.3f\n",
                    c.name, composite.partials.size(), db(rms), db(peak),
                    rms > 0.0 ? peak / rms : 0.0, corr,
                    bands.low, bands.lowMid, bands.highMid, bands.high);
    }

    std::printf("\ncrest = peak/rms. corr = stereo correlation: 1.0 is dead mono,\n"
                "lower means the case is genuinely spread across the image.\n"
                "The four band columns are fractions of total energy and sum to 1.\n"
                "\nReading a diff: single-rank cases are exact, so any movement is real.\n"
                "Multi-rank cases carry slow beats between near-unison ranks -- at this\n"
                "window that is +/- 0.55 dB of RMS noise, so treat half a dB as nothing\n"
                "and re-run with --seconds 60 before calling it a regression. peak and\n"
                "crest are phase-dependent by nature and never settle.\n");
    return 0;
}
