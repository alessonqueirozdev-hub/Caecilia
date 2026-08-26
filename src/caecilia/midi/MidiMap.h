// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/midi/ChannelToDivisionMap.h"
#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiLearnBinding.h"
#include "caecilia/midi/ProgramChangeMap.h"
#include "caecilia/midi/SequencerNavMap.h"
#include "caecilia/midi/VelocityCurve.h"

#include <array>
#include <cstddef>

namespace caecilia::midi
{

/**
 * @brief The complete, resolved MIDI binding table the router reads.
 *
 * A @ref MidiMap aggregates every sub-mapping the @ref MidiRouter consults:
 *   - @ref ChannelToDivisionMap : which manual each channel plays,
 *   - @ref VelocityCurve        : how velocity is shaped,
 *   - @ref ProgramChangeMap     : PC -> generals/divisionals,
 *   - @ref SequencerNavMap      : the si5/do6 page-turn keys,
 *   - a fixed table of @ref MidiLearnBinding (CC / note / PC learned controls).
 *
 * ## Real-time contract
 * Every mutator here runs OFF the audio thread (during setup or while editing
 * bindings / MIDI-learn). The router holds a @c const pointer to a published map
 * and only calls the @c noexcept query methods while rendering. Because the map
 * is fixed-capacity and trivially relocatable, edits are done on a shadow copy
 * and swapped in wholesale rather than mutated in place under the audio thread.
 */
class MidiMap
{
public:
    /// Maximum number of simultaneously learned controls.
    static constexpr std::size_t kMaxLearnBindings = 256;

    /// Builds a map with the user's default si5/do6 sequencer navigation.
    MidiMap() noexcept;

    // --- Sub-map access -----------------------------------------------------
    /// @name Mutable accessors (off-thread configuration only).
    /// @{
    [[nodiscard]] ChannelToDivisionMap& channels() noexcept { return channels_; }
    [[nodiscard]] VelocityCurve&        velocity() noexcept { return velocity_; }
    [[nodiscard]] ProgramChangeMap&     programChange() noexcept { return programChange_; }
    [[nodiscard]] SequencerNavMap&      sequencerNav() noexcept { return sequencerNav_; }
    /// @}

    /// @name Const accessors (real-time-safe reads).
    /// @{
    [[nodiscard]] const ChannelToDivisionMap& channels() const noexcept { return channels_; }
    [[nodiscard]] const VelocityCurve&        velocity() const noexcept { return velocity_; }
    [[nodiscard]] const ProgramChangeMap&     programChange() const noexcept { return programChange_; }
    [[nodiscard]] const SequencerNavMap&      sequencerNav() const noexcept { return sequencerNav_; }
    /// @}

    // --- Learned-binding table (off-thread edits) ---------------------------

    /**
     * @brief Install a learned binding.
     * @return The slot index, or @c kMaxLearnBindings if the table is full.
     *
     * Replaces an existing binding whose source matches; otherwise appends.
     * NOT real-time safe.
     */
    std::size_t installBinding(const MidiLearnBinding& binding) noexcept;

    /// Remove the binding at @p index (no-op if out of range). NOT RT-safe.
    void removeBindingAt(std::size_t index) noexcept;

    /// Remove every learned binding. NOT RT-safe.
    void clearBindings() noexcept;

    /// @return Number of active learned bindings.
    [[nodiscard]] std::size_t bindingCount() const noexcept { return bindingCount_; }

    /**
     * @brief Read one learned binding by slot.
     * @return The binding, or a default (invalid) one if @p index is out of range.
     *
     * The table is what a console draws and what a saved document carries, and
     * both have to walk it. @c noexcept and allocation-free, so the audio thread
     * may also read it -- though the plugin deliberately does not: see
     * CaeciliaAudioProcessor::BoundControls for why one bit per control is enough
     * there.
     */
    [[nodiscard]] const MidiLearnBinding& bindingAt(std::size_t index) const noexcept
    {
        static const MidiLearnBinding kNone{};
        return index < bindingCount_ ? bindings_[index] : kNone;
    }

    // --- Real-time-safe query -----------------------------------------------

    /**
     * @brief Find the first learned binding whose physical source matches @p ev.
     * @return A pointer to the binding, or @c nullptr if none is bound to this
     *         controller.
     *
     * This matches on the controller identity (CC number / note / program +
     * channel) irrespective of the current value or note edge, so the router can
     * both fire the binding on its "on" edge and deliberately swallow the paired
     * "off" edge. Use @c MidiLearnBinding::shouldFire to decide whether to act.
     * @c noexcept and allocation-free; the router calls it while rendering.
     */
    [[nodiscard]] const MidiLearnBinding* findBinding(const MidiEvent& ev) const noexcept;

private:
    ChannelToDivisionMap channels_{};
    VelocityCurve        velocity_{};
    ProgramChangeMap     programChange_{};
    SequencerNavMap      sequencerNav_{};

    std::array<MidiLearnBinding, kMaxLearnBindings> bindings_{};
    std::size_t                                     bindingCount_ = 0;
};

} // namespace caecilia::midi
