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
 * room every voice renders in full; once the active set costs more than the
 * budget allows, the QUIETEST voices are released into their own tails -- so a
 * worst-case tutti thins subtly instead of xrunning or gating.
 *
 * Which voices those are is decided once per block by @ref planBlock and applied
 * unchanged across every slice of it; see that function for why the choice is not
 * simply "whichever voice the loop reached when the budget ran out", which is
 * what it used to be.
 *
 * Note what shedding does and does not buy. A released voice still renders its
 * tail, so the block it was shed in costs the same as it would have; the saving
 * arrives over the following release time, as the tails end and free their slots.
 * That makes this a polyphony governor with a slow-acting remedy rather than a
 * per-block abort -- deliberately, because the alternative is to stop rendering a
 * sounding pipe mid-note, and a click is far more audible than one late buffer.
 *
 * @todo Tier demotion is the rung between rendering in full and giving up, and
 *       it is also the only FAST remedy: it would make the offending block itself
 *       cheaper instead of the next thirty. It does not exist.
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

    /// What @ref planBlock worked out about the block about to be rendered.
    struct BlockPlan
    {
        /// Linear level below which voices are given up; 0 = the set fits.
        float         shedBelowLevel = 0.0f;
        /// What the whole active set costs, in full, whether or not it fits.
        float         demandUnits    = 0.0f;
        /// How many voices are below @ref shedBelowLevel, for the meters.
        std::uint32_t shedCount      = 0;
    };

    /**
     * @brief Decide, once for the whole block, what cannot be afforded.
     * @param pool        The active set, exactly as @ref renderActive will see it.
     * @param budgetUnits The block's allowance (DeadlineBudget::total()).
     * @return The plan; @c shedBelowLevel is 0 when everything fits.
     *
     * The old policy was no policy: the budget was spent in iteration order and
     * whatever the loop reached when it ran out was released. Iteration order is
     * pool slot order, so under a tutti the pipe that disappeared could as easily
     * be the 16' pedal holding the passage up as the top of a mixture nobody would
     * miss -- and the StealPolicy::Quietest the engine had been configured with
     * was consulted only on the pool-full path, never here.
     *
     * Selecting the quietest voices exactly would mean sorting the active set
     * every block. Instead the levels are histogrammed -- one @c frexp per voice,
     * one linear pass, no allocation -- into quarter-octave buckets, and the
     * buckets are walked from the loudest down until the budget is used up. That
     * places the threshold within 1.5 dB, at which point the voices inside a
     * bucket are interchangeable by any musical measure.
     *
     * The bucket the budget runs out IN is dropped with the rest -- except when
     * nothing louder fit either, which is the tutti: every voice in one bucket,
     * and dropping it would silence the instrument outright instead of thinning
     * it. That one case keeps its bucket and lets @ref renderBatch's budget check
     * trim inside it, where the order is arbitrary but the voices are equal.
     *
     * RT-safe: noexcept, allocation-free, O(active voices).
     */
    [[nodiscard]] BlockPlan planBlock(const VoicePoolView& pool,
                                      float                budgetUnits) noexcept;

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

    /// Level buckets for @ref planBlock: @ref kSubBuckets per octave across
    /// @ref kOctaves of them, so 1.5 dB each over a 156 dB range. Everything
    /// below that range shares bucket zero, which is right -- 156 dB under the
    /// loudest a voice can be is not a pipe anyone is going to miss.
    static constexpr int         kOctaves      = 26;
    static constexpr int         kSubBuckets   = 4;
    static constexpr int         kQuietestExp  = -24;
    static constexpr std::size_t kLevelBuckets =
        static_cast<std::size_t>(kOctaves * kSubBuckets);

    StealPolicy stealPolicy_ = StealPolicy::Quietest;
    std::size_t maxVoices_   = 0;
    std::size_t maxFrames_   = 0;
};

} // namespace caecilia::core::engine
