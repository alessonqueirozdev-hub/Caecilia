// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The reservoir plate, and why a first-order bellows cannot sound like one.
//
// The bellows integrated a first-order relaxation: pressure slid toward the
// equilibrium the demand set and stopped there. A real reservoir has a top plate
// with MASS, so a chord landing on it makes the plate drop, overshoot and bounce
// a few times at two to eight hertz before settling -- and every pipe on that
// reservoir goes flat, quiet and dull in time with the bounce, because pitch,
// level, brightness and speech all track the deviation.
//
// That bounce is what an organist means by the wind breathing. A model that can
// only sag can sag convincingly and will never breathe.
//

#include "caecilia/wind/Bellows.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace wind = caecilia::wind;

namespace
{
constexpr float  kNominal = 800.0f;
constexpr float  kDemand  = 40.0f;
constexpr float  kFeed    = 4.0f;   // 40 / 4 = 10 Pa of sag
constexpr double kSr      = 48000.0;

wind::BellowsConfig config(float resonanceHz, float damping)
{
    wind::BellowsConfig c;
    c.nominalPressurePa = kNominal;
    c.compliance        = 1.0f;
    c.feedConductance   = kFeed;
    c.plateResonanceHz  = resonanceHz;
    c.plateDamping      = damping;
    return c;
}

/// Hold @p demand for @p seconds and return the pressure at the end of every
/// block. Blocks are @p frames long, which is how a host would drive this.
std::vector<float> run(wind::Bellows& b, float demand, double seconds,
                       std::size_t frames = 512)
{
    const double dt = static_cast<double>(frames) / kSr;
    const auto   n  = static_cast<std::size_t>(seconds / dt);

    std::vector<float> trace;
    trace.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        b.beginStep();
        b.addDemand(demand);
        b.integrate(dt);
        trace.push_back(b.pressureEndPa());
    }
    return trace;
}

/// The equilibrium a steady demand settles at.
constexpr float equilibrium(float demand) { return kNominal - demand / kFeed; }
} // namespace

TEST_CASE("A plate of no mass is the bellows that was there before",
          "[wind][bellows]")
{
    // Zero is the default, and it has to keep meaning "exactly what this did
    // yesterday" -- every caller that has not thought about the plate, and every
    // test written against the old behaviour, depends on it.
    wind::Bellows b;
    b.configure(config(/*resonanceHz*/ 0.0f, 0.45f));

    // Three seconds, not one. The first-order rate here is G/C = 4 per second, so
    // after one second there are still 10*exp(-4) = 0.18 Pa of approach left --
    // measuring the destination at one second measures the journey.
    const std::vector<float> trace = run(b, kDemand, 3.0);
    REQUIRE_FALSE(trace.empty());

    // First order means monotone: it approaches the equilibrium and never passes
    // it. That single property is what distinguishes the two models.
    const float target = equilibrium(kDemand);
    for (std::size_t i = 1; i < trace.size(); ++i)
    {
        INFO("block " << i << ": " << trace[i - 1] << " -> " << trace[i]);
        CHECK(trace[i] <= trace[i - 1] + 1.0e-4f); // never rises
        CHECK(trace[i] >= target - 1.0e-3f);       // never undershoots
    }
    CHECK(trace.back() == Approx(target).margin(0.05));
}

TEST_CASE("A plate with mass drops past the pressure and comes back",
          "[wind][bellows][regression]")
{
    // THE point. The chord does not lower the wind, it lands on it.
    wind::Bellows b;
    b.configure(config(4.5f, 0.45f));

    const std::vector<float> trace  = run(b, kDemand, 2.0);
    const float              target = equilibrium(kDemand);
    const float              lowest = *std::min_element(trace.begin(), trace.end());

    // Undershoot: a damping ratio of 0.45 overshoots by exp(-pi*z/sqrt(1-z^2)),
    // which is 20.5% of the ten-pascal step -- about two pascals below target.
    const float step      = kNominal - target;
    const float overshoot = (target - lowest) / step;

    INFO("target " << target << " Pa, lowest " << lowest << " Pa, overshoot "
                   << (overshoot * 100.0f) << "%");
    CHECK(overshoot > 0.15f);
    CHECK(overshoot < 0.27f);

    // And it comes back up: something after the trough is above the target again.
    const auto  troughAt = static_cast<std::size_t>(
        std::min_element(trace.begin(), trace.end()) - trace.begin());
    const float peakAfter = *std::max_element(trace.begin() + static_cast<long>(troughAt),
                                              trace.end());
    CHECK(peakAfter > target);

    // Then it settles where the first-order model would have.
    CHECK(trace.back() == Approx(target).margin(0.05));
}

TEST_CASE("The reservoir rings at the frequency it was given", "[wind][bellows]")
{
    // A damped oscillator rings at f0 * sqrt(1 - zeta^2), not at f0. At 4.5 Hz and
    // 0.45 that is 4.02 Hz -- a period of 249 ms, which is what an organist hears
    // as the reservoir recovering rather than as a wobble.
    wind::Bellows b;
    b.configure(config(4.5f, 0.30f)); // livelier, so there are peaks to count

    constexpr std::size_t kFrames = 128; // 2.67 ms: fine enough to place a trough
    const double          dt      = static_cast<double>(kFrames) / kSr;
    const std::vector<float> trace = run(b, kDemand, 2.0, kFrames);

    // Local minima of the trace, which are one damped period apart.
    std::vector<std::size_t> troughs;
    for (std::size_t i = 1; i + 1 < trace.size(); ++i)
        if (trace[i] < trace[i - 1] && trace[i] <= trace[i + 1])
            troughs.push_back(i);

    REQUIRE(troughs.size() >= 5);

    // THREE periods, not one. A trough lands on a whole block, so measuring one
    // period carries a block of quantisation -- 1.1% here -- and the thing this has
    // to separate is only 4.8%: the damped frequency against the undamped one it
    // would be if the sqrt(1 - zeta^2) were dropped. Over three periods the
    // quantisation is a third of that and the two are clearly different numbers.
    const double period = static_cast<double>(troughs[4] - troughs[1]) * dt / 3.0;
    const double hz     = 1.0 / period;
    const double wanted = 4.5 * std::sqrt(1.0 - 0.30 * 0.30);

    // Measured: 4.293 Hz, which is the damped frequency to three figures.
    //
    // What this can and cannot resolve is worth writing down. Drop the
    // sqrt(1 - zeta^2) entirely -- integrate as though the plate were undamped --
    // and the measured ring moves only to 4.377, not to 4.5: the trough spacing of
    // a DECAYING oscillation is not simply one over its frequency, so substituting
    // one for the other shows up smaller than the substitution. Two percent is
    // below what a block-quantised trough count separates cleanly, so this test
    // does not catch that mutation and is not written as though it did. It catches
    // a plate ringing at the wrong rate, which is what it is for.
    INFO(troughs.size() << " troughs, period " << (period * 1000.0) << " ms, "
                        << hz << " Hz against a wanted " << wanted
                        << " (undamped would be 4.5)");
    CHECK(hz == Approx(wanted).epsilon(0.025));
}

TEST_CASE("Damping is what decides how long it rings", "[wind][bellows]")
{
    // The difference between a well-regulated organ and a lively one, and it is a
    // property of the instrument rather than a global taste.
    const auto overshootFor = [](float damping)
    {
        wind::Bellows b;
        b.configure(config(4.5f, damping));
        const std::vector<float> trace = run(b, kDemand, 2.0);
        const float target = equilibrium(kDemand);
        const float lowest = *std::min_element(trace.begin(), trace.end());
        return (target - lowest) / (kNominal - target);
    };

    const float lively = overshootFor(0.20f);
    const float normal = overshootFor(0.45f);
    const float tight  = overshootFor(0.80f);

    INFO("overshoot 0.20 -> " << lively << ", 0.45 -> " << normal
                              << ", 0.80 -> " << tight);
    CHECK(lively > normal);
    CHECK(normal > tight);
    CHECK(lively > 0.40f);  // exp(-pi*.2/sqrt(1-.04)) = 0.527
    CHECK(tight  < 0.06f);  // exp(-pi*.8/sqrt(1-.64)) = 0.015
}

TEST_CASE("Inertia changes the approach and never the destination",
          "[wind][bellows]")
{
    // Where the wind settles is the regulator's business -- nominal minus demand
    // over the feed conductance -- and the plate has no say in it. If it did, every
    // instrument would be quietly retuned by giving its reservoir a plate.
    for (const float damping : { 0.20f, 0.45f, 0.80f, 1.50f })
    {
        wind::Bellows plated;
        plated.configure(config(4.5f, damping));
        wind::Bellows flat;
        flat.configure(config(0.0f, damping));

        const float withPlate = run(plated, kDemand, 4.0).back();
        const float without   = run(flat,   kDemand, 4.0).back();

        INFO("damping " << damping << ": " << withPlate << " vs " << without);
        CHECK(withPlate == Approx(without).margin(0.02));
        CHECK(withPlate == Approx(equilibrium(kDemand)).margin(0.02));
    }
}

TEST_CASE("The same second of wind, whatever the host's buffer size",
          "[wind][bellows][regression]")
{
    // The reason this is solved in closed form rather than stepped. An explicit
    // integrator stable at 64 frames is not at 2048, and 2048 is a buffer size
    // hosts really use -- so the reservoir would blow up on exactly the machines
    // that most need a large one.
    //
    // With the demand constant across a block, which it is by construction, the
    // closed form is exact: coarse steps land on the same trajectory as fine ones,
    // not merely near it.
    const auto after = [](std::size_t frames)
    {
        wind::Bellows b;
        b.configure(config(4.5f, 0.45f));
        // One second, however many blocks that takes.
        return run(b, kDemand, 1.0, frames).back();
    };

    const float fine   = after(64);
    const float medium = after(512);
    const float coarse = after(2048);

    INFO("64 -> " << fine << ", 512 -> " << medium << ", 2048 -> " << coarse);
    CHECK(medium == Approx(fine).margin(0.05));
    CHECK(coarse == Approx(fine).margin(0.20)); // the coarse grid lands elsewhere in the cycle

    // Above all: nothing diverges. An unstable integrator shows up here first.
    for (const std::size_t frames : { 64u, 256u, 512u, 1024u, 2048u })
    {
        wind::Bellows b;
        b.configure(config(8.0f, 0.15f)); // fast and barely damped: the worst case
        const std::vector<float> trace = run(b, kDemand, 4.0, frames);
        for (const float p : trace)
        {
            INFO("frames " << frames << ", pressure " << p);
            REQUIRE(std::isfinite(p));
            REQUIRE(p > kNominal * 0.5f);
            REQUIRE(p < kNominal * 1.5f);
        }
    }
}

TEST_CASE("Letting a chord go bounces the wind the other way", "[wind][bellows]")
{
    // The recovery overshoots too, above nominal, which is the surplus the
    // brightness axis turns into a moment of extra edge as the hands come up.
    wind::Bellows b;
    b.configure(config(4.5f, 0.35f));

    (void) run(b, kDemand, 2.0);            // hold the chord until it settles
    const std::vector<float> release = run(b, 0.0f, 2.0); // and let it go

    const float highest = *std::max_element(release.begin(), release.end());
    INFO("recovery peaks at " << highest << " Pa against a nominal " << kNominal);
    CHECK(highest > kNominal);
    CHECK(highest < kNominal * 1.02f); // a bounce, not a bang
    CHECK(release.back() == Approx(kNominal).margin(0.05));
}

TEST_CASE("A plate damped to critical settles without ringing", "[wind][bellows]")
{
    // Damping at or past one is clamped just under it: at these frequencies an
    // overdamped reservoir and a critically damped one are the same thing to the
    // ear, and the closed form used here is the underdamped one.
    wind::Bellows b;
    b.configure(config(4.5f, 1.0f));

    const std::vector<float> trace  = run(b, kDemand, 2.0);
    const float              target = equilibrium(kDemand);

    for (const float p : trace)
    {
        INFO("pressure " << p);
        CHECK(std::isfinite(p));
        CHECK(p >= target - 0.05f); // no meaningful undershoot
        CHECK(p <= kNominal + 1.0e-3f);
    }
    CHECK(trace.back() == Approx(target).margin(0.05));
}

TEST_CASE("A reset takes the plate's motion with it", "[wind][bellows]")
{
    // A host reset means silence and stillness. A reservoir still moving from the
    // chord before it would carry that motion into the first block of the next
    // take, and an offline render would not be reproducible.
    wind::Bellows b;
    b.configure(config(4.5f, 0.30f));
    (void) run(b, kDemand, 0.15); // caught mid-bounce

    b.reset();
    CHECK(b.pressureEndPa() == Approx(kNominal));

    // With no demand at all it must simply stay there, which it cannot do if the
    // plate's velocity survived.
    const std::vector<float> still = run(b, 0.0f, 1.0);
    for (const float p : still)
    {
        INFO("pressure " << p);
        CHECK(p == Approx(kNominal).margin(1.0e-3));
    }
}
