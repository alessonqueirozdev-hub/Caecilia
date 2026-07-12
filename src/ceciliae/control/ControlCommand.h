/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ceciliae::control
{

/**
 * @brief The transport-agnostic verb set of the control surface.
 *
 * Every OSC address and every JSON-RPC method decodes to exactly one of these,
 * so the OSC, JSON-RPC, in-proc and (via a bridge) MIDI-learn transports all
 * drive the SAME command bus with zero behavioural drift. Grouped into
 * registration, playback, query and meta families; the numbering is a public
 * contract (see @c GrammarVersion) and must only grow additively.
 */
enum class ControlOpcode : std::uint16_t
{
    None = 0, ///< Invalid / no-op sentinel.

    // --- Registration: mutate the drawn state (selector-driven) -------------
    EngageStops    = 0x0100, ///< Draw every stop matched by @ref ControlCommand::selector.
    DisengageStops = 0x0101, ///< Retire every stop matched by the selector.
    ToggleStops    = 0x0102, ///< Flip every stop matched by the selector.
    ClearAll       = 0x0103, ///< Retire every drawn stop and coupler.
    BuildPlenum    = 0x0104, ///< Ask the plenum builder to construct a chorus (selector scopes it).

    EngageCoupler    = 0x0110, ///< Engage the coupler in @ref ControlCommand::targetId.
    DisengageCoupler = 0x0111, ///< Disengage that coupler.
    ToggleCoupler    = 0x0112, ///< Flip that coupler.

    // --- Registration: combinations, sequencer, history --------------------
    RecallGeneral    = 0x0120, ///< Recall general combination @ref ControlCommand::index.
    StoreGeneral     = 0x0121, ///< Capture the live state into general @ref ControlCommand::index.
    RecallDivisional = 0x0122, ///< Recall divisional @c index within @ref ControlCommand::division.
    StoreDivisional  = 0x0123, ///< Capture the division into divisional @c index.
    SequencerNext    = 0x0130, ///< Advance the combination sequencer one step.
    SequencerPrev    = 0x0131, ///< Step the sequencer back one.
    SequencerFirst   = 0x0132, ///< Jump the sequencer to its first step.
    SequencerLast    = 0x0133, ///< Jump the sequencer to its last step.
    SetCrescendoStep = 0x0134, ///< Move the crescendo / Walze to @ref ControlCommand::index.
    Undo             = 0x0140, ///< Undo the last registration command.
    Redo             = 0x0141, ///< Redo the last undone registration command.

    // --- Playback: note & transport control --------------------------------
    NoteOn      = 0x0200, ///< Sound @c note at @c velocity on @c division.
    NoteOff     = 0x0201, ///< Release @c note on @c division.
    AllNotesOff = 0x0202, ///< Gentle all-notes-off (respects releases).
    Panic       = 0x0203, ///< Hard all-sound-off (immediate silence).

    // --- Query: read-only; the answer travels in ControlResult::payload -----
    ResolveSelector      = 0x0300, ///< List the stops the selector currently matches.
    GetRegistrationState = 0x0301, ///< Report the drawn stops and couplers.
    ExplainStop          = 0x0302, ///< Explain why the stop in @c targetId is on/off (provenance).
    DescribeOrgan        = 0x0303, ///< Report the loaded organ's divisions/stops metadata.

    // --- Meta: handshake & liveness ----------------------------------------
    Ping           = 0x0400, ///< Liveness probe; echoes @c requestId.
    GetGrammarVersion = 0x0401 ///< Report the control-surface / selector-grammar version.
};

/// @return a stable, lowercase, dotted name for @p opcode (e.g. `"stops.engage"`).
/// Used as the canonical token both the OSC address space and the JSON-RPC
/// method map derive from. Never allocates; returns a static string.
[[nodiscard]] std::string_view opcodeName(ControlOpcode opcode) noexcept;

/// @return the opcode whose @ref opcodeName equals @p name, or @c None if unknown.
[[nodiscard]] ControlOpcode opcodeFromName(std::string_view name) noexcept;

/**
 * @brief A single decoded, transport-agnostic control request.
 *
 * This is the ONE object every transport produces and the ONE object the
 * @c ControlServer forwards to its @c ICommandSink. It is a plain value type: a
 * verb plus a small, opcode-dependent payload. Only the fields relevant to an
 * opcode are read (documented per field), so a `NoteOn` ignores @ref selector
 * and an `EngageStops` ignores @ref note.
 *
 * The @ref selector is carried as the shared textual grammar (NOT a parsed
 * tree): the control module intentionally performs no registration logic, so
 * the sink parses it once with @c registration::SelectorParser. That keeps a
 * single grammar authority and lets the command cross module boundaries as a
 * pure value. Because this type may hold a @c std::string it is an off-thread
 * object — it never touches the audio thread, which only ever sees the
 * fixed-capacity @c registration::StateDelta the sink emits downstream.
 */
struct ControlCommand
{
    ControlOpcode    opcode   = ControlOpcode::None; ///< The verb.
    std::string      selector;                       ///< Selector-grammar text (selector-driven verbs).
    std::uint32_t    index    = 0;                    ///< General/divisional/crescendo index.
    std::uint16_t    targetId = 0;                    ///< StopId / CouplerId / piston id, per opcode.
    core::DivisionId division{};                       ///< Divisional scope / playback manual.
    core::MidiNote   note     = 0;                    ///< Playback note number.
    core::Velocity   velocity = 0;                    ///< Playback velocity.
    std::uint64_t    requestId = 0;                   ///< Client correlation id, echoed in the result.

    /// @return true if this command reads state without mutating it.
    [[nodiscard]] bool isQuery() const noexcept;

    /// @return true if this command has no effect (unset opcode).
    [[nodiscard]] bool isNoOp() const noexcept { return opcode == ControlOpcode::None; }

    friend bool operator==(const ControlCommand&, const ControlCommand&) = default;

    // --- Ergonomic factories (off-thread convenience) -----------------------

    [[nodiscard]] static ControlCommand engage(std::string_view selectorText, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::EngageStops, std::string{selectorText}, 0, 0, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand disengage(std::string_view selectorText, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::DisengageStops, std::string{selectorText}, 0, 0, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand toggle(std::string_view selectorText, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::ToggleStops, std::string{selectorText}, 0, 0, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand recallGeneral(std::uint32_t generalIndex, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::RecallGeneral, {}, generalIndex, 0, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand noteOn(core::DivisionId div, core::MidiNote n,
                                               core::Velocity vel, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::NoteOn, {}, 0, 0, div, n, vel, reqId};
    }
    [[nodiscard]] static ControlCommand noteOff(core::DivisionId div, core::MidiNote n,
                                                std::uint64_t reqId = 0)
    {
        return {ControlOpcode::NoteOff, {}, 0, 0, div, n, 0, reqId};
    }
    [[nodiscard]] static ControlCommand resolve(std::string_view selectorText, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::ResolveSelector, std::string{selectorText}, 0, 0, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand explainStop(core::StopId stop, std::uint64_t reqId = 0)
    {
        return {ControlOpcode::ExplainStop, {}, 0, stop.value, {}, 0, 0, reqId};
    }
    [[nodiscard]] static ControlCommand ping(std::uint64_t reqId = 0)
    {
        return {ControlOpcode::Ping, {}, 0, 0, {}, 0, 0, reqId};
    }
};

} // namespace ceciliae::control
