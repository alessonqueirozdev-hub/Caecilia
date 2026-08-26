// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/control/OscControlTransport.h"

namespace caecilia::control
{
namespace
{

constexpr std::string_view kRoot = "/caecilia/";

/// Translate the dotted opcode token (`stops.engage`) into an OSC tail
/// (`stops/engage`), appending onto @p out.
void appendSlashed(std::string& out, std::string_view dotted)
{
    for (char c : dotted)
        out.push_back(c == '.' ? '/' : c);
}

} // namespace

bool OscControlTransport::start()
{
    // TODO(phase8): bind a UDP socket on listenPort_ and spawn the reader thread
    // that parses datagrams -> decodeAddress() -> endpoint_->handle(). No socket
    // library is linked into caecilia_core; the concrete backend is an isolated
    // shell delivered in phase 8. For now the transport arms without I/O.
    running_ = true;
    return true;
}

void OscControlTransport::stop() noexcept
{
    // TODO(phase8): close the socket and join the reader thread.
    running_ = false;
}

void OscControlTransport::onStateEvent(const StateEvent& event)
{
    // TODO(phase8): serialise and send encodeFeedback(event) to subscribed OSC
    // clients. Kept as a no-op (the string is built to validate the mapping).
    (void) encodeFeedback(event);
}

std::string OscControlTransport::addressFor(ControlOpcode opcode)
{
    std::string address{kRoot};
    appendSlashed(address, opcodeName(opcode));
    return address;
}

std::optional<ControlCommand> OscControlTransport::decodeAddress(std::string_view address,
                                                                 std::string_view selectorArg) const
{
    if (!address.starts_with(kRoot))
        return std::nullopt;

    // Reconstruct the dotted opcode token from the address tail.
    std::string dotted;
    dotted.reserve(address.size());
    for (char c : address.substr(kRoot.size()))
        dotted.push_back(c == '/' ? '.' : c);

    const ControlOpcode opcode = opcodeFromName(dotted);
    if (opcode == ControlOpcode::None)
        return std::nullopt;

    ControlCommand command;
    command.opcode = opcode;
    command.selector.assign(selectorArg);

    // TODO(phase8): populate index / targetId / division / note / velocity from
    // the OSC type-tagged numeric arguments once the argument parser is in place.
    return command;
}

std::string OscControlTransport::encodeFeedback(const StateEvent& event) const
{
    // TODO(phase8): build a real OSC message with type tags. This textual form
    // documents the intended /caecilia/state/... mapping and keeps the seam live.
    std::string message{kRoot};
    message += "state/";
    switch (event.kind)
    {
        case StateEventKind::StopEngaged:
        case StateEventKind::StopDisengaged:   message += "stop";        break;
        case StateEventKind::CouplerEngaged:
        case StateEventKind::CouplerDisengaged:message += "coupler";     break;
        case StateEventKind::SequencerStep:    message += "sequencer";   break;
        case StateEventKind::CrescendoStep:    message += "crescendo";   break;
        case StateEventKind::GeneralRecalled:  message += "general";     break;
        case StateEventKind::NoteOn:
        case StateEventKind::NoteOff:          message += "note";        break;
        case StateEventKind::OrganLoaded:      message += "organ";       break;
        case StateEventKind::RegistrationCleared: message += "cleared";  break;
        case StateEventKind::Error:            message += "error";       break;
    }
    return message;
}

} // namespace caecilia::control
