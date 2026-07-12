/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

//
// Wind-model tests: the realism moat. Wind is an audio-rate control signal, not
// an LFO. Cover pressure SAG under sustained polyphony demand (steady-state
// magnitude, monotonic in demand, recovery when the load is removed), the
// per-block immutable snapshot's baseline behaviour, and the tremulant modelled
// as wind-pressure modulation (a within-supply oscillation, not an output gain).
//

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/wind/WindModel.h"
#include "caecilia/wind/WindTypes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using Catch::Approx;
namespace core = caecilia::core;
namespace wind = caecilia::wind;

namespace
{
constexpr core::SampleRate kSampleRate = 48000.0;
constexpr std::size_t      kBlock      = 512;
constexpr float            kNominalPa  = wind::kDefaultNominalPressurePa; // 735 Pa
constexpr core::WindchestId kChest{0};

// Defaults from BellowsConfig / WindchestConfig used by makeSingleChestConfig.
constexpr float kFeedConductance  = 4.0f;
constexpr float kTrunkConductance = 50.0f;

/// Build a single-bellows / single-chest model ready to step.
wind::WindModel makeModel()
{
    wind::WindModel model;
    model.prepare(kSampleRate, kBlock);
    model.configure(wind::makeSingleChestConfig(kNominalPa));
    return model;
}

/// Advance @p blocks blocks, re-registering @p demand before each so the load is
/// sustained (step() consumes and clears the demand each block).
void run(wind::WindModel& model, float demand, int blocks)
{
    for (int i = 0; i < blocks; ++i)
    {
        if (demand > 0.0f)
            model.registerDemand(kChest, demand);
        model.step(kBlock);
    }
}
} // namespace

TEST_CASE("Wind snapshot sits at nominal with no demand", "[wind][baseline]")
{
    wind::WindModel model = makeModel();
    CHECK(model.numChests() == 1);
    CHECK(model.numTremulants() == 1);
    CHECK(model.nominalPressurePa(kChest) == Approx(kNominalPa));

    run(model, 0.0f, 200);

    CHECK(model.pressureAt(kChest, 0) == Approx(kNominalPa).margin(0.1));
    CHECK(model.pressureDeviation(kChest, 0) == Approx(0.0).margin(1e-3));
}

TEST_CASE("Pressure sags toward the demand-dependent equilibrium", "[wind][sag]")
{
    wind::WindModel model = makeModel();

    constexpr float demand = 40.0f;
    run(model, demand, 600); // fully converged for the default time constant

    // Steady chest pressure = nominal - bellows sag (D/feed) - trunk drop (D/trunk).
    const float expected = kNominalPa - demand / kFeedConductance - demand / kTrunkConductance;

    CHECK(model.pressureAt(kChest, 0) == Approx(expected).margin(0.5));
    CHECK(model.pressureAt(kChest, 0) < kNominalPa - 5.0f); // an audible sag
    CHECK(model.pressureDeviation(kChest, 0) < -0.005f);    // below nominal
}

TEST_CASE("Sag increases monotonically with demand", "[wind][sag]")
{
    wind::WindModel light = makeModel();
    wind::WindModel heavy = makeModel();

    run(light, 40.0f, 600);
    run(heavy, 120.0f, 600);

    const float pLight = light.pressureAt(kChest, 0);
    const float pHeavy = heavy.pressureAt(kChest, 0);

    CHECK(pHeavy < pLight);              // more polyphony -> more sag
    CHECK(pHeavy < kNominalPa - 20.0f);  // and a large tutti sags substantially
}

TEST_CASE("Wind recovers to nominal once the load is removed", "[wind][recovery]")
{
    wind::WindModel model = makeModel();

    run(model, 40.0f, 600);
    const float loaded = model.pressureAt(kChest, 0);
    REQUIRE(loaded < kNominalPa - 5.0f);

    run(model, 0.0f, 600); // release the keys
    const float recovered = model.pressureAt(kChest, 0);

    CHECK(recovered > loaded + 5.0f);
    CHECK(recovered == Approx(kNominalPa).margin(0.5));
}

TEST_CASE("Unbound pipes resolve to the first windchest", "[wind][routing]")
{
    wind::WindModel model = makeModel();
    // No pipe bindings were configured, so any pipe defaults to chest 0.
    CHECK(model.chestForPipe(core::PipeId{7, 60}) == kChest);
}

TEST_CASE("Tremulant modulates the wind pressure itself", "[wind][tremulant]")
{
    wind::WindModel model = makeModel();

    // Disabled by default: the per-block snapshot is flat at nominal.
    run(model, 0.0f, 5);
    CHECK(model.pressureAt(kChest, 0) == Approx(kNominalPa).margin(0.1));

    // Engage the tremulant (index 0) and let its click-free depth ramp settle.
    model.setTremulantEnabled(0, true);

    float minPa = kNominalPa;
    float maxPa = kNominalPa;
    for (int b = 0; b < 300; ++b)
    {
        model.step(kBlock);
        if (b >= 60) // skip the depth ramp-in
        {
            const float p = model.pressureAt(kChest, 0);
            minPa = std::min(minPa, p);
            maxPa = std::max(maxPa, p);
        }
    }

    // The pressure swings through a substantial fraction of the configured depth
    // (0.06 * 735 ~= 44 Pa peak), on BOTH sides of nominal — an AM+FM+timbral
    // driver modulation, not an output-gain wobble.
    const float depthPa = 0.06f * kNominalPa;
    CHECK((maxPa - minPa) > depthPa);      // peak-to-peak exceeds one-sided depth
    CHECK(maxPa > kNominalPa + 0.4f * depthPa);
    CHECK(minPa < kNominalPa - 0.4f * depthPa);
}
