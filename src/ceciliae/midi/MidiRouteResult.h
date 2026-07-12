/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/midi/RegistrationCommandTemplate.h"

#include <cstdint>

namespace ceciliae::midi
{

/// What a routed MIDI event resolved to.
enum class MidiRouteKind : std::uint8_t
{
    Ignored,      ///< The event is not actionable (e.g. pitch bend in the scaffold).
    Note,         ///< A keyboard note-on/note-off scoped to a division.
    Sustain,      ///< A sustain (damper) pedal change on a division.
    Registration, ///< A registration action (piston, sequencer, PC-recall, learned CC).
    Panic         ///< All-notes-off / all-sound-off.
};

/// A keyboard note routed to a division, post transpose + velocity shaping.
struct NoteRoute
{
    core::DivisionId division{};       ///< Manual/division the key belongs to.
    core::MidiNote   note     = 0;     ///< Transposed sounding note.
    core::Velocity   velocity = 0;     ///< Shaped velocity (0 on note-off).
    bool             on       = false; ///< true = note-on, false = note-off.
};

/// A sustain-pedal change scoped to a division.
struct SustainRoute
{
    core::DivisionId division{};    ///< Division the pedal affects.
    bool             down = false;  ///< true when the pedal is engaged.
};

/**
 * @brief The typed outcome of @c MidiRouter::route.
 *
 * A discriminated result rather than a raw engine command: note events still
 * need per-division pipe expansion (engine-side, against the live registration),
 * and registration intents must be resolved OFF the audio thread. Keeping the
 * router a pure @c raw-MIDI -> intent function preserves the strict layering
 * (the router owns no registration or voice-allocation logic).
 *
 * Read only the member named by @ref kind. The whole struct is trivially
 * copyable and can be batched into a small fixed buffer per block.
 */
struct MidiRouteResult
{
    MidiRouteKind kind = MidiRouteKind::Ignored;

    NoteRoute                   note{};         ///< Valid when kind == Note.
    SustainRoute                sustain{};      ///< Valid when kind == Sustain.
    RegistrationCommandTemplate registration{}; ///< Valid when kind == Registration.

    std::uint32_t sampleOffset = 0; ///< Intra-block frame carried from the source event.

    // --- factory helpers ----------------------------------------------------

    [[nodiscard]] static MidiRouteResult ignored(std::uint32_t offset = 0) noexcept
    {
        MidiRouteResult r;
        r.kind         = MidiRouteKind::Ignored;
        r.sampleOffset = offset;
        return r;
    }

    [[nodiscard]] static MidiRouteResult makeNote(NoteRoute n, std::uint32_t offset) noexcept
    {
        MidiRouteResult r;
        r.kind         = MidiRouteKind::Note;
        r.note         = n;
        r.sampleOffset = offset;
        return r;
    }

    [[nodiscard]] static MidiRouteResult makeSustain(SustainRoute s, std::uint32_t offset) noexcept
    {
        MidiRouteResult r;
        r.kind         = MidiRouteKind::Sustain;
        r.sustain      = s;
        r.sampleOffset = offset;
        return r;
    }

    [[nodiscard]] static MidiRouteResult makeRegistration(RegistrationCommandTemplate cmd,
                                                          std::uint32_t offset) noexcept
    {
        MidiRouteResult r;
        r.kind         = MidiRouteKind::Registration;
        r.registration = cmd;
        r.sampleOffset = offset;
        return r;
    }

    [[nodiscard]] static MidiRouteResult makePanic(std::uint32_t offset) noexcept
    {
        MidiRouteResult r;
        r.kind         = MidiRouteKind::Panic;
        r.sampleOffset = offset;
        return r;
    }
};

} // namespace ceciliae::midi
