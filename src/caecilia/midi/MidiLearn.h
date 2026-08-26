// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiLearnBinding.h"
#include "caecilia/midi/RegistrationCommandTemplate.h"

#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief The MIDI-learn capture state machine.
 *
 * The workflow: the UI (or a control adapter) picks a registration target and
 * @ref arm()s learn with the @ref RegistrationCommandTemplate that carries its
 * shared selector-grammar identity. The next suitable incoming controller is
 * captured into a @ref MidiLearnBinding, which the caller then installs into the
 * live @ref MidiMap.
 *
 * ## Threading
 * MIDI-learn is an interactive, OFF-audio-thread activity. @ref observe is meant
 * to be fed a copy of incoming events from a message-thread tap (not the render
 * callback), so it may touch this mutable state freely. Once a binding is
 * installed into the published @ref MidiMap, the audio-thread router reads only
 * that immutable map — the learn machine is never on the hot path. @todo no such
 * tap exists: nothing outside this directory constructs a MidiLearn.
 */
class MidiLearn
{
public:
    /// Where the capture machine currently sits.
    enum class State : std::uint8_t
    {
        Idle,     ///< Not listening.
        Armed,    ///< Listening for the next controller.
        Captured  ///< A binding is ready to be taken/installed.
    };

    MidiLearn() noexcept = default;

    /**
     * @brief Begin listening; the next suitable controller binds to @p target.
     *
     * Re-arming while already armed replaces the pending target and discards any
     * previously captured (but not yet taken) binding.
     */
    void arm(const RegistrationCommandTemplate& target) noexcept;

    /// Abandon learning and drop any pending capture.
    void cancel() noexcept;

    /**
     * @brief Offer an incoming event to the capture machine.
     * @return true if this event completed a capture (state becomes Captured).
     *
     * Only meaningful edges capture: a note-on, a program change, or a control
     * change whose value indicates an intentional actuation. Events that don't
     * qualify (note-offs, zero-value CCs, aftertouch) are ignored while armed.
     */
    [[nodiscard]] bool observe(const MidiEvent& ev) noexcept;

    // --- Queries ------------------------------------------------------------

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool  isArmed() const noexcept { return state_ == State::Armed; }
    [[nodiscard]] bool  hasCaptured() const noexcept { return state_ == State::Captured; }

    /// @return The captured binding (valid only when @ref hasCaptured()).
    [[nodiscard]] const MidiLearnBinding& captured() const noexcept { return captured_; }

    /**
     * @brief Consume the captured binding and return the machine to Idle.
     * @return The captured binding; a default binding if nothing was captured.
     */
    [[nodiscard]] MidiLearnBinding takeCaptured() noexcept;

private:
    /// Build a MidiSource that identifies the controller behind @p ev.
    [[nodiscard]] static MidiSource sourceFromEvent(const MidiEvent& ev) noexcept;

    State                       state_  = State::Idle;
    RegistrationCommandTemplate target_{};
    MidiLearnBinding            captured_{};
};

} // namespace caecilia::midi
