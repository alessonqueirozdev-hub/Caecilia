// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Resampler / Kaiser-windowed-sinc accuracy tests. The sample layer
// reads material at a continuously-variable fractional rate, so the
// reconstruction kernel must: preserve DC (unity gain), reproduce samples exactly
// at integer positions, interpolate a ramp exactly in Linear mode, and reconstruct
// a band-limited sinusoid to high accuracy in KaiserSinc16 mode (beating linear).
//

#include "caecilia/dsp/Resampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace dsp = caecilia::dsp;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/// RMS of |interp(pos) - analytic(pos)| across an interior sweep of positions.
double reconstructionRms(const dsp::Resampler& rs,
                         const std::vector<float>& source,
                         double periodSamples,
                         double firstPos,
                         double lastPos,
                         double step)
{
    double sumSq = 0.0;
    std::size_t count = 0;
    for (double pos = firstPos; pos <= lastPos; pos += step)
    {
        const double got = rs.interpolateAt(source.data(), source.size(), pos);
        const double want = std::sin(2.0 * kPi * pos / periodSamples);
        const double err = got - want;
        sumSq += err * err;
        ++count;
    }
    return count ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
}
} // namespace

TEST_CASE("Resampler preserves DC (unity-gain interpolation)", "[dsp][resampler]")
{
    dsp::Resampler rs;
    rs.prepare(dsp::Resampler::Quality::KaiserSinc16, 512);

    const std::vector<float> constant(64, 1.0f);
    for (double pos : {10.0, 10.25, 10.5, 10.75, 20.3, 41.9})
        CHECK(rs.interpolateAt(constant.data(), constant.size(), pos) == Approx(1.0f).margin(1e-4));
}

TEST_CASE("Resampler reproduces exact samples at integer positions", "[dsp][resampler]")
{
    dsp::Resampler rs;
    rs.prepare(dsp::Resampler::Quality::KaiserSinc16, 512);

    std::vector<float> source(64);
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = std::sin(0.37f * static_cast<float>(i));

    // Interior positions only, so the 16-tap window never clamps at an edge.
    for (std::size_t m = 8; m <= 55; ++m)
    {
        const double got = rs.interpolateAt(source.data(), source.size(), static_cast<double>(m));
        CHECK(got == Approx(source[m]).margin(1e-4));
    }
}

TEST_CASE("Linear quality is exact on a linear ramp", "[dsp][resampler]")
{
    dsp::Resampler rs;
    rs.prepare(dsp::Resampler::Quality::Linear, 512);
    CHECK(rs.quality() == dsp::Resampler::Quality::Linear);

    std::vector<float> ramp(64);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = 2.0f * static_cast<float>(i) + 1.0f;

    for (double pos = 5.0; pos <= 50.0; pos += 0.25)
    {
        const double got  = rs.interpolateAt(ramp.data(), ramp.size(), pos);
        const double want = 2.0 * pos + 1.0;
        CHECK(got == Approx(want).margin(1e-4));
    }
}

TEST_CASE("Kaiser-sinc reconstructs a sinusoid accurately and beats linear", "[dsp][resampler]")
{
    // A pure tone at fs/8 (well below Nyquist) is exactly reconstructible; the
    // 16-tap windowed sinc should be near-transparent, and clearly better than a
    // 2-point linear read.
    constexpr double period = 8.0;
    std::vector<float> source(256);
    for (std::size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(i) / period));

    dsp::Resampler kaiser;
    kaiser.prepare(dsp::Resampler::Quality::KaiserSinc16, 1024);

    dsp::Resampler linear;
    linear.prepare(dsp::Resampler::Quality::Linear, 1024);

    const double kaiserRms = reconstructionRms(kaiser, source, period, 16.0, 240.0, 0.37);
    const double linearRms = reconstructionRms(linear, source, period, 16.0, 240.0, 0.37);

    CHECK(kaiserRms < 5e-3);          // near-transparent reconstruction
    CHECK(kaiserRms < linearRms);     // and strictly better than linear
    CHECK(linearRms > 1e-2);          // linear visibly errs at 8 samples/cycle
}

TEST_CASE("Resampler::readBlock zero-fills past the end of the source", "[dsp][resampler]")
{
    dsp::Resampler rs;
    rs.prepare(dsp::Resampler::Quality::KaiserSinc16, 512);

    const std::vector<float> source(32, 1.0f);
    std::vector<float>       output(8, -99.0f);

    double readPos = 30.0;
    rs.readBlock(source.data(), source.size(), readPos, /*ratio*/ 1.0,
                 output.data(), output.size());

    // Positions 30 and 31 are inside the (constant) source -> ~1.0; from 32 on the
    // reader has run off the end and must emit exact silence.
    CHECK(output[0] == Approx(1.0f).margin(1e-4));
    CHECK(output[1] == Approx(1.0f).margin(1e-4));
    CHECK(output[2] == 0.0f);
    CHECK(output[7] == 0.0f);
    CHECK(readPos >= 32.0);
}
