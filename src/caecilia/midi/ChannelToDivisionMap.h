// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/midi/MidiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief Routes each of the 16 MIDI channels to a division/manual, with an
 *        optional transpose and playable key range.
 *
 * A typical console binds channel 0 -> Great, 1 -> Swell, 2 -> Pedal, etc. Each
 * channel may transpose (e.g. a 61-key controller shifted to reach the full
 * compass) and clamp its playable range so split keyboards work.
 *
 * Structural edits happen off the audio thread; the router only reads it via the
 * @c noexcept accessors while rendering. Trivially copyable (16 small entries) so
 * a new mapping can be published wholesale.
 */
class ChannelToDivisionMap
{
public:
    static constexpr std::size_t kNumChannels = 16;

    /// Per-channel routing record.
    struct Entry
    {
        bool             mapped   = false;             ///< Is this channel routed at all?
        core::DivisionId division{};                   ///< Destination division when mapped.
        std::int8_t      transpose = 0;                ///< Semitone offset applied to notes.
        core::MidiNote   lowNote  = 0;                 ///< Lowest playable note (inclusive).
        core::MidiNote   highNote = 127;               ///< Highest playable note (inclusive).
    };

    ChannelToDivisionMap() noexcept = default;

    // --- Off-thread configuration (NOT real-time safe) ----------------------

    /// Route @p channel to @p division with an optional @p transpose.
    void mapChannel(MidiChannel channel, core::DivisionId division,
                    std::int8_t transpose = 0) noexcept
    {
        if (channel >= kNumChannels)
            return;
        auto& e     = entries_[channel];
        e.mapped    = true;
        e.division  = division;
        e.transpose = transpose;
    }

    /// Restrict @p channel to the inclusive key range [@p low, @p high].
    void setKeyRange(MidiChannel channel, core::MidiNote low, core::MidiNote high) noexcept
    {
        if (channel >= kNumChannels)
            return;
        entries_[channel].lowNote  = low;
        entries_[channel].highNote = high;
    }

    /// Remove any routing for @p channel.
    void clearChannel(MidiChannel channel) noexcept
    {
        if (channel < kNumChannels)
            entries_[channel] = Entry{};
    }

    // --- Real-time-safe queries ---------------------------------------------

    /// @return true if @p channel is routed to a division.
    [[nodiscard]] bool isMapped(MidiChannel channel) const noexcept
    {
        return channel < kNumChannels && entries_[channel].mapped;
    }

    /// @return The destination division for @p channel (undefined if unmapped).
    [[nodiscard]] core::DivisionId division(MidiChannel channel) const noexcept
    {
        return channel < kNumChannels ? entries_[channel].division : core::DivisionId{};
    }

    /**
     * @brief Apply @p channel's transpose to @p note, clamped to [0, 127].
     * @return The transposed note number.
     */
    [[nodiscard]] core::MidiNote transposed(MidiChannel channel, core::MidiNote note) const noexcept
    {
        if (channel >= kNumChannels)
            return note;
        const int shifted = static_cast<int>(note) + entries_[channel].transpose;
        if (shifted < 0)   return 0;
        if (shifted > 127) return 127;
        return static_cast<core::MidiNote>(shifted);
    }

    /// @return true if @p note (pre-transpose) is inside @p channel's key range.
    [[nodiscard]] bool inRange(MidiChannel channel, core::MidiNote note) const noexcept
    {
        if (channel >= kNumChannels)
            return false;
        const auto& e = entries_[channel];
        return note >= e.lowNote && note <= e.highNote;
    }

    /// @return The raw entry for @p channel (bounds-checked to channel 0).
    [[nodiscard]] const Entry& entry(MidiChannel channel) const noexcept
    {
        return entries_[channel < kNumChannels ? channel : 0];
    }

private:
    std::array<Entry, kNumChannels> entries_{};
};

} // namespace caecilia::midi
