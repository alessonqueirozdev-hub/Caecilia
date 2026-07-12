/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/wind/WindTypes.h"

#include <cstddef>

namespace caecilia::wind
{

/**
 * @brief A single reservoir / bellows: the pressure vessel that regulates wind.
 *
 * The bellows integrates a first-order regulated pressure ODE once per block.
 * Its equilibrium pressure sags below nominal in proportion to the summed flow
 * demand drawn from it during the block; it then relaxes toward that
 * equilibrium at a time constant of @c compliance / @c feedConductance. Over a
 * block the pressure is treated as a straight line between its start and end
 * values, which the feeding windchests sample sample-accurately.
 *
 * ## Real-time contract
 * - configure() is off-thread setup (no allocation of its own, but not marked
 *   RT-safe by convention).
 * - beginStep(), addDemand(), integrate() and every accessor are @c noexcept,
 *   allocation-free and lock-free — safe on the audio thread.
 */
class Bellows
{
public:
    Bellows() noexcept = default;

    /// Apply a physical configuration and snap the pressure to nominal.
    void configure(const BellowsConfig& config) noexcept;

    /// Reset dynamic state: pressure returns to nominal and demand clears.
    void reset() noexcept;

    /// Clear the accumulated demand at the start of a block's integration.
    void beginStep() noexcept;

    /// Accumulate a windchest's flow demand for this block. RT-safe.
    void addDemand(float flow) noexcept;

    /**
     * @brief Advance the reservoir by one block of @p dtSeconds seconds.
     *
     * Uses an exact exponential relaxation step (unconditionally stable for any
     * @p dtSeconds) toward the demand-dependent equilibrium pressure, recording
     * the block-start and block-end pressures for downstream sampling. RT-safe.
     */
    void integrate(double dtSeconds) noexcept;

    /// @return Pressure (Pa) at the start of the most recently integrated block.
    [[nodiscard]] float pressureStartPa() const noexcept { return startPa_; }

    /// @return Pressure (Pa) at the end of the most recently integrated block.
    [[nodiscard]] float pressureEndPa() const noexcept { return endPa_; }

    /// @return Configured nominal (unloaded) pressure in pascals.
    [[nodiscard]] float nominalPressurePa() const noexcept { return config_.nominalPressurePa; }

    /// @return Current sag (nominal − end-of-block pressure) in pascals.
    [[nodiscard]] float instantaneousSagPa() const noexcept
    {
        return config_.nominalPressurePa - endPa_;
    }

private:
    BellowsConfig config_{};
    float pressurePa_  = kDefaultNominalPressurePa; ///< Live integrator state.
    float startPa_     = kDefaultNominalPressurePa; ///< Pressure at block start.
    float endPa_       = kDefaultNominalPressurePa; ///< Pressure at block end.
    float demandAccum_ = 0.0f;                      ///< Summed demand for the current block.
};

} // namespace caecilia::wind
