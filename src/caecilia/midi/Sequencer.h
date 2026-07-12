/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/midi/MidiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief One entry in the combination sequencer's ordered program.
 *
 * A step names the registration to install when the sequencer lands on it. In
 * the resolved form the off-thread registration engine fills @ref stateDeltaId
 * with the handle of a pre-computed, published @c StateDelta so advancing the
 * sequencer is a single @c ApplyStateDelta command. @ref generalIndex records
 * which stored general (in the @ref CombinationStore) the step captured, so the
 * program survives re-resolution when the organ or registration changes.
 */
struct SequencerStep
{
    std::uint32_t stateDeltaId  = 0; ///< Handle into the off-thread StateDelta table.
    std::uint16_t generalIndex  = 0; ///< Source general combination, if captured from one.
    std::uint16_t label         = 0; ///< Optional display label / user tag.

    friend constexpr bool operator==(SequencerStep, SequencerStep) noexcept = default;
};

/**
 * @brief The combination stepper: an ordered list of registrations a performer
 *        walks with Previous/Next during a piece.
 *
 * The user drives it from si5/do6 (mapped to Previous/Next by the
 * @ref SequencerNavMap). Advancing yields the @ref SequencerStep to apply, which
 * the registration bridge turns into an @c ApplyStateDelta engine command.
 *
 * ## Real-time contract
 * The step *program* (the ordered list) is edited OFF the audio thread and then
 * published; while a piece plays it is immutable. The pointer-moving operations
 * (@ref next / @ref previous / @ref goTo / ...) mutate only the current index and
 * are @c noexcept, allocation-free, so the registration bridge can call them when
 * a page-turn intent arrives. Fixed-capacity: no allocation ever occurs.
 */
class Sequencer
{
public:
    static constexpr std::size_t kMaxSteps = 512;

    Sequencer() noexcept = default;

    // --- Off-thread program editing (NOT real-time safe) --------------------

    /// Remove every step and rewind to "before the first step". NOT RT-safe.
    void clear() noexcept;

    /**
     * @brief Append a step to the program.
     * @return true if appended; false if the program is already at capacity.
     * NOT real-time safe.
     */
    bool append(const SequencerStep& step) noexcept;

    /**
     * @brief Replace the step at @p index (must be < @ref count). NOT RT-safe.
     * @return true on success.
     */
    bool replace(std::size_t index, const SequencerStep& step) noexcept;

    // --- Pointer movement (real-time-safe) ----------------------------------

    /// Move to and return the next step; clamps at the last step. @c noexcept.
    [[nodiscard]] SequencerStep next() noexcept;

    /// Move to and return the previous step; clamps at the first step. @c noexcept.
    [[nodiscard]] SequencerStep previous() noexcept;

    /// Jump to and return the first step. @c noexcept.
    [[nodiscard]] SequencerStep first() noexcept;

    /// Jump to and return the last step. @c noexcept.
    [[nodiscard]] SequencerStep last() noexcept;

    /**
     * @brief Jump to and return step @p index (clamped to the valid range).
     * @c noexcept. No-op-safe when the program is empty.
     */
    [[nodiscard]] SequencerStep goTo(std::size_t index) noexcept;

    /// Reset the pointer to "before the first step" without changing the program.
    void rewind() noexcept { position_ = kNoPosition; }

    // --- Queries ------------------------------------------------------------

    /// @return The step the pointer currently rests on (undefined if none).
    [[nodiscard]] SequencerStep current() const noexcept;

    /// @return Current 0-based position, or @ref kNoPosition when unset/empty.
    [[nodiscard]] std::size_t position() const noexcept { return position_; }

    /// @return Number of steps in the program.
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    /// @return true if the program has no steps.
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    /// Sentinel meaning "no current position" (pointer before the first step).
    static constexpr std::size_t kNoPosition = static_cast<std::size_t>(-1);

private:
    std::array<SequencerStep, kMaxSteps> steps_{};
    std::size_t count_    = 0;
    std::size_t position_ = kNoPosition;
};

} // namespace caecilia::midi
