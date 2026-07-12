/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/RegistrationCommandTemplate.h"

#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief One learned mapping: a physical controller -> a registration action.
 *
 * The @ref source names the CC / note / program that fires the binding, and
 * @ref command is the semantic @ref RegistrationCommandTemplate it resolves to —
 * carrying the SAME selector-grammar identity the UI, OSC and JSON-RPC surfaces
 * use, so a MIDI-learned drawstop is indistinguishable from one toggled anywhere
 * else.
 *
 * For continuous controllers, @ref triggerThreshold gives a simple on/off gate:
 * the binding fires when a CC value crosses from below the threshold to at-or-
 * above it (momentary controls send 127/0). Trivially copyable so the whole
 * table can be published to the audio thread atomically.
 */
struct MidiLearnBinding
{
    MidiSource                  source{};   ///< Physical controller to listen to.
    RegistrationCommandTemplate command{};  ///< Semantic action to request.
    std::uint8_t                triggerThreshold = 64; ///< CC on/off gate value.

    /// @return true if this binding is bound to a real controller.
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return source.kind != MidiSource::Kind::None
            && command.verb != RegistrationVerb::None;
    }

    /**
     * @brief Decide whether @p ev should fire this binding.
     *
     * Notes and program-changes fire on the "on" edge (note-on with velocity,
     * or the program-change itself). Continuous controllers fire when the value
     * reaches @ref triggerThreshold. Pure @c noexcept predicate, RT-safe.
     */
    [[nodiscard]] constexpr bool shouldFire(const MidiEvent& ev) const noexcept
    {
        if (!source.matches(ev))
            return false;
        switch (source.kind)
        {
            case MidiSource::Kind::Note:          return ev.isNoteOn();
            case MidiSource::Kind::ProgramChange: return true;
            case MidiSource::Kind::ControlChange: return ev.data2 >= triggerThreshold;
            case MidiSource::Kind::None:
            default:                              return false;
        }
    }

    friend bool operator==(const MidiLearnBinding&, const MidiLearnBinding&) noexcept = default;
};

} // namespace caecilia::midi
