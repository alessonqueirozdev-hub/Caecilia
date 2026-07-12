/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/AudioBlock.h"
#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/ITuning.h"
#include "ceciliae/core/IWindSupply.h"
#include "ceciliae/engine/DeadlineBudget.h"

#include <cstddef>
#include <cstdint>

namespace ceciliae::core::engine
{

/**
 * @brief The immutable per-block bundle the scheduler hands down while pulling
 *        voices.
 *
 * It carries the timing facts (@ref sampleRate, @ref numFrames, @ref framePos),
 * the read-only shared-model snapshots (@ref wind, @ref tuning), the CPU
 * governor (@ref budget) and the pre-zeroed per-windchest accumulation buses the
 * voices sum into. A RenderContext owns nothing: every pointer references
 * engine-owned storage that was allocated in @c prepare() and remains valid for
 * the whole block.
 *
 * Note: @c IVoice::renderAdd only receives an @c AudioBlock, so a voice reaches
 * the wind supply through the reference it was bound to at construction; the
 * context's @ref wind pointer is the SAME snapshot and is used here for demand
 * aggregation, pipe→chest routing and metering.
 */
struct RenderContext
{
    SampleRate    sampleRate       = 44100.0; ///< Host sample rate (Hz).
    std::size_t   numFrames        = 0;       ///< Frames to render this block.
    std::uint64_t framePos         = 0;       ///< Absolute frame index at block start.
    std::size_t   oversampleFactor = 1;       ///< Oversampling in the nonlinear island.
    double        nyquist          = 22050.0; ///< Nyquist for anti-alias decisions.

    const IWindSupply* wind   = nullptr; ///< Per-block wind snapshot (read-only).
    const ITuning*     tuning = nullptr; ///< Per-pipe frequency table (read-only).
    DeadlineBudget*    budget = nullptr; ///< CPU governor for stealing/demotion.

    AudioBlock* chestBuses    = nullptr; ///< [numChestBuses] pre-zeroed per-chest buses.
    std::size_t numChestBuses = 0;       ///< Number of windchest accumulation buses.

    /**
     * @brief The accumulation bus for a windchest, clamped to the valid range.
     * @param chest Windchest id whose bus is requested.
     * @return Pointer to the bus block, or the first bus if out of range /
     *         nullptr if there are no buses.
     */
    [[nodiscard]] AudioBlock* busFor(WindchestId chest) const noexcept
    {
        if (chestBuses == nullptr || numChestBuses == 0)
            return nullptr;
        const std::size_t i = chest.value < numChestBuses ? chest.value : 0;
        return &chestBuses[i];
    }
};

} // namespace ceciliae::core::engine
