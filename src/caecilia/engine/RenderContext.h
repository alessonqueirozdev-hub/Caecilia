// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"

#include <array>
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/engine/DeadlineBudget.h"

#include <cstddef>
#include <cstdint>

namespace caecilia::core::engine
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
 * context's @ref wind pointer is the SAME snapshot and is used here for
 * pipe→chest routing (VoiceScheduler::renderBatch) and metering
 * (AudioEngine::captureMeters). Demand aggregation is not wired up: nothing
 * calls IWindSupply::registerDemand(), so the reservoir sees no load.
 */
struct RenderContext
{
    SampleRate    sampleRate       = 44100.0; ///< Host sample rate (Hz).
    std::size_t   numFrames        = 0;       ///< Frames to render this block.
    std::uint64_t framePos         = 0;       ///< Absolute frame index at block start.
    std::size_t   oversampleFactor = 1;       ///< Oversampling in the nonlinear island.
    double        nyquist          = 22050.0; ///< Nyquist for anti-alias decisions.

    /// This slice's share of the whole block, in [0, 1].
    ///
    /// The CPU budget is a per-BLOCK allowance, but a block is rendered in as
    /// many slices as it has events in it. A voice's cost estimate describes a
    /// whole block, so charging it in full for each slice would spend the budget
    /// as many times as the block was cut -- a busy MIDI bar would shed voices on
    /// a load that fits comfortably. Scaling by this makes the sum across a
    /// block's slices come out exactly equal to the unsliced cost.
    float         costScale        = 1.0f;

    /// Swell-shoe gain for each division, as a per-sample RAMP rather than a
    /// value: `start` at the first frame of the block, moving by `inc` each frame.
    ///
    /// A ramp rather than a level because a shoe is dragged, not stepped, and a
    /// per-block gain jump on a sustained chord is a zipper. Sixty-four divisions
    /// of two floats is half a cache line; there is nothing to save by being
    /// cleverer.
    ///
    /// The gain is FLAT across the spectrum, and that is a known simplification,
    /// not an oversight: real shutters attenuate treble far more than bass, so
    /// this reads as a volume pedal rather than as a box. Do NOT fix that with a
    /// filter per voice — that is a biquad on the hot path, per note, forever. The
    /// shutter low-pass belongs on a per-division BUS, which is work for after the
    /// one-voice-per-rank migration gives us those buses.
    struct ExpressionRamp
    {
        float start = 1.0f;
        float inc   = 0.0f;
    };
    static constexpr std::size_t kMaxExpressionDivisions = 64;
    std::array<ExpressionRamp, kMaxExpressionDivisions> expression{};

    /// The shoe ramp for a division, clamped. Divisions past the array read as
    /// fully open, which is what an unenclosed division is anyway.
    [[nodiscard]] ExpressionRamp expressionFor(DivisionId division) const noexcept
    {
        return division.value < kMaxExpressionDivisions ? expression[division.value]
                                                        : ExpressionRamp{};
    }

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

} // namespace caecilia::core::engine
