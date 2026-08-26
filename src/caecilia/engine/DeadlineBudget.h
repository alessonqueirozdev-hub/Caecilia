// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

namespace caecilia::core::engine
{

/**
 * @brief The per-block CPU cost governor that turns "we are out of time" into a
 *        graceful, audible-only-as-thinning degradation instead of an xrun.
 *
 * Each block the engine @ref reset()s the budget to the number of abstract cost
 * units it can afford (AudioEngine::makeContext(); AudioEngine::prepare()
 * defaults the total to one unit per voice slot unless the host tightens it via
 * AudioEngine::setBlockBudget()). Before rendering each voice the scheduler
 * calls @ref tryConsume with that voice's @c cpuCostEstimate(); a voice that no
 * longer fits is released into its own tail rather than skipped. The cost unit
 * is intentionally abstract (a voice's estimated relative weight), not
 * wall-clock time, so the policy is deterministic and host-independent.
 *
 * @todo Shedding partials and demoting tiers are not implemented; releasing the
 *       voice is the only response to a spent budget. See
 *       VoiceScheduler::renderBatch().
 *
 * Real-time contract: every method is @c noexcept, allocation-free and lock-free.
 */
class DeadlineBudget
{
public:
    DeadlineBudget() noexcept = default;

    /**
     * @brief Begin a new block with @p totalUnits of affordable cost.
     * @param totalUnits Non-negative budget in abstract cost units.
     */
    void reset(float totalUnits) noexcept
    {
        total_     = totalUnits > 0.0f ? totalUnits : 0.0f;
        remaining_ = total_;
    }

    /**
     * @brief Attempt to spend @p cost units of budget.
     * @return true if the cost fit (and was deducted); false if it would
     *         overrun (nothing is deducted — the caller should shed/steal).
     */
    [[nodiscard]] bool tryConsume(float cost) noexcept
    {
        const float c = cost > 0.0f ? cost : 0.0f;
        if (c > remaining_)
            return false;
        remaining_ -= c;
        return true;
    }

    /// @return Remaining affordable cost this block.
    [[nodiscard]] float remaining() const noexcept { return remaining_; }

    /// @return The full budget the block started with.
    [[nodiscard]] float total() const noexcept { return total_; }

    /// @return true once the budget is spent and shedding should begin.
    [[nodiscard]] bool exhausted() const noexcept { return remaining_ <= 0.0f; }

    /**
     * @return Fraction of the budget already consumed in [0, 1]. Useful as the
     *         pressure signal that drives progressive tier demotion before the
     *         hard wall is hit.
     */
    [[nodiscard]] float pressure() const noexcept
    {
        return total_ > 0.0f ? (total_ - remaining_) / total_ : 1.0f;
    }

private:
    float total_     = 0.0f;
    float remaining_ = 0.0f;
};

} // namespace caecilia::core::engine
