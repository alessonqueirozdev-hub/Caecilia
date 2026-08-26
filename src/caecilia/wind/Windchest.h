// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/wind/WindTypes.h"

#include <cstddef>
#include <cstdint>

namespace caecilia::wind
{

/**
 * @brief A windchest: the distribution node that feeds a group of pipes.
 *
 * A windchest draws from a bellows (possibly shared with sibling chests) and
 * imposes an additional local "trunk" pressure drop proportional to its own
 * instantaneous demand. Over a block its sagged pressure is a straight line
 * between the block-start and block-end values; the WindModel adds the assigned
 * tremulant's per-frame delta on top when a caller samples pressureAt().
 *
 * ## Real-time contract
 * registerDemand(), updateBlock(), sagPressureAt() and the accessors are all
 * @c noexcept and allocation-free. configure() is off-thread setup.
 */
class Windchest
{
public:
    Windchest() noexcept = default;

    /// Apply configuration and reset dynamic state to nominal.
    void configure(const WindchestConfig& config) noexcept;

    /// Reset the sag trajectory to nominal and clear pending demand.
    void reset() noexcept;

    /// Accumulate a voice's instantaneous flow demand for the current block. RT-safe.
    void registerDemand(float flow) noexcept;

    /// @return Total flow demand accumulated since the last resetDemand().
    [[nodiscard]] float pendingDemand() const noexcept { return demandAccum_; }

    /// Clear the demand accumulator for the next block. RT-safe.
    void resetDemand() noexcept { demandAccum_ = 0.0f; }

    /**
     * @brief Latch this block's sag trajectory from the feeding bellows.
     * @param bellowsStartPa Bellows pressure at block start.
     * @param bellowsEndPa   Bellows pressure at block end.
     * @param numFrames      Frames in the block (used to interpolate).
     *
     * Applies the local trunk drop from this chest's pending demand and stores
     * the resulting start/end endpoints. RT-safe.
     */
    void updateBlock(float bellowsStartPa, float bellowsEndPa, std::size_t numFrames) noexcept;

    /**
     * @brief Sagged chest pressure at a sample offset, EXCLUDING tremulant.
     * @param frameInBlock Offset in [0, numFrames) from the last updateBlock().
     * @return Interpolated pressure in pascals. RT-safe.
     */
    [[nodiscard]] float sagPressureAt(std::size_t frameInBlock) const noexcept;

    /// @return Configured nominal pressure (Pa).
    [[nodiscard]] float nominalPressurePa() const noexcept { return config_.nominalPressurePa; }

    /// @return Index of the feeding bellows.
    [[nodiscard]] std::uint16_t bellowsIndex() const noexcept { return config_.bellowsIndex; }

    /// @return Index of the assigned tremulant, or -1 if none.
    [[nodiscard]] std::int32_t tremulantIndex() const noexcept { return config_.tremulantIndex; }

    /// @return This chest's stable identity.
    [[nodiscard]] core::WindchestId id() const noexcept { return config_.id; }

private:
    WindchestConfig config_{};
    float       startPa_ = kDefaultNominalPressurePa; ///< Sagged pressure at block start.
    float       endPa_   = kDefaultNominalPressurePa; ///< Sagged pressure at block end.
    std::size_t frames_  = 1;                          ///< Frames spanned by the current trajectory.
    float       demandAccum_ = 0.0f;                   ///< Demand accumulated for the current block.
};

} // namespace caecilia::wind
