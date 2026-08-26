// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The CPU governor's control law.
//
// DeadlineBudget was written to shed voices when a block runs out of time, and
// on the measured cost of an AdditiveVoice it could not fire: the default budget
// was one unit per voice slot (1024) and a completely full pool of 1024 voices
// demands about 840. Nothing ever tightened it, so the scheduler's promise that a
// worst-case tutti thins rather than xruns was decorative.
//
// The law is pure arithmetic and takes the elapsed time as an argument, so all of
// this is deterministic -- no stopwatch, no sleeping, no flakiness. The closed
// loop is exercised at the bottom against a model of a machine.
//

#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/CpuGovernor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using Catch::Approx;
namespace engine = caecilia::core::engine;

namespace
{
/// A governor past its warm-up, so a test can measure the law rather than the
/// warm-up. Small warm-up and ceiling keep the numbers readable.
engine::CpuGovernor warmGovernor(engine::CpuGovernor::Config config = {})
{
    config.warmupBlocks = 4;
    config.ceilingUnits = 100.0f;
    config.floorUnits   = 10.0f;

    engine::CpuGovernor g;
    g.configure(config);
    g.reset();
    g.setEnabled(true);

    // Warm-up blocks, all comfortably idle.
    for (int i = 0; i < 4; ++i)
        (void) g.observe(0.001, 0.010, 1.0f);
    return g;
}

/// One block at a given load, spending a given number of units.
float step(engine::CpuGovernor& g, float load, float unitsSpent = 50.0f)
{
    constexpr double kDeadline = 0.010; // 10 ms, ~480 frames at 48 kHz
    return g.observe(kDeadline * static_cast<double>(load), kDeadline, unitsSpent);
}
} // namespace

TEST_CASE("The warm-up refuses to govern the first blocks", "[engine][cpu]")
{
    // Cold caches, lazy page faults and the first touch of every partial array all
    // land on the first callbacks. A governor that believed them would shed a
    // tutti the machine holds easily once warm.
    engine::CpuGovernor::Config cfg;
    cfg.warmupBlocks = 8;
    cfg.ceilingUnits = 100.0f;

    engine::CpuGovernor g;
    g.configure(cfg);
    g.reset();
    g.setEnabled(true);

    for (int i = 0; i < 8; ++i)
    {
        // A brutal 4x overrun, every single warm-up block.
        CHECK(step(g, 4.0f) == Approx(100.0f));
    }

    // The ninth is believed.
    CHECK(step(g, 4.0f) < 100.0f);
}

TEST_CASE("Sustained pressure lowers the budget and it settles", "[engine][cpu]")
{
    engine::CpuGovernor g = warmGovernor();
    REQUIRE(g.budgetUnits() == Approx(100.0f));

    // Just over target, but not over the deadline: the gentle slope.
    const float afterOne = step(g, 0.75f);
    CHECK(afterOne < 100.0f);
    CHECK(afterOne > 60.0f); // one attack step of 25%, not a collapse

    for (int i = 0; i < 200; ++i)
        (void) step(g, 0.75f);

    // It stops at the floor rather than running to zero.
    CHECK(g.budgetUnits() == Approx(10.0f));
}

TEST_CASE("A missed deadline is answered in one block, not five", "[engine][cpu]")
{
    // Pure multiplicative decrease needs several blocks to walk down from the
    // ceiling. Every one of those blocks is an xrun once the deadline is ALREADY
    // being missed, which is exactly when the loop has to be quick.
    const auto blocksToReach = [](float load, float target)
    {
        engine::CpuGovernor g = warmGovernor();
        int blocks = 0;
        while (g.budgetUnits() > target && blocks < 100)
        {
            (void) step(g, load, /*unitsSpent*/ 50.0f);
            ++blocks;
        }
        return blocks;
    };

    // 50 units took 1.5 block periods, so 0.6 of a period affords 50*0.6/1.5 = 20.
    CHECK(blocksToReach(1.5f, 25.0f) == 1);

    // Under the deadline, the same destination is approached gradually.
    CHECK(blocksToReach(0.7f, 25.0f) > 3);
}

TEST_CASE("The fast path never cuts below the floor", "[engine][cpu]")
{
    // The jump is `unitsSpent * target / load` and deliberately ignores the fixed
    // per-block cost -- buses, wind, the master chain -- so on a machine that
    // cannot keep up even while nearly idle it computes almost nothing. The floor
    // is the statement that below it the answer is not to shed further.
    engine::CpuGovernor g = warmGovernor();

    for (int i = 0; i < 50; ++i)
        (void) step(g, 3.0f, /*unitsSpent*/ 1.0f);

    CHECK(g.budgetUnits() == Approx(10.0f));
    CHECK(g.budgetUnits() > 0.0f);
}

TEST_CASE("An idle machine wins the budget back, slowly", "[engine][cpu]")
{
    engine::CpuGovernor g = warmGovernor();

    for (int i = 0; i < 100; ++i)
        (void) step(g, 2.0f, 50.0f);
    REQUIRE(g.budgetUnits() == Approx(10.0f));

    // Recovery is deliberately much slower than the cut: one heavy chord must not
    // turn into an oscillation between shedding and overloading.
    const float afterOne = step(g, 0.05f, 1.0f);
    CHECK(afterOne > 10.0f);
    CHECK(afterOne < 11.0f);

    for (int i = 0; i < 400; ++i)
        (void) step(g, 0.05f, 1.0f);
    CHECK(g.budgetUnits() == Approx(100.0f));
}

TEST_CASE("The dead band gives the loop somewhere to stand", "[engine][cpu]")
{
    // Between `target * deadBand` and `target` the budget does not move. Without
    // it the loop alternates cut and creep every block around the target and the
    // sounding voice count flickers.
    engine::CpuGovernor::Config cfg;
    cfg.warmupBlocks = 4;
    cfg.ceilingUnits = 100.0f;
    cfg.floorUnits   = 10.0f;
    cfg.targetLoad   = 0.60f;
    cfg.deadBand     = 0.85f; // dead band is [0.51, 0.60]

    engine::CpuGovernor g = warmGovernor(cfg);
    for (int i = 0; i < 50; ++i)
        (void) step(g, 0.90f);
    const float settled = g.budgetUnits();

    for (int i = 0; i < 50; ++i)
        (void) step(g, 0.55f); // inside the band
    CHECK(g.budgetUnits() == Approx(settled));
}

TEST_CASE("A disabled governor meters but does not act", "[engine][cpu]")
{
    // What an offline bounce needs. A host rendering faster or slower than real
    // time is not a deadline, and a mix thinned by a stopwatch measuring the wrong
    // thing would not match what the organist heard live.
    engine::CpuGovernor g = warmGovernor();
    g.setEnabled(false);

    for (int i = 0; i < 50; ++i)
        (void) step(g, 6.0f);

    CHECK(g.budgetUnits() == Approx(100.0f));
    CHECK(g.load() > 1.0f);          // the meter still moved
    CHECK(g.pressureBlocks() == 50); // and the pressure was still counted
}

TEST_CASE("A clock that jumped is dropped, not believed", "[engine][cpu]")
{
    // Suspend the machine, resume it, and the first delta is however long the lid
    // was shut. Shedding the entire organ on wake is not a graceful degradation.
    engine::CpuGovernor g = warmGovernor();

    (void) g.observe(3600.0, 0.010, 50.0f);           // an hour asleep
    CHECK(g.budgetUnits() == Approx(100.0f));

    (void) g.observe(-0.5, 0.010, 50.0f);             // a re-based counter
    CHECK(g.budgetUnits() == Approx(100.0f));

    (void) g.observe(std::numeric_limits<double>::quiet_NaN(), 0.010, 50.0f);
    CHECK(g.budgetUnits() == Approx(100.0f));
    CHECK(!std::isnan(g.load()));

    (void) g.observe(0.001, 0.0, 50.0f);              // no deadline to compare to
    CHECK(g.budgetUnits() == Approx(100.0f));
}

TEST_CASE("reset re-arms the warm-up and returns to the ceiling", "[engine][cpu]")
{
    // A new sample rate or block size is a new machine as far as this loop is
    // concerned: a verdict reached about a 10 ms deadline says nothing about a
    // 2.6 ms one.
    engine::CpuGovernor g = warmGovernor();
    for (int i = 0; i < 100; ++i)
        (void) step(g, 2.0f);
    REQUIRE(g.budgetUnits() < 100.0f);

    g.reset();
    CHECK(g.budgetUnits() == Approx(100.0f));
    CHECK(g.load() == Approx(0.0f));
    CHECK(g.pressureBlocks() == 0);

    for (int i = 0; i < 4; ++i)
        CHECK(step(g, 4.0f) == Approx(100.0f));
}

TEST_CASE("The peak meter shows the spike an average hides", "[engine][cpu]")
{
    // An xrun is ONE block over 1.0. A mean over a third of a second cannot show
    // one, which is why the console gets both numbers.
    engine::CpuGovernor g = warmGovernor();
    g.setEnabled(false); // measure the meters, not the loop

    for (int i = 0; i < 50; ++i)
        (void) step(g, 0.10f);
    (void) step(g, 1.40f);

    CHECK(g.load() < 0.30f);      // the average barely noticed
    CHECK(g.peakLoad() > 1.30f);  // the peak did
}

TEST_CASE("An engine governs only when asked to", "[engine][cpu]")
{
    // Off by default, because a bare engine is what every other test runs and a
    // wall clock in the middle of one is not a thing to make them depend on. A
    // pinned budget and a measured one are alternatives, so setting either must
    // put the other away.
    engine::AudioEngine eng;
    eng.prepare(48000.0, 256, 2, 1);
    CHECK(!eng.cpuGovernor().isEnabled());

    engine::CpuGovernor::Config cfg;
    cfg.ceilingUnits = 512.0f;
    eng.enableCpuGovernor(cfg);
    CHECK(eng.cpuGovernor().isEnabled());
    CHECK(eng.cpuGovernor().budgetUnits() == Approx(512.0f));

    eng.setBlockBudget(4.0f);
    CHECK(!eng.cpuGovernor().isEnabled());

    // And re-preparing at a new block size re-arms the warm-up rather than
    // carrying a verdict about the old deadline into the new one.
    eng.enableCpuGovernor(cfg);
    eng.prepare(96000.0, 64, 2, 1);
    CHECK(eng.cpuGovernor().isEnabled());
    CHECK(eng.cpuGovernor().budgetUnits() == Approx(512.0f));
}

TEST_CASE("The loop converges on a modelled machine", "[engine][cpu]")
{
    // A machine, not a sequence of numbers: rendering costs a fixed per-block
    // overhead plus a rate per unit, and what it renders is whatever the budget
    // allowed of a constant demand. This is the whole loop closed, and it is the
    // test that would catch a law that is stable against scripted loads and
    // oscillates against real feedback.
    //
    // The demand -- 400 units, roughly a 20-rank tutti under a five-note chord --
    // is far more than this modelled machine can afford, so the governor has to
    // find the level that fits.
    constexpr double kDeadline = 0.010;
    constexpr double kFixed    = 0.0008;  // 8% of the period before a note sounds
    constexpr double kRate     = 0.00006; // 60 us per unit: ~120 units affordable
    constexpr float  kDemand   = 400.0f;

    engine::CpuGovernor::Config cfg;
    cfg.warmupBlocks = 4;
    cfg.ceilingUnits = 1024.0f;
    cfg.floorUnits   = 16.0f;
    cfg.targetLoad   = 0.60f;

    engine::CpuGovernor g;
    g.configure(cfg);
    g.reset();
    g.setEnabled(true);

    float budget   = cfg.ceilingUnits;
    float worstFit = 0.0f;

    for (int block = 0; block < 600; ++block)
    {
        const float  spent   = budget < kDemand ? budget : kDemand;
        const double elapsed = kFixed + kRate * static_cast<double>(spent);

        budget = g.observe(elapsed, kDeadline, spent);

        // After the loop has had time to settle, no block may miss the deadline.
        if (block > 100)
            worstFit = std::max(worstFit, static_cast<float>(elapsed / kDeadline));
    }

    INFO("settled budget " << g.budgetUnits() << " units, load " << g.load()
                           << ", worst " << worstFit);

    // It found a real working point rather than the floor or the ceiling.
    CHECK(g.budgetUnits() > 50.0f);
    CHECK(g.budgetUnits() < 200.0f);

    // It sits near the target, and never overruns once settled.
    CHECK(g.load() > 0.40f);
    CHECK(g.load() < 0.65f);
    CHECK(worstFit < 1.0f);
}
