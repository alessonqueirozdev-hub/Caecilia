/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace caecilia::wind
{

/// A typical nominal wind pressure (~75 mm water column) used as a sane default.
inline constexpr float kDefaultNominalPressurePa = 735.0f;

/// Shape of the periodic pressure modulation a tremulant injects into the wind.
enum class TremulantWaveform : std::uint8_t
{
    Sine,        ///< Smooth, gentle undulation.
    Triangle,    ///< Linear ramp up/down; a touch more prominent.
    AsymmetricSaw///< Fast attack / slow recovery, like a mechanical trem valve.
};

/**
 * @brief Physical configuration of a single bellows / reservoir.
 *
 * The reservoir is modelled as a first-order regulated pressure vessel: it
 * relaxes toward an equilibrium pressure that SAGS in proportion to the
 * instantaneous flow demand drawn from it, at a rate set by its regulator
 * stiffness and reservoir compliance. Stiffer feed → less sag and faster
 * recovery; larger compliance → slower, more audible "settling".
 */
struct BellowsConfig
{
    float nominalPressurePa = kDefaultNominalPressurePa; ///< Target (unloaded) pressure.
    float compliance        = 1.0f;  ///< Reservoir volume compliance (consistent units); larger = slower.
    float feedConductance   = 4.0f;  ///< Regulator/valve stiffness: flow per Pa of deficit. Larger = stiffer wind.
    float leak              = 0.0f;  ///< Passive leak conductance (reserved for phase-2 refinement).
};

/**
 * @brief Configuration of a windchest: a distribution node fed by a bellows.
 *
 * A windchest may share a bellows with sibling chests. It applies an additional
 * local "trunk" pressure drop proportional to its own demand, and may have a
 * tremulant assigned that modulates its pressure.
 */
struct WindchestConfig
{
    core::WindchestId id{};                    ///< Stable identity of this chest.
    std::uint16_t     bellowsIndex   = 0;      ///< Index of the feeding bellows in WindModelConfig::bellows.
    std::int32_t      tremulantIndex = -1;     ///< Index into WindModelConfig::tremulants, or -1 for none.
    float nominalPressurePa = kDefaultNominalPressurePa; ///< This chest's nominal pressure.
    float trunkConductance  = 50.0f;           ///< Local trunk stiffness: flow per Pa of local drop.
};

/**
 * @brief Configuration of a tremulant modelled as wind-pressure modulation.
 *
 * The tremulant modulates the DRIVER (chest pressure), not output gain, so
 * downstream it yields coupled AM + FM + timbral effects through each voice's
 * per-tonal-family WindResponseCurve.
 */
struct TremulantConfig
{
    float             rateHz         = 5.0f;                    ///< Modulation rate in Hz (typically 4–7).
    float             depthFraction  = 0.06f;                   ///< Peak deviation as a fraction of chest nominal.
    TremulantWaveform waveform       = TremulantWaveform::Sine; ///< Modulation shape.
    bool              enabledAtStart = false;                   ///< Whether it is drawn at load time.
};

/// Binds every pipe of a rank to the windchest that supplies it.
struct PipeChestBinding
{
    core::RankId      rank{};  ///< The rank whose pipes draw from @ref chest.
    core::WindchestId chest{}; ///< The supplying windchest.
};

/// A single per-windchest instantaneous flow-demand contribution.
struct WindDemand
{
    core::WindchestId chest{};      ///< Windchest being drawn from.
    float             flow = 0.0f;  ///< Volumetric flow demand (arbitrary consistent units).
};

/**
 * @brief Complete, off-thread description a WindModel compiles into runtime state.
 *
 * Built by the model/loader layer and handed to WindModel::configure() outside
 * the audio thread. All vectors are read once at configure() time; nothing here
 * is touched on the audio path.
 */
struct WindModelConfig
{
    std::vector<BellowsConfig>    bellows;      ///< Reservoirs, indexed by WindchestConfig::bellowsIndex.
    std::vector<WindchestConfig>  chests;       ///< Distribution nodes; chest i is addressed by its WindchestId.
    std::vector<TremulantConfig>  tremulants;   ///< Tremulants, indexed by WindchestConfig::tremulantIndex.
    std::vector<PipeChestBinding> pipeBindings; ///< Rank → chest routing; ranks absent here default to chest 0.
};

/**
 * @brief Convenience factory: a single bellows feeding a single windchest with
 *        one (initially disengaged) tremulant. Handy for early bring-up.
 * @param nominalPa Nominal chest/bellows pressure in pascals.
 * @param tremRateHz Tremulant rate in Hz.
 * @param tremDepth  Tremulant depth as a fraction of nominal.
 * @return A ready-to-use WindModelConfig with chest id {0}.
 */
[[nodiscard]] inline WindModelConfig makeSingleChestConfig(float nominalPa   = kDefaultNominalPressurePa,
                                                           float tremRateHz  = 5.0f,
                                                           float tremDepth   = 0.06f)
{
    WindModelConfig cfg;

    BellowsConfig bellows;
    bellows.nominalPressurePa = nominalPa;
    cfg.bellows.push_back(bellows);

    TremulantConfig trem;
    trem.rateHz        = tremRateHz;
    trem.depthFraction = tremDepth;
    cfg.tremulants.push_back(trem);

    WindchestConfig chest;
    chest.id                = core::WindchestId{0};
    chest.bellowsIndex      = 0;
    chest.tremulantIndex    = 0;
    chest.nominalPressurePa = nominalPa;
    cfg.chests.push_back(chest);

    return cfg;
}

} // namespace caecilia::wind
