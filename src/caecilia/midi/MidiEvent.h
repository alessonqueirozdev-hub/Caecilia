// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/midi/MidiTypes.h"

#include <cstdint>

namespace caecilia::midi
{

/// Channel-voice message kinds this module interprets. `Other` swallows
/// realtime/system messages the router intentionally ignores in the scaffold.
enum class MidiMessageType : std::uint8_t
{
    NoteOff,           ///< 0x8n : note released.
    NoteOn,            ///< 0x9n : note struck (velocity 0 == note-off by convention).
    PolyAftertouch,    ///< 0xAn : per-note pressure.
    ControlChange,     ///< 0xBn : continuous controller (CC).
    ProgramChange,     ///< 0xCn : program/patch select.
    ChannelAftertouch, ///< 0xDn : channel pressure.
    PitchBend,         ///< 0xEn : 14-bit pitch wheel.
    Other              ///< System / realtime / unhandled.
};

/**
 * @brief A core-native, JUCE-free MIDI channel-voice message.
 *
 * The plugin module translates @c juce::MidiMessage into this type at the seam,
 * so nothing downstream of the seam sees a JUCE MIDI type. @ref MidiEvent is
 * trivially copyable and carries a sample-accurate @ref sampleOffset so the
 * router can preserve intra-block timing when it emits engine intents.
 *
 * @c data1 / @c data2 are the raw MIDI data bytes (each in [0, 127]); their
 * meaning depends on @ref type (note/velocity, CC number/value, program, etc.).
 */
struct MidiEvent
{
    MidiMessageType type    = MidiMessageType::Other; ///< Decoded message kind.
    MidiChannel     channel = 0;                      ///< Source channel [0, 15].
    std::uint8_t    data1   = 0;                      ///< Note / CC number / program.
    std::uint8_t    data2   = 0;                      ///< Velocity / CC value / unused.
    std::uint32_t   sampleOffset = 0;                 ///< Frame within the current block.

    /// @return true for a note-on that actually sounds (velocity > 0).
    [[nodiscard]] constexpr bool isNoteOn() const noexcept
    {
        return type == MidiMessageType::NoteOn && data2 > 0;
    }

    /// @return true for a note-off (explicit, or a note-on with velocity 0).
    [[nodiscard]] constexpr bool isNoteOff() const noexcept
    {
        return type == MidiMessageType::NoteOff
            || (type == MidiMessageType::NoteOn && data2 == 0);
    }

    /**
     * @brief Squeeze the message into a single word.
     * @return Kind, channel and both data bytes: twenty-one bits of thirty-two.
     *
     * For carrying an event across a lock-free queue of plain integers, which is
     * how the plugin hands a learned control from the audio thread to the message
     * thread. @ref sampleOffset is deliberately NOT packed: what travels this way
     * is a drawstop or a piston, and the thread hop it is about to take is orders
     * of magnitude longer than the intra-block timing it would preserve.
     */
    [[nodiscard]] constexpr std::uint32_t pack() const noexcept
    {
        return (static_cast<std::uint32_t>(type)    << 18)
             | (static_cast<std::uint32_t>(channel) << 14)
             | (static_cast<std::uint32_t>(data1)   << 7)
             |  static_cast<std::uint32_t>(data2);
    }

    /// @brief The inverse of @ref pack; @ref sampleOffset comes back zero.
    [[nodiscard]] static constexpr MidiEvent unpack(std::uint32_t packed) noexcept
    {
        return { static_cast<MidiMessageType>((packed >> 18) & 0x07u),
                 static_cast<MidiChannel>((packed >> 14) & 0x0Fu),
                 static_cast<std::uint8_t>((packed >> 7) & 0x7Fu),
                 static_cast<std::uint8_t>(packed & 0x7Fu),
                 0 };
    }

    // --- factory helpers ----------------------------------------------------

    [[nodiscard]] static constexpr MidiEvent noteOn(MidiChannel ch, core::MidiNote note,
                                                    core::Velocity velocity,
                                                    std::uint32_t offset = 0) noexcept
    {
        return {MidiMessageType::NoteOn, ch, note, velocity, offset};
    }

    [[nodiscard]] static constexpr MidiEvent noteOff(MidiChannel ch, core::MidiNote note,
                                                     std::uint32_t offset = 0) noexcept
    {
        return {MidiMessageType::NoteOff, ch, note, 0, offset};
    }

    [[nodiscard]] static constexpr MidiEvent controlChange(MidiChannel ch, std::uint8_t cc,
                                                           std::uint8_t value,
                                                           std::uint32_t offset = 0) noexcept
    {
        return {MidiMessageType::ControlChange, ch, cc, value, offset};
    }

    [[nodiscard]] static constexpr MidiEvent programChange(MidiChannel ch, std::uint8_t program,
                                                           std::uint32_t offset = 0) noexcept
    {
        return {MidiMessageType::ProgramChange, ch, program, 0, offset};
    }
};

// ---------------------------------------------------------------------------
// Well-known continuous controllers this module reacts to by default.
// ---------------------------------------------------------------------------
namespace cc
{
    inline constexpr std::uint8_t kExpression   = 11;  ///< Swell shoe (enclosed divisions).
    inline constexpr std::uint8_t kSustainPedal = 64;  ///< Damper / sustain.
    inline constexpr std::uint8_t kAllSoundOff  = 120; ///< Channel Mode: all sound off.
    inline constexpr std::uint8_t kAllNotesOff  = 123; ///< Channel Mode: all notes off.
} // namespace cc

/**
 * @brief Identifies the physical controller a MIDI-learn binding listens to.
 *
 * A source matches a CC number, a specific note, or a program-change, optionally
 * restricted to one channel (or @ref kAnyChannel). Trivially copyable so it can
 * be stored in the RT-read binding table.
 */
struct MidiSource
{
    /// Which family of message triggers this binding.
    enum class Kind : std::uint8_t { None, ControlChange, Note, ProgramChange };

    Kind         kind    = Kind::None;    ///< Message family to match.
    MidiChannel  channel = kAnyChannel;   ///< Channel filter (kAnyChannel = wildcard).
    std::uint8_t data1   = 0;             ///< CC number / note number / program number.

    /// @return true if @p ev is the controller this source describes.
    [[nodiscard]] constexpr bool matches(const MidiEvent& ev) const noexcept
    {
        if (channel != kAnyChannel && channel != ev.channel)
            return false;
        switch (kind)
        {
            case Kind::ControlChange:
                return ev.type == MidiMessageType::ControlChange && ev.data1 == data1;
            case Kind::Note:
                return (ev.type == MidiMessageType::NoteOn
                        || ev.type == MidiMessageType::NoteOff)
                       && ev.data1 == data1;
            case Kind::ProgramChange:
                return ev.type == MidiMessageType::ProgramChange && ev.data1 == data1;
            case Kind::None:
            default:
                return false;
        }
    }

    friend constexpr bool operator==(MidiSource, MidiSource) noexcept = default;
};

} // namespace caecilia::midi
