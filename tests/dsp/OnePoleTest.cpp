// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// OnePole tests, and specifically the coefficient/state split.
//
// The split exists so a bank of sections at one corner frequency costs one exp()
// instead of one per section. That is only a win if it is also a no-op: the
// decisive case below asserts BIT-IDENTICAL output between a filter configured
// through setLowpass and one configured through OnePoleCoeffs::lowpass, because
// "the reverb still sounds the same" is the whole claim being made.
//

#include "caecilia/dsp/OnePole.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace dsp = caecilia::dsp;

namespace
{
constexpr double kSampleRate = 48000.0;

/// A deterministic, spectrally busy input: a filter that only ever sees a sine
/// can hide a wrong zero.
std::vector<float> testSignal(std::size_t n)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto t = static_cast<double>(i);
        x[i] = static_cast<float>(0.6 * std::sin(0.017 * t)
                                + 0.3 * std::sin(0.41 * t)
                                + 0.1 * std::sin(2.9 * t));
    }
    return x;
}

/// Magnitude response at @p freqHz, measured rather than derived, by running the
/// filter to steady state and taking the peak of the output.
double magnitudeAt(dsp::OnePole filter, double freqHz)
{
    const double w = 2.0 * 3.14159265358979323846 * freqHz / kSampleRate;
    double       peak = 0.0;
    const auto   total = static_cast<std::size_t>(kSampleRate * 0.25);
    const auto   from  = total / 2; // discard the transient
    for (std::size_t i = 0; i < total; ++i)
    {
        const float y = filter.process(static_cast<float>(std::sin(w * static_cast<double>(i))));
        if (i >= from)
            peak = std::max(peak, std::abs(static_cast<double>(y)));
    }
    return peak;
}
} // namespace

TEST_CASE("OnePoleCoeffs::lowpass is bit-identical to OnePole::setLowpass", "[dsp][onepole]")
{
    const std::vector<float> x = testSignal(4096);

    for (const float cutoff : { 60.0f, 260.0f, 1000.0f, 6000.0f, 18000.0f })
    {
        dsp::OnePole viaSetter;
        viaSetter.prepare(kSampleRate);
        viaSetter.setLowpass(cutoff);

        dsp::OnePole viaCoeffs;
        viaCoeffs.setCoeffs(dsp::OnePoleCoeffs::lowpass(kSampleRate, cutoff));

        bool identical = true;
        for (const float s : x)
            identical = identical && (viaSetter.process(s) == viaCoeffs.process(s));

        INFO("cutoff = " << cutoff);
        CHECK(identical);
    }
}

TEST_CASE("OnePoleCoeffs::highpass is bit-identical to OnePole::setHighpass", "[dsp][onepole]")
{
    const std::vector<float> x = testSignal(4096);

    for (const float cutoff : { 20.0f, 200.0f, 2000.0f })
    {
        dsp::OnePole viaSetter;
        viaSetter.prepare(kSampleRate);
        viaSetter.setHighpass(cutoff);

        dsp::OnePole viaCoeffs;
        viaCoeffs.setCoeffs(dsp::OnePoleCoeffs::highpass(kSampleRate, cutoff));

        bool identical = true;
        for (const float s : x)
            identical = identical && (viaSetter.process(s) == viaCoeffs.process(s));

        INFO("cutoff = " << cutoff);
        CHECK(identical);
    }
}

TEST_CASE("OnePole::setCoeffs leaves the filter memory alone", "[dsp][onepole]")
{
    // Retuning a damper mid-tail must not click. If setCoeffs cleared the state
    // the FDN would drop a sample of its feedback every time the damping corner
    // moved, which is exactly what an automation sweep does.
    dsp::OnePole filter;
    filter.prepare(kSampleRate);
    filter.setLowpass(1000.0f);

    // Run it right to its DC gain first. A partially-settled filter still moves
    // by a few parts in 10^5 per sample, which would swamp the thing being
    // measured here.
    for (int i = 0; i < 2048; ++i)
        (void) filter.process(1.0f);

    const float before = filter.process(1.0f);
    filter.setCoeffs(filter.coeffs()); // same response, so same output
    const float after = filter.process(1.0f);

    CHECK(after == Approx(before).epsilon(1.0e-5));
    CHECK(after != 0.0f);
}

TEST_CASE("OnePole low-pass actually attenuates above its corner", "[dsp][onepole]")
{
    // Sanity on the response itself, so the bit-identity tests above cannot both
    // be identically wrong.
    dsp::OnePole lp;
    lp.prepare(kSampleRate);
    lp.setLowpass(1000.0f);

    const double atCorner = magnitudeAt(lp, 1000.0);
    const double wayAbove = magnitudeAt(lp, 12000.0);
    const double wayBelow = magnitudeAt(lp, 50.0);

    CHECK(wayBelow > 0.95);
    CHECK(atCorner == Approx(0.707).margin(0.06)); // -3 dB, within the impulse-invariant warp
    CHECK(wayAbove < 0.15);
}
