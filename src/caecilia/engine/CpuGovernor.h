// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstdint>

namespace caecilia::core::engine
{

/**
 * @brief The closed loop that decides how large @ref DeadlineBudget may be, from
 *        how long the last blocks actually took.
 *
 * @c DeadlineBudget spends an allowance; this is what sets the allowance. The two
 * were built as one idea and only half of it existed: the budget defaulted to one
 * unit per voice slot (AudioEngine::prepare) and nothing ever tightened it, so on
 * the measured cost of an @c AdditiveVoice a full pool of 1024 voices demands
 * about 840 units against a budget of 1024. The governor could not fire even in
 * principle, and the scheduler's written promise -- a worst-case tutti thins
 * subtly rather than xrunning -- was decorative.
 *
 * ## Why a loop and not a number
 *
 * A cost unit is deliberately abstract (a voice's relative weight), so how many
 * of them fit in one block is a property of the MACHINE, not of the instrument. A
 * constant would have to be wrong on all but one computer. Measuring instead
 * makes the budget mean the same thing everywhere: "as much as this machine can
 * render in @ref Config::targetLoad of a block period".
 *
 * ## The control law
 *
 * Multiplicative decrease, multiplicative increase -- the shape TCP uses, and for
 * the same reason: it needs no model of the cost curve, only the sign of the
 * error. Each block,
 *
 *   - over target: cut the budget by @ref Config::attack;
 *   - comfortably under target: creep it back toward @ref Config::ceilingUnits by
 *     @ref Config::release;
 *   - in between: leave it alone, so the loop settles instead of hunting.
 *
 * with one addition. Pure multiplicative decrease takes several blocks to walk
 * down from the ceiling, and every one of those blocks is an xrun if the deadline
 * is ALREADY being missed. So when the measured load is at or over 1.0 the
 * governor also jumps straight to what the measurement says is affordable,
 * `unitsSpent x target / load`, and takes whichever is lower. One block to react
 * to a real overrun; a gentle slope for mere pressure.
 *
 * That jump ignores the fixed per-block cost (buses, wind, the master chain), so
 * it cuts too far rather than too little -- the safe direction for a deadline,
 * and @ref Config::floorUnits is what stops it cutting into nothing. If a machine
 * cannot render the floor in real time, the answer is not to shed further; it is
 * that the machine cannot run this instrument, and thinning it to four pipes only
 * hides that.
 *
 * ## Real-time contract
 *
 * Pure arithmetic on floats: no clock, no allocation, no lock, no branch on
 * anything but its own state. The clock lives in AudioEngine::processBlock, which
 * hands the elapsed time in -- which also makes the law here deterministic and
 * testable without a stopwatch.
 */
class CpuGovernor
{
public:
    /// Tuning constants. All off-thread; @ref configure republishes them.
    struct Config
    {
        /// Fraction of one block period the ENGINE may take. The rest is headroom
        /// for the plugin's own master chain, the host's mixer, the other plugins
        /// and the scheduler's jitter -- an audio callback that uses its whole
        /// period on average is already xrunning half the time.
        float targetLoad = 0.60f;

        /// Never budget above this: the point past which nothing is shed anyway.
        float ceilingUnits = 1024.0f;

        /// Never budget below this. See the class note: the floor is the guard
        /// against the fixed per-block cost being mistaken for voice cost.
        float floorUnits = 24.0f;

        /// Fraction of the budget given up per block while over target.
        float attack = 0.25f;

        /// Fraction of the budget won back per block while comfortably under it.
        /// Much slower than @ref attack on purpose: shedding is a fire alarm and
        /// recovering is not, and a fast release turns one heavy chord into an
        /// oscillation between shedding and overloading.
        float release = 0.02f;

        /// Recover only below @c targetLoad * @c deadBand. Without a dead band the
        /// loop has no stable point -- it alternates cut and creep every block
        /// around the target and the voice count flickers.
        float deadBand = 0.85f;

        /// Blocks to ignore after @ref reset. The first callbacks pay for cold
        /// caches, lazy page faults and the first touch of every partial array;
        /// governing on them would shed a tutti that the machine can hold easily
        /// once it is warm.
        std::uint32_t warmupBlocks = 64;
    };

    CpuGovernor() noexcept = default;

    /// Replace the tuning constants and re-clamp the current budget. Off-thread.
    void configure(const Config& config) noexcept;

    /// @return The active configuration.
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /**
     * @brief Return to the ceiling and re-arm the warm-up. Off-thread.
     *
     * Called from AudioEngine::prepare: a new sample rate or block size is a new
     * machine as far as this loop is concerned, and carrying a budget across one
     * would apply a verdict reached about a different deadline.
     */
    void reset() noexcept;

    /**
     * @brief Turn the loop on or off. Cheap enough to call every block, which is
     *        what an offline bounce needs.
     *
     * Disabled, @ref observe still tracks @ref load for the meters but never moves
     * the budget, and the budget stays wherever it was -- so a host that renders
     * offline (faster or slower than real time, and either way not a deadline)
     * gets exactly the audio it would have got live, rather than a mix thinned by
     * a stopwatch measuring the wrong thing.
     */
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    /// @return true while the loop is allowed to move the budget.
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /**
     * @brief Fold in one block's measurement and return the budget for the next.
     * @param elapsedSeconds  Wall time the block's render actually took.
     * @param deadlineSeconds Block period, i.e. frames / sample rate.
     * @param unitsSpent      Cost units the block actually consumed.
     * @return The budget, in cost units, for the next block.
     *
     * RT-safe. A nonsensical measurement (a negative or absurd delta, which is
     * what a suspended process or a re-based clock looks like) is dropped rather
     * than acted on -- waking from sleep must not shed the organ.
     */
    float observe(double elapsedSeconds, double deadlineSeconds,
                  float unitsSpent) noexcept;

    /// @return The budget in cost units, as @ref observe last left it.
    [[nodiscard]] float budgetUnits() const noexcept { return budget_; }

    /// @return Smoothed engine load, 1.0 meaning "a block took a whole block".
    [[nodiscard]] float load() const noexcept { return loadSmoothed_; }

    /// @return Decaying peak load, which is the honest thing to put on a meter:
    ///         an xrun is one block over 1.0, and an average never shows it.
    [[nodiscard]] float peakLoad() const noexcept { return peakLoad_; }

    /// @return Blocks whose measurement was over @ref Config::targetLoad since
    ///         @ref reset. A count rather than a flag: the useful question is
    ///         "does this machine struggle", not "did it once".
    [[nodiscard]] std::uint32_t pressureBlocks() const noexcept { return pressureBlocks_; }

private:
    /// One-pole coefficient for @ref load. At 512 frames / 48 kHz this averages
    /// over roughly a third of a second -- slow enough to read, fast enough to
    /// show a chord landing.
    static constexpr float kLoadSmoothing = 0.05f;

    /// Per-block decay of @ref peakLoad, chosen so a spike is still visible on a
    /// console polling at 30 Hz.
    static constexpr float kPeakDecay = 0.97f;

    Config        config_{};
    bool          enabled_        = false;
    float         budget_         = 1024.0f;
    float         loadSmoothed_   = 0.0f;
    float         peakLoad_       = 0.0f;
    std::uint32_t warmupLeft_     = 0;
    std::uint32_t pressureBlocks_ = 0;
};

} // namespace caecilia::core::engine
