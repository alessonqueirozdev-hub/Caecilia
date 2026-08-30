// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

/**
 * @file
 * @brief Measuring what a rank actually SOUNDS like, not what it says it will.
 *
 * Every spectral test in this tree so far has checked a @c SpectralModel -- the
 * description of a timbre. None has checked the audio that comes out of the
 * synthesiser fed by it. Those are different claims, and the second is the one an
 * organist hears: a partial the model lists but the bank never renders, a level
 * the recipe sets and the wind coupling then eats, an alias folding down from a
 * top-octave mixture, all of them live in the gap.
 *
 * So this renders a note and measures the result, by Goertzel over a Hann window.
 * The window is not a nicety: a rectangular Goertzel's sidelobes fall off so
 * slowly that a strong fundamental leaks tens of decibels into the bin where a
 * weak second harmonic is being looked for, which is exactly the measurement these
 * tests need to be able to trust.
 */

#include "caecilia/core/AudioBlock.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace caecilia::testing
{

/// Equal-tempered frequency of a MIDI note at A4 = 440.
[[nodiscard]] inline double noteHz(int midiNote)
{
    return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
}

/**
 * @brief Render one rank's voice at @p note into a mono buffer.
 * @param voicing     The rank, as buildRankVoicing produced it.
 * @param note        MIDI note to sound.
 * @param seconds     How long to render, after the attack.
 * @param sampleRate  Render rate.
 *
 * The first tenth of a second is rendered and DISCARDED. A pipe's speech is a
 * transient with its own spectrum -- that is the whole point of chiff -- and
 * measuring the steady tone means measuring after it has arrived.
 */
[[nodiscard]] inline std::vector<float> renderRankNote(const synth::RankVoicing& voicing,
                                                       int    note,
                                                       double seconds     = 0.5,
                                                       double sampleRate  = 48000.0)
{
    constexpr std::size_t kBlock = 256;

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
    voice.prepare(sampleRate, kBlock);
    voice.adoptRank(&voicing);
    voice.noteOn(core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 }, 100);

    const auto settle = static_cast<std::size_t>(0.10 * sampleRate / kBlock);
    const auto blocks = static_cast<std::size_t>(seconds * sampleRate / kBlock);

    std::vector<float> out;
    out.reserve(blocks * kBlock);

    std::vector<float> left(kBlock), right(kBlock);
    for (std::size_t b = 0; b < settle + blocks; ++b)
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        float*      chans[2] = { left.data(), right.data() };
        core::AudioBlock block(chans, 2, kBlock);
        voice.renderAdd(block);

        if (b < settle)
            continue;

        // Mono sum. A rank is panned and scattered across the case, and a
        // single-channel measurement of a stereo image would report the pan as a
        // level difference between harmonics.
        for (std::size_t i = 0; i < kBlock; ++i)
            out.push_back(0.5f * (left[i] + right[i]));
    }
    return out;
}

/**
 * @brief Energy at @p freqHz in @p signal, by Goertzel over a Hann window.
 * @return Magnitude, in the same arbitrary units for every frequency, so ratios
 *         between two calls are meaningful and the absolute value is not.
 */
[[nodiscard]] inline double magnitudeAt(const std::vector<float>& signal,
                                        double freqHz, double sampleRate = 48000.0)
{
    const std::size_t n = signal.size();
    if (n == 0)
        return 0.0;

    const double omega = 2.0 * std::numbers::pi * freqHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);

    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        // Hann, so a strong neighbour does not leak into this bin.
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi
                                              * static_cast<double>(i)
                                              / static_cast<double>(n - 1));
        const double s0 = w * static_cast<double>(signal[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const double real = s1 - s2 * std::cos(omega);
    const double imag = s2 * std::sin(omega);
    return std::sqrt(real * real + imag * imag) / static_cast<double>(n);
}

/**
 * @brief The strongest energy NEAR @p targetHz, within @p tolerance (a fraction).
 *
 * Not at it. A rank's pipes are deliberately scattered a few cents from exact
 * equal temperament -- a rank with zero scatter is a synthesiser, not an organ --
 * so its partials do not sit on integer multiples of the nominal fundamental.
 * Measuring at the nominal frequency finds the SKIRT of the partial rather than
 * its peak, and because the error in hertz grows with the harmonic number, the
 * reading gets progressively darker up the series.
 *
 * That artefact cost an afternoon: it looks exactly like a synthesiser failing to
 * honour the upper partials of its own model, which is a thing worth investigating
 * and was not what was happening.
 */
[[nodiscard]] inline double magnitudeNear(const std::vector<float>& signal,
                                          double targetHz, double tolerance = 0.015,
                                          double sampleRate = 48000.0)
{
    constexpr int kSteps = 41;
    double best = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        const double frac = -1.0 + 2.0 * static_cast<double>(i) / (kSteps - 1);
        const double hz   = targetHz * (1.0 + tolerance * frac);
        best = std::max(best, magnitudeAt(signal, hz, sampleRate));
    }
    return best;
}

/// @return The level of harmonic @p n relative to the fundamental, in decibels.
///
/// Negative is quieter than the fundamental, which is what every partial of an
/// organ pipe is. -120 stands for "not there at all" rather than -inf, so a test
/// can compare it like any other number.
[[nodiscard]] inline double harmonicDb(const std::vector<float>& signal,
                                       double fundamentalHz, int n,
                                       double sampleRate = 48000.0)
{
    const double f0 = magnitudeNear(signal, fundamentalHz, 0.015, sampleRate);
    const double fn = magnitudeNear(signal, fundamentalHz * n, 0.015, sampleRate);
    if (!(f0 > 0.0) || !(fn > 0.0))
        return -120.0;
    return 20.0 * std::log10(fn / f0);
}

/// @return The voicing of the stop named @p name, or an empty one.
[[nodiscard]] inline synth::RankVoicing voicingNamed(const model::Organ& organ,
                                                     std::string_view    name)
{
    for (const model::Stop& s : organ.stops())
        if (s.name() == name)
            return model::buildRankVoicing(organ, s.id());
    return {};
}

} // namespace caecilia::testing
