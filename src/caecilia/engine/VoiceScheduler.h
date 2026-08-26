// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/engine/RenderContext.h"
#include "caecilia/engine/StealPolicy.h"
#include "caecilia/engine/VoiceBatchView.h"

#include <cstddef>

namespace caecilia::core::engine
{

/**
 * @brief Pulls the active voices for a block and renders them into the
 *        per-windchest accumulation buses under a CPU deadline.
 *
 * The scheduler is deliberately a plain, separately-compiled class that consumes
 * only non-template views (@c VoicePoolView / @c VoiceBatchView) and the
 * @c RenderContext, so it is independent of the pool's compile-time capacity.
 *
 * Its degradation policy is the product promise: while the @c DeadlineBudget has
 * room every voice renders in full; once a voice no longer fits, the scheduler
 * releases it into its own release tail instead of skipping it for a block — so
 * a worst-case tutti thins subtly instead of xrunning or gating.
 *
 * @todo Tier demotion and quietest-first stealing are the two gentler rungs
 *       intended above that, and neither exists: releasing is currently the
 *       only response to budget pressure. See VoiceScheduler::renderBatch().
 *
 * ## Real-time contract
 * - @ref prepare is off-thread setup (no per-block allocation happens here yet,
 *   but it is the seam for any future scratch buffers).
 * - @ref renderActive is @c noexcept, allocation-free and lock-free.
 */
class VoiceScheduler
{
public:
    VoiceScheduler() noexcept = default;

    /**
     * @brief One-time configuration hook (off the audio thread).
     * @param maxVoices      Upper bound on simultaneously active voices.
     * @param maxBlockFrames Largest block that will be rendered.
     */
    void prepare(std::size_t maxVoices, std::size_t maxBlockFrames) noexcept;

    /// Select how victims are chosen when the budget is exhausted.
    void setStealPolicy(StealPolicy policy) noexcept { stealPolicy_ = policy; }

    /// @return The active steal policy.
    [[nodiscard]] StealPolicy stealPolicy() const noexcept { return stealPolicy_; }

    /**
     * @brief Render every active voice into its windchest bus, respecting the
     *        deadline budget in @p ctx.
     * @param pool Flattened view of the active voices (kind-grouped).
     * @param ctx  Per-block context: buses, wind, tuning, budget, timing.
     * @return Number of voices that actually rendered this block (the rest were
     *         shed under budget pressure).
     *
     * RT-safe. Each voice ADDS into its bus (never overwrites), so many voices
     * on a chest sum correctly.
     */
    std::size_t renderActive(const VoicePoolView& pool, RenderContext& ctx) noexcept;

private:
    /// Render one kind's contiguous batch; returns how many voices rendered.
    std::size_t renderBatch(const VoiceBatchView& batch, RenderContext& ctx) noexcept;

    StealPolicy stealPolicy_ = StealPolicy::Quietest;
    std::size_t maxVoices_   = 0;
    std::size_t maxFrames_   = 0;
};

} // namespace caecilia::core::engine
