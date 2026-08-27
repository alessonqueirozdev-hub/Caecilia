// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/midi/MidiEvent.h"

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief The note-ons something swallowed, so that their note-offs go the same way.
 *
 * A key that fires a piston or draws a stop must not also sound a pipe, so its
 * note-on is eaten — and then its note-off has to be eaten too, or the engine
 * never sees the key going down and the pipe would not have been sounding anyway.
 * Simple enough. The trap is the other case:
 *
 *   Deciding on the NOTE-OFF whether to swallow it — by asking "is this note bound
 *   / is it a page-turn key" — eats the release of a key whose press already
 *   sounded. That happens whenever the mapping arrives while the key is down, or a
 *   moment before it is played and the table has not crossed to the audio thread
 *   yet. The pipe then speaks until the next panic.
 *
 * So the decision is made on the way DOWN and remembered. This class is that
 * memory, and it exists on its own because three different things need it and had
 * been getting it right, wrong and wrong: the plugin's sequencer navigation, the
 * plugin's learned controls, and @c MidiRouter in both of its swallow paths.
 *
 * One bit per (channel, note): 256 bytes, no allocation, every operation
 * @c noexcept and lock-free.
 */
class SwallowedNotes
{
public:
    /// Note that @p ev — a note-on — was swallowed. A non-note-on is ignored.
    void remember(const MidiEvent& ev) noexcept
    {
        if (ev.type == MidiMessageType::NoteOn && ev.data2 > 0)
            bits_.set(slot(ev));
    }

    /**
     * @brief Claim @p ev if it is the release of a swallowed key.
     * @return true if it pairs with a remembered note-on, which is then forgotten.
     *
     * Only ever true for a note-off (a note-on of velocity zero included), so it
     * is safe to ask about any event.
     */
    [[nodiscard]] bool take(const MidiEvent& ev) noexcept
    {
        if (! ev.isNoteOff())
            return false;
        const std::size_t s = slot(ev);
        if (! bits_.test(s))
            return false;
        bits_.reset(s);
        return true;
    }

    /// @return true if this key's press was swallowed and its release has not come.
    [[nodiscard]] bool isPending(const MidiEvent& ev) const noexcept
    {
        return bits_.test(slot(ev));
    }

    /**
     * @brief Forget everything.
     *
     * For a host reset or a panic, where the releases these keys owed are never
     * coming. Without it the next press of one of them is swallowed to pay for a
     * release that already happened.
     */
    void reset() noexcept { bits_.reset(); }

    /// @return How many keys are still owed a release. Diagnostic/test.
    [[nodiscard]] std::size_t pendingCount() const noexcept { return bits_.count(); }

private:
    static constexpr std::size_t kBits = 16 * 128;

    [[nodiscard]] static constexpr std::size_t slot(const MidiEvent& ev) noexcept
    {
        return static_cast<std::size_t>((ev.channel & 0x0F) * 128 + (ev.data1 & 0x7F));
    }

    /// Not constexpr, and not for want of trying: std::bitset's observers only
    /// become constexpr in C++23, and this is a C++20 codebase. Everything here is
    /// still noexcept and allocation-free, which is what the audio thread needs.
    std::bitset<kBits> bits_{};
};

} // namespace caecilia::midi
