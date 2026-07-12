/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/midi/MidiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief Binds specific keys to sequencer navigation (Previous / Next / ...).
 *
 * The user maps si5 / do6 on the lowest manual to step the combination
 * sequencer backwards and forwards — the classic "page turner" workflow. Each
 * entry binds a note (optionally on one channel) to a @ref SequencerDirection;
 * the router swallows those keys so they never sound a pipe.
 *
 * Edits are off-thread; @ref lookup is a @c noexcept scan the router performs
 * per note event. Trivially copyable (a small fixed table).
 */
class SequencerNavMap
{
public:
    static constexpr std::size_t kMaxEntries = 16;

    /// Default MIDI note numbers for the user's si5 / do6 page-turn keys
    /// (middle C = 60 convention: B5 = 83, C6 = 84).
    static constexpr core::MidiNote kDefaultPreviousNote = 83; ///< si5.
    static constexpr core::MidiNote kDefaultNextNote     = 84; ///< do6.

    /// A single note -> direction binding.
    struct NavEntry
    {
        bool               active     = false;
        bool               anyChannel = true;
        MidiChannel        channel    = 0;
        core::MidiNote     note       = 0;
        SequencerDirection direction  = SequencerDirection::None;
    };

    SequencerNavMap() noexcept = default;

    /// @return A map preloaded with the user's default si5/do6 = Prev/Next binding.
    [[nodiscard]] static SequencerNavMap withUserDefaults() noexcept
    {
        SequencerNavMap m;
        m.bind(kDefaultPreviousNote, SequencerDirection::Previous);
        m.bind(kDefaultNextNote, SequencerDirection::Next);
        return m;
    }

    // --- Off-thread configuration (NOT real-time safe) ----------------------

    /**
     * @brief Bind @p note to @p direction. Replaces an existing binding for the
     *        same (note, channel) pair; no-op if the table is full.
     * @param channel Restrict to one channel, or leave @ref kAnyChannel for all.
     */
    void bind(core::MidiNote note, SequencerDirection direction,
              MidiChannel channel = kAnyChannel) noexcept
    {
        const bool anyCh = channel == kAnyChannel;
        for (auto& e : entries_)
        {
            if (e.active && e.note == note
                && e.anyChannel == anyCh && (anyCh || e.channel == channel))
            {
                e.direction = direction;
                return;
            }
        }
        for (auto& e : entries_)
        {
            if (!e.active)
            {
                e = NavEntry{true, anyCh, anyCh ? MidiChannel{0} : channel, note, direction};
                return;
            }
        }
        // Table full: silently drop (scaffold). TODO(phase8): surface capacity errors.
    }

    /// Remove all bindings.
    void clear() noexcept { entries_ = {}; }

    // --- Real-time-safe query -----------------------------------------------

    /**
     * @brief Resolve a (channel, note) pair to a navigation direction.
     * @return @ref SequencerDirection::None if the key is not a nav key.
     */
    [[nodiscard]] SequencerDirection lookup(MidiChannel channel,
                                            core::MidiNote note) const noexcept
    {
        for (const auto& e : entries_)
        {
            if (e.active && e.note == note
                && (e.anyChannel || e.channel == channel))
                return e.direction;
        }
        return SequencerDirection::None;
    }

    /// @return true if @p note (on @p channel) is bound to any navigation.
    [[nodiscard]] bool isNavKey(MidiChannel channel, core::MidiNote note) const noexcept
    {
        return lookup(channel, note) != SequencerDirection::None;
    }

private:
    std::array<NavEntry, kMaxEntries> entries_{};
};

} // namespace caecilia::midi
