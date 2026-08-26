// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/control/ControlCommand.h"

#include <array>

namespace caecilia::control
{
namespace
{

/// The canonical opcode <-> name table. This is the single source both the OSC
/// address space (`/caecilia/<name-with-slashes>`) and the JSON-RPC method map
/// (`caecilia.<name>`) are generated from, so all transports stay in lockstep.
struct OpcodeName
{
    ControlOpcode    opcode;
    std::string_view name;
};

constexpr std::array<OpcodeName, 32> kOpcodeNames{{
    {ControlOpcode::EngageStops,          "stops.engage"},
    {ControlOpcode::DisengageStops,       "stops.disengage"},
    {ControlOpcode::ToggleStops,          "stops.toggle"},
    {ControlOpcode::ClearAll,             "registration.clear"},
    {ControlOpcode::BuildPlenum,          "registration.buildPlenum"},
    {ControlOpcode::EngageCoupler,        "coupler.engage"},
    {ControlOpcode::DisengageCoupler,     "coupler.disengage"},
    {ControlOpcode::ToggleCoupler,        "coupler.toggle"},
    {ControlOpcode::RecallGeneral,        "general.recall"},
    {ControlOpcode::StoreGeneral,         "general.store"},
    {ControlOpcode::RecallDivisional,     "divisional.recall"},
    {ControlOpcode::StoreDivisional,      "divisional.store"},
    {ControlOpcode::SequencerNext,        "sequencer.next"},
    {ControlOpcode::SequencerPrev,        "sequencer.previous"},
    {ControlOpcode::SequencerFirst,       "sequencer.first"},
    {ControlOpcode::SequencerLast,        "sequencer.last"},
    {ControlOpcode::SetCrescendoStep,     "crescendo.set"},
    {ControlOpcode::Undo,                 "history.undo"},
    {ControlOpcode::Redo,                 "history.redo"},
    {ControlOpcode::NoteOn,               "note.on"},
    {ControlOpcode::NoteOff,              "note.off"},
    {ControlOpcode::AllNotesOff,          "note.allOff"},
    {ControlOpcode::Panic,                "note.panic"},
    {ControlOpcode::ResolveSelector,      "query.resolve"},
    {ControlOpcode::GetRegistrationState, "query.state"},
    {ControlOpcode::ExplainStop,          "query.explain"},
    {ControlOpcode::DescribeOrgan,        "query.organ"},
    {ControlOpcode::Ping,                 "meta.ping"},
    {ControlOpcode::GetGrammarVersion,    "meta.version"},
    // Trailing sentinels keep the array size stable as the contract grows.
    {ControlOpcode::None,                 ""},
    {ControlOpcode::None,                 ""},
    {ControlOpcode::None,                 ""},
}};

} // namespace

std::string_view opcodeName(ControlOpcode opcode) noexcept
{
    for (const auto& entry : kOpcodeNames)
        if (entry.opcode == opcode && opcode != ControlOpcode::None)
            return entry.name;
    return {};
}

ControlOpcode opcodeFromName(std::string_view name) noexcept
{
    if (name.empty())
        return ControlOpcode::None;
    for (const auto& entry : kOpcodeNames)
        if (entry.opcode != ControlOpcode::None && entry.name == name)
            return entry.opcode;
    return ControlOpcode::None;
}

bool ControlCommand::isQuery() const noexcept
{
    switch (opcode)
    {
        case ControlOpcode::ResolveSelector:
        case ControlOpcode::GetRegistrationState:
        case ControlOpcode::ExplainStop:
        case ControlOpcode::DescribeOrgan:
        case ControlOpcode::Ping:
        case ControlOpcode::GetGrammarVersion:
            return true;
        default:
            return false;
    }
}

} // namespace caecilia::control
