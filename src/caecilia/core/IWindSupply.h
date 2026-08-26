// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
 * @todo Nothing drives a bound supply yet: AudioEngine::stepWind() is an empty
 *       stub and no voice calls registerDemand(), so a supply handed to the
 *       engine keeps reporting its initial pressures: no sag, no tremulant.
 *       The plugin binds no supply at all; only the dev tools do.
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

    /**
     * @brief Advance the supply by one block, consuming the registered demand.
     * @param numFrames Frames in the block about to be rendered.
     *
     * Called once per block by the engine, BEFORE any voice reads pressure, so
     * the snapshot a voice reads already carries this block's sag and tremulant
     * phase rather than the previous block's.
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    virtual void step(std::size_t numFrames) noexcept = 0;

    /**
     * @brief Engage or disengage the tremulant on one windchest.
     * @param chest The chest whose tremulant to move.
     * @param shouldBeEnabled Whether it should be running.
     *
     * Addressed by CHEST, not by tremulant index, because only the supply knows
     * which is which: an organ's chests and its tremulants are not in
     * correspondence — the Récit has one and the Grand-Orgue does not — so a caller
     * passing a chest id as an index would engage the wrong tremulant or none.
     * A chest without one is ignored rather than an error.
     *
     * On the audio thread, reached from an engine command. A tremulant is part of
     * the WIND, not of the voices — it modulates chest pressure, and every pipe on
     * that chest follows because they are breathing the same air.
     *
     * RT-safe.
     */
    virtual void setChestTremulantEnabled(WindchestId chest,
                                          bool        shouldBeEnabled) noexcept = 0;

    /**
     * @brief Set a chest tremulant's rate and depth while it is running.
     * @param chest The chest whose tremulant to shape.
     * @param rateHz Modulation rate in Hz.
     * @param depthFraction Peak pressure swing as a fraction of the chest nominal.
     *
     * Separate from engaging it because these are continuous host parameters and
     * that is a switch — and because a shape change must not disturb the
     * modulation's phase, where an engage legitimately starts one.
     *
     * RT-safe.
     */
    virtual void setChestTremulantShape(WindchestId chest, float rateHz,
                                        float depthFraction) noexcept = 0;
};

} // namespace caecilia::core
