/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>

namespace caecilia::core
{

/// Identifies a windchest / bellows reservoir within a loaded organ.
struct WindchestId
{
    std::uint16_t value = 0;
    friend constexpr bool operator==(WindchestId, WindchestId) noexcept = default;
};

/**
 * @brief Read-only query surface a voice uses to sample the wind supply while
 *        rendering.
 *
 * Wind is an audio-rate control signal, not an LFO. The wind model integrates a
 * reservoir ODE once per block and exposes the result here as an IMMUTABLE
 * snapshot: pressure sags under polyphony load and the tremulant modulates the
 * pressure itself (yielding coupled AM+FM+timbral effects downstream). Because
 * the snapshot is computed once then read-only, every voice reads it race-free.
 *
 * Real-time contract: every accessor is @c noexcept, allocation-free and
 * lock-free. Nominal pressures are in pascals (Pa).
 */
class IWindSupply
{
public:
    virtual ~IWindSupply() = default;

    /**
     * @brief Nominal (target) pressure of a windchest, ignoring sag/tremulant.
     * @param chest The windchest to query.
     * @return Nominal chest pressure in pascals.
     */
    [[nodiscard]] virtual float nominalPressurePa(WindchestId chest) const noexcept = 0;

    /**
     * @brief Instantaneous pressure of a windchest at a sample offset within the
     *        current block, including sag and tremulant modulation.
     * @param chest        The windchest to query.
     * @param frameInBlock Sample offset in [0, blockSize) for sample-accurate taps.
     * @return Actual chest pressure in pascals at that instant.
     */
    [[nodiscard]] virtual float pressureAt(WindchestId chest,
                                           std::size_t frameInBlock) const noexcept = 0;

    /**
     * @brief Normalised deviation from nominal at a sample offset:
     *        (pressureAt - nominal) / nominal.
     *
     * Convenience for per-partial modulation (pitch/level/brightness) that
     * scales the deviation through a per-tonal-family WindResponseCurve.
     */
    [[nodiscard]] virtual float pressureDeviation(WindchestId chest,
                                                  std::size_t frameInBlock) const noexcept = 0;

    /**
     * @brief Which windchest feeds a given pipe.
     * @param pipe The pipe whose supply is requested.
     */
    [[nodiscard]] virtual WindchestId chestForPipe(PipeId pipe) const noexcept = 0;

    /**
     * @brief Register a voice's instantaneous flow demand so the NEXT block's
     *        reservoir integration produces realistic sag under polyphony.
     * @param chest       The windchest being drawn from.
     * @param flowDemand  Volumetric flow demand in arbitrary consistent units.
     *
     * This is the only mutating call, and it merely accumulates into the
     * writer-owned demand tally; it takes no locks and does not allocate.
     */
    virtual void registerDemand(WindchestId chest, float flowDemand) noexcept = 0;
};

} // namespace caecilia::core
