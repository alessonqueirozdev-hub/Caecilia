// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/midi/MidiEvent.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

class MidiMap;

/**
 * @brief The audio thread's half of MIDI learn: what to do with an event, decided
 *        without the binding table.
 *
 * A learned drawstop takes two threads to work. The message thread owns the
 * @ref MidiMap, parses selectors and changes the registration; the audio thread
 * has exactly two questions in front of it — is a learn waiting for a control, and
 * does THIS control have a binding — and both are answerable from one bit. So it
 * gets one bit per control and nothing else: 512 bytes instead of the thirty
 * kilobytes a MidiMap occupies, and no question about whether that map is safe to
 * copy across a triple buffer.
 *
 * This class is that half. It lives here rather than in the plugin for two
 * reasons: it is pure logic over @ref MidiEvent with no JUCE in it, and the
 * plugin is not covered by the headless test suite — so written there it was the
 * one piece of a learned drawstop that nothing could check. Written here it is
 * checked, and the actuation rule stops being restated in two places that can
 * drift apart.
 *
 * ## Real-time contract
 * - @ref adopt is off-thread: it walks a binding table.
 * - @ref inspect and @ref reset are @c noexcept, allocation-free and lock-free.
 */
class LearnedControls
{
public:
    /// What the audio thread should do with the event it just offered.
    enum class Verdict : std::uint8_t
    {
        /// Nothing here concerns learn. Let it reach the organ.
        Play,

        /// A learn is armed and this is an actuation. Report it to the message
        /// thread and do NOT let it sound: binding a stop tab must not also sound
        /// the pipe it is being bound to.
        Capture,

        /// A bound control was actuated. Report it and swallow it.
        Fire,

        /// Swallow it and say nothing — the other edge of something already
        /// swallowed, or the "off" edge of a bound tab.
        Swallow
    };

    LearnedControls() noexcept = default;

    /**
     * @brief Rebuild the bitset from a binding table. Off the audio thread.
     *
     * A binding on the wildcard channel lights every channel's bit, or the audio
     * thread would pass the control through on fifteen of the sixteen.
     *
     * Program change is deliberately absent: it already has its own path to the
     * general pistons, and a learned program-change binding rides that rather than
     * being swallowed here.
     */
    void adopt(const MidiMap& map) noexcept;

    /// Forget every control. Off-thread; @ref adopt calls it first.
    void clear() noexcept;

    /**
     * @brief Decide what to do with one event.
     * @param ev    The incoming message.
     * @param armed Whether a learn is waiting for a control.
     * @return The verdict; see @ref Verdict.
     *
     * Mutates: it remembers the note-ons it swallowed so their note-offs go the
     * same way. RT-safe.
     */
    [[nodiscard]] Verdict inspect(const MidiEvent& ev, bool armed) noexcept;

    /// Drop every remembered note-off. Call from a host reset or a panic, where
    /// the note-offs those keys would have sent are never coming.
    void reset() noexcept { swallowed_.reset(); }

    /// @return true if a note on this channel has a binding. Diagnostic/test.
    [[nodiscard]] bool isNoteBound(MidiChannel channel, std::uint8_t note) const noexcept
    {
        return test(notes_, channel, note);
    }

    /// @return true if a CC on this channel has a binding. Diagnostic/test.
    [[nodiscard]] bool isControlBound(MidiChannel channel, std::uint8_t cc) const noexcept
    {
        return test(ccs_, channel, cc);
    }

    /**
     * @brief The actuation edges a learn accepts.
     *
     * A key going down, a program change, a controller moving off zero. NOT a key
     * coming up, a controller passing through zero on its way somewhere, or a
     * pressure message the pedalboard sends on its own.
     *
     * Public and used by @c MidiLearn::observe as well, so the audio thread and
     * the capture machine cannot disagree about what the organist meant. They were
     * two copies of this rule until this existed, with a comment on one of them
     * admitting it.
     */
    [[nodiscard]] static constexpr bool isActuation(const MidiEvent& ev) noexcept
    {
        return ev.isNoteOn()
            || ev.type == MidiMessageType::ProgramChange
            || (ev.type == MidiMessageType::ControlChange && ev.data2 > 0);
    }

private:
    /// 16 channels x 128 numbers, one bit each.
    static constexpr std::size_t kBits  = 16 * 128;
    static constexpr std::size_t kWords = kBits / 32;

    using Mask = std::array<std::uint32_t, kWords>;

    [[nodiscard]] static constexpr std::size_t bit(MidiChannel channel,
                                                   std::uint8_t number) noexcept
    {
        return static_cast<std::size_t>((channel & 0x0F) * 128 + (number & 0x7F));
    }

    [[nodiscard]] static constexpr bool test(const Mask& m, MidiChannel channel,
                                             std::uint8_t number) noexcept
    {
        const std::size_t b = bit(channel, number);
        return (m[b >> 5] & (std::uint32_t{ 1 } << (b & 31))) != 0;
    }

    static constexpr void set(Mask& m, MidiChannel channel, std::uint8_t number) noexcept
    {
        const std::size_t b = bit(channel, number);
        m[b >> 5] |= (std::uint32_t{ 1 } << (b & 31));
    }

    Mask               notes_{};
    Mask               ccs_{};
    std::bitset<kBits> swallowed_{};
};

} // namespace caecilia::midi
