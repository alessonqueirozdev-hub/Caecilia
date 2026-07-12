/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstdint>
#include <string>
#include <utility>

namespace caecilia::control
{

/**
 * @brief The kind of state change fanned out to observers.
 *
 * These describe registration and playback truth AFTER it has settled off the
 * audio thread — they are NOT the audio-thread meter feed (that reaches the UI
 * through the engine's dedicated double-buffered @c MeterSnapshot, not here).
 * Observers turn them into OSC feedback bundles, MIDI LED updates, or UI hints.
 */
enum class StateEventKind : std::uint8_t
{
    StopEngaged,          ///< A drawstop was drawn.
    StopDisengaged,       ///< A drawstop was retired.
    CouplerEngaged,       ///< A coupler was engaged.
    CouplerDisengaged,    ///< A coupler was disengaged.
    RegistrationCleared,  ///< The whole registration was cleared.
    SequencerStep,        ///< The combination sequencer moved (@ref StateEvent::index = step).
    CrescendoStep,        ///< The crescendo / Walze moved (@ref StateEvent::index = step).
    GeneralRecalled,      ///< A general combination was recalled (@c index).
    NoteOn,               ///< A note started (@c division, @c note).
    NoteOff,              ///< A note released.
    OrganLoaded,          ///< A new organ definition became active.
    Error                 ///< An asynchronous error (message in @ref StateEvent::label).
};

/**
 * @brief One immutable state-change notification.
 *
 * A flat value type carrying only the fields relevant to its @ref kind, plus a
 * @ref label holding the pillar-5 semantic identity (e.g. `"Great Principal 8'"`)
 * so a screen reader, an OSC feedback string and a hardware display all render
 * the same human-facing fact from one source.
 */
struct StateEvent
{
    StateEventKind   kind     = StateEventKind::Error; ///< What changed.
    std::uint16_t    targetId = 0;                      ///< StopId / CouplerId, per kind.
    core::DivisionId division{};                         ///< Owning division (where meaningful).
    std::uint32_t    index    = 0;                      ///< Sequencer/crescendo/general index.
    core::MidiNote   note     = 0;                      ///< Note number (playback kinds).
    float            value    = 0.0f;                   ///< Auxiliary scalar (reserved).
    std::string      label;                             ///< Semantic identity / diagnostic text.

    /// Convenience factory for a stop engage/disengage notification.
    [[nodiscard]] static StateEvent stop(bool engaged, core::StopId id, std::string label = {})
    {
        StateEvent e;
        e.kind     = engaged ? StateEventKind::StopEngaged : StateEventKind::StopDisengaged;
        e.targetId = id.value;
        e.label    = std::move(label);
        return e;
    }
};

/**
 * @brief An observer of settled control state — a transport encoding feedback,
 *        a MIDI-LED driver, or the UI mirror.
 *
 * Fanned out OFF the audio thread by an @c IStatePublisher, so implementations
 * may allocate and perform I/O (encode a datagram, repaint). Not called on the
 * RT path; therefore not @c noexcept — an OSC socket write may legitimately
 * fail and should be handled by the observer, never propagated.
 */
class IStateObserver
{
public:
    virtual ~IStateObserver() = default;

    /// React to a single settled state change.
    virtual void onStateEvent(const StateEvent& event) = 0;
};

/**
 * @brief Fans settled state changes out to every registered @c IStateObserver.
 *
 * Implemented by @c ControlServer. Registration is single-threaded off-thread
 * work; observers are added/removed and events published from that same
 * off-thread context, so no locking is required by the contract.
 */
class IStatePublisher
{
public:
    virtual ~IStatePublisher() = default;

    /// Register @p observer (no-op if already present or null).
    virtual void addObserver(IStateObserver* observer) = 0;

    /// Remove @p observer if present.
    virtual void removeObserver(IStateObserver* observer) noexcept = 0;

    /// Deliver @p event to every current observer.
    virtual void publish(const StateEvent& event) = 0;
};

} // namespace caecilia::control
