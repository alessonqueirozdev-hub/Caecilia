// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/engine/EngineCommand.h"
#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiMap.h"
#include "caecilia/midi/MidiRouteResult.h"

namespace caecilia::midi
{

/**
 * @brief Turns raw core-native MIDI into typed engine intent.
 *
 * The router is the audio-thread front door of the MIDI module. It reads a
 * published, immutable @ref MidiMap and, for each @ref MidiEvent, decides
 * whether the message is a keyboard note (routed to a division with transpose
 * and velocity shaping), a sustain change, a registration action (a sequencer
 * page-turn, a PC recall, or a learned CC/note), or a panic.
 *
 * ## Resolution order (per event)
 *  1. **Note on/off** — a bound sequencer nav key (si5/do6) or a learned note
 *     control is consumed as a registration action on its "on" edge and the
 *     paired note-off is swallowed; otherwise the note is routed to the
 *     channel's division as a @ref NoteRoute.
 *  2. **Control change** — CC 120/123 => Panic; a learned CC => registration;
 *     CC 64 => sustain on the channel's division; anything else is ignored.
 *  3. **Program change** — resolved through the @ref ProgramChangeMap (PC->generals).
 *  4. Everything else is ignored in the scaffold.
 *
 * ## Real-time contract
 * @ref route is @c noexcept, allocation-free and lock-free; it never mutates the
 * map. @ref connect merely stores a pointer and must be called off the audio
 * thread before rendering. The router itself is stateless beyond that pointer,
 * so per-voice/per-note state (sustain latching, pipe expansion) lives in the
 * engine, not here.
 *
 * @todo Nothing in the plugin constructs a MidiRouter. The shipping audio
 *       callback walks juce::MidiMessage directly in plugin::CommandBridge,
 *       which handles note on/off, all-notes-off and sustain only.
 */
class MidiRouter
{
public:
    MidiRouter() noexcept = default;

    /**
     * @brief Point the router at the binding table it should read. NOT RT-safe.
     *
     * The referenced @ref MidiMap must outlive the router and must not be mutated
     * in place while rendering; publish a new map and re-connect instead.
     */
    void connect(const MidiMap& map) noexcept { map_ = &map; }

    /// @return true once a map has been connected.
    [[nodiscard]] bool isConnected() const noexcept { return map_ != nullptr; }

    /**
     * @brief Route one MIDI event to a typed intent.
     * @return A @ref MidiRouteResult; @ref MidiRouteKind::Ignored when the event
     *         is not actionable or no map is connected.
     *
     * Real-time safe: @c noexcept, no allocation, no locking.
     */
    [[nodiscard]] MidiRouteResult route(const MidiEvent& ev) const noexcept;

    /**
     * @brief Convenience bridge: encode a routed result as an @ref EngineCommand
     *        when a direct, registration-free encoding exists.
     * @param result A result previously produced by @ref route.
     * @param[out] out Filled with the encoded command on success.
     * @return true if @p result maps directly to an engine command.
     *
     * Only @ref MidiRouteKind::Panic maps directly today. @ref MidiRouteKind::Note
     * needs per-division pipe expansion and @ref MidiRouteKind::Registration must
     * be resolved off-thread, so both return false here and are handled by their
     * respective bridges. @c noexcept / RT-safe.
     */
    [[nodiscard]] static bool toEngineCommand(const MidiRouteResult& result,
                                              core::engine::EngineCommand& out) noexcept;

private:
    [[nodiscard]] MidiRouteResult routeNote(const MidiEvent& ev) const noexcept;
    [[nodiscard]] MidiRouteResult routeControlChange(const MidiEvent& ev) const noexcept;
    [[nodiscard]] MidiRouteResult routeProgramChange(const MidiEvent& ev) const noexcept;

    const MidiMap* map_ = nullptr;
};

} // namespace caecilia::midi
