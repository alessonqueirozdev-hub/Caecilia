// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// DspMath tests. Right now this covers fastSineTurns, which replaces std::sin on
// the FDN reverb's modulation path — 768,000 calls a second, unconditionally, on
// the audio thread.
//
// The whole justification for an approximation is a number: how far it can be
// from the real thing. So that number is asserted here rather than asserted in
// prose, and it is asserted over the awkward inputs too (negative phases, phases
// far outside one turn, the exact cardinal points), because range reduction is
// where this class of function actually goes wrong.
//

#include "caecilia/dsp/DspMath.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
namespace dsp = caecilia::dsp;

namespace
{
/// The documented worst case. Measured at 7.1e-7 by the Remez fit that produced
/// the coefficients; asserted with a little headroom for a different rounding
/// mode or an FMA contraction on another compiler.
constexpr double kTolerance = 1.0e-6;

double reference(double turns) noexcept
{
    return std::sin(2.0 * dsp::kPi * turns);
}
} // namespace

TEST_CASE("fastSineTurns tracks std::sin across a full turn", "[dsp][math]")
{
    double worst = 0.0;
    double worstAt = 0.0;

    // A prime step so the samples do not land only on convenient phases.
    constexpr int kSteps = 100003;
    for (int i = 0; i < kSteps; ++i)
    {
        const double turns = static_cast<double>(i) / kSteps;
        const double err   = std::abs(static_cast<double>(dsp::fastSineTurns(static_cast<float>(turns)))
                                      - reference(turns));
        if (err > worst)
        {
            worst   = err;
            worstAt = turns;
        }
    }

    INFO("worst error " << worst << " at " << worstAt << " turns");
    CHECK(worst < kTolerance);
}

TEST_CASE("fastSineTurns is exact at the cardinal phases", "[dsp][math]")
{
    // Zero crossings must be exactly zero: a residual there would leave a DC
    // offset on anything this drives.
    CHECK(dsp::fastSineTurns(0.0f) == 0.0f);
    CHECK(std::abs(dsp::fastSineTurns(0.5f)) == 0.0f);
    CHECK(std::abs(dsp::fastSineTurns(1.0f)) == 0.0f);
    CHECK(std::abs(dsp::fastSineTurns(-1.0f)) == 0.0f);

    // The peaks are the polynomial's worst case by construction, so they are
    // near +/-1 rather than exactly it — but they must never exceed unity, or a
    // caller that scales by a depth budget overshoots it.
    CHECK(dsp::fastSineTurns(0.25f) == Approx(1.0).margin(kTolerance));
    CHECK(dsp::fastSineTurns(0.75f) == Approx(-1.0).margin(kTolerance));
    CHECK(std::abs(dsp::fastSineTurns(0.25f)) <= 1.0f);
    CHECK(std::abs(dsp::fastSineTurns(0.75f)) <= 1.0f);
}

TEST_CASE("fastSineTurns range-reduces negative and out-of-range phases", "[dsp][math]")
{
    // Range reduction is the part of a fast sine that breaks. An LFO phase
    // accumulator is wrapped by its owner, but nothing in the signature says so,
    // and a caller that forgets must not get garbage.
    for (const float turns : { -0.125f, -0.5f, -0.9f, -3.25f, 1.125f, 7.75f, 41.3f, -41.3f })
    {
        INFO("turns = " << turns);
        CHECK(static_cast<double>(dsp::fastSineTurns(turns))
              == Approx(reference(static_cast<double>(turns))).margin(kTolerance));
    }
}

TEST_CASE("fastSineTurns is odd and periodic", "[dsp][math]")
{
    // Two identities the fold must preserve. If the sign handling in the fold is
    // wrong, the error shows here long before it shows in a tolerance sweep.
    for (int i = 1; i < 500; ++i)
    {
        const float t = static_cast<float>(i) / 500.0f;
        INFO("turns = " << t);
        CHECK(dsp::fastSineTurns(-t) == Approx(-dsp::fastSineTurns(t)).margin(1.0e-7));
        CHECK(dsp::fastSineTurns(t + 1.0f) == Approx(dsp::fastSineTurns(t)).margin(1.0e-6));
    }
}

TEST_CASE("fastSineTurns beats the same-length Taylor series", "[dsp][math]")
{
    // The reason for a minimax fit rather than the obvious Taylor coefficients.
    // Both are four terms; the Taylor series concentrates its accuracy at zero
    // and is worst at the quarter turn, which is exactly where an LFO peaks.
    const auto taylor = [](double turns)
    {
        double t = turns - std::floor(turns + 0.5);
        const double a = std::abs(t);
        const double folded = 0.25 - std::abs(a - 0.25);
        const double x  = 2.0 * dsp::kPi * std::copysign(folded, t);
        const double x2 = x * x;
        return x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0))));
    };

    double worstMinimax = 0.0;
    double worstTaylor  = 0.0;
    for (int i = 0; i <= 20000; ++i)
    {
        const double turns = static_cast<double>(i) / 20000.0;
        worstMinimax = std::max(worstMinimax,
                                std::abs(static_cast<double>(dsp::fastSineTurns(static_cast<float>(turns)))
                                         - reference(turns)));
        worstTaylor  = std::max(worstTaylor, std::abs(taylor(turns) - reference(turns)));
    }

    INFO("minimax " << worstMinimax << " vs taylor " << worstTaylor);
    CHECK(worstMinimax * 50.0 < worstTaylor);
}


TEST_CASE("equalPowerPan reproduces the trig law it tabulates", "[dsp][math][pan]")
{
    // The table replaced a cos/sin pair that ran once per partial per note-on.
    // It is only allowed to be cheaper, not different.
    double worst = 0.0;
    for (int i = -2000; i <= 2000; ++i)
    {
        const float pan = static_cast<float>(i) / 1000.0f; // includes out-of-range
        float l = 0.0f, r = 0.0f;
        dsp::equalPowerPan(pan, l, r);

        const double clamped = pan < -1.0f ? -1.0 : (pan > 1.0f ? 1.0 : pan);
        const double theta   = (clamped + 1.0) * 0.25 * dsp::kPi;
        worst = std::max({ worst,
                           std::abs(static_cast<double>(l) - std::cos(theta)),
                           std::abs(static_cast<double>(r) - std::sin(theta)) });
    }
    INFO("worst gain error " << worst);
    CHECK(worst < 1.0e-5);
}

TEST_CASE("equalPowerPan keeps constant power across the image", "[dsp][math][pan]")
{
    // The failure a cheap pan law is usually guilty of: interpolating the two
    // gains independently bows the sum of squares, so a centred source ends up
    // louder (or quieter) than a panned one. On an organ that would make every
    // rank's level depend on where it happens to sit in the case.
    double worst = 0.0;
    for (int i = -1000; i <= 1000; ++i)
    {
        float l = 0.0f, r = 0.0f;
        dsp::equalPowerPan(static_cast<float>(i) / 1000.0f, l, r);
        worst = std::max(worst, std::abs(static_cast<double>(l) * l
                                       + static_cast<double>(r) * r - 1.0));
    }
    INFO("worst |L^2 + R^2 - 1| = " << worst);
    CHECK(worst < 1.0e-4);

    // Endpoints and centre must still be exactly what they claim.
    float l = 0.0f, r = 0.0f;
    dsp::equalPowerPan(-1.0f, l, r);
    CHECK(l == Approx(1.0).margin(1e-5));
    CHECK(r == Approx(0.0).margin(1e-5));
    dsp::equalPowerPan(1.0f, l, r);
    CHECK(l == Approx(0.0).margin(1e-5));
    CHECK(r == Approx(1.0).margin(1e-5));
    dsp::equalPowerPan(0.0f, l, r);
    CHECK(l == Approx(0.70710678).margin(1e-5));
    CHECK(r == Approx(0.70710678).margin(1e-5));
}

TEST_CASE("randomPhasor lands on the unit circle and spreads over it", "[dsp][math]")
{
    // It stands in for cos/sin of a random angle, so it has to be a unit vector
    // (anything else scales the oscillator it seeds) and it has to actually
    // scatter -- a table indexed by too few bits would cluster, and clustered
    // start phases are what makes voices sum coherently.
    int quadrant[4] = { 0, 0, 0, 0 };
    double worstMag = 0.0;

    std::uint32_t h = 0x12345678u;
    for (int i = 0; i < 20000; ++i)
    {
        h = h * 1664525u + 1013904223u;
        const dsp::Phasor p = dsp::randomPhasor(h);
        const double mag = static_cast<double>(p.cos) * p.cos + static_cast<double>(p.sin) * p.sin;
        worstMag = std::max(worstMag, std::abs(mag - 1.0));
        quadrant[(p.cos >= 0.0f ? 0 : 1) + (p.sin >= 0.0f ? 0 : 2)] += 1;
    }

    INFO("worst |z|^2 - 1 = " << worstMag);
    CHECK(worstMag < 1.0e-6);
    for (int q = 0; q < 4; ++q)
    {
        INFO("quadrant " << q << " got " << quadrant[q] << " of 20000");
        CHECK(quadrant[q] > 4000);
        CHECK(quadrant[q] < 6000);
    }
}
