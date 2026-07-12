/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/control/JsonRpcControlTransport.h"

namespace caecilia::control
{
namespace
{
constexpr std::string_view kMethodPrefix = "caecilia.";
} // namespace

bool JsonRpcControlTransport::start()
{
    // TODO(phase8): bind the TCP/WebSocket listener on listenPort_ and spawn the
    // accept/read loop that feeds raw frames into handleMessage(). No socket or
    // JSON dependency is linked into caecilia_core; the concrete backend is an
    // isolated phase-8 shell. For now the transport arms without I/O.
    running_ = true;
    return true;
}

void JsonRpcControlTransport::stop() noexcept
{
    // TODO(phase8): close the listener and join the connection threads.
    running_ = false;
}

void JsonRpcControlTransport::onStateEvent(const StateEvent& event)
{
    // TODO(phase8): broadcast encodeNotification(event) to connected clients.
    (void) encodeNotification(event);
}

std::string JsonRpcControlTransport::methodFor(ControlOpcode opcode)
{
    std::string method{kMethodPrefix};
    method += opcodeName(opcode);
    return method;
}

ControlOpcode JsonRpcControlTransport::opcodeForMethod(std::string_view method)
{
    if (!method.starts_with(kMethodPrefix))
        return ControlOpcode::None;
    return opcodeFromName(method.substr(kMethodPrefix.size()));
}

std::string JsonRpcControlTransport::handleMessage(std::string_view requestJson)
{
    const std::optional<ControlCommand> command = decodeRequest(requestJson);
    if (!command.has_value())
    {
        // TODO(phase8): emit a proper JSON-RPC parse-error / invalid-request object.
        return encodeResponse(ControlResult::failure(ControlStatus::BadRequest,
                                                     "malformed JSON-RPC request"));
    }

    if (endpoint_ == nullptr)
        return encodeResponse(ControlResult::failure(ControlStatus::NotReady,
                                                     "transport not connected",
                                                     command->requestId));

    const ControlResult result = endpoint_->handle(*command);

    // A notification (no id) yields no response object per the JSON-RPC spec;
    // requestId == 0 is treated as "notification" by this stub's convention.
    if (command->requestId == 0)
        return {};

    return encodeResponse(result);
}

std::optional<ControlCommand> JsonRpcControlTransport::decodeRequest(std::string_view requestJson) const
{
    // TODO(phase8): parse the JSON-RPC object, look up "method" via
    // opcodeForMethod(), and read "params"/"id" into the command fields. Until
    // the JSON codec lands we cannot decode a raw string, so we report "no
    // command" and let handleMessage() answer BadRequest.
    (void) requestJson;
    return std::nullopt;
}

std::string JsonRpcControlTransport::encodeResponse(const ControlResult& result) const
{
    // TODO(phase8): serialise a real JSON-RPC 2.0 result/error object. This
    // placeholder documents the intended envelope and keeps the seam coherent.
    if (result.ok())
        return R"({"jsonrpc":"2.0","result":{"status":")"
               + std::string{statusName(result.status)} + "\"}}";

    return R"({"jsonrpc":"2.0","error":{"code":)"
           + std::to_string(static_cast<int>(result.status))
           + R"(,"message":")" + std::string{statusName(result.status)} + "\"}}";
}

std::string JsonRpcControlTransport::encodeNotification(const StateEvent& event) const
{
    // TODO(phase8): serialise a real JSON-RPC notification object.
    (void) event;
    return R"({"jsonrpc":"2.0","method":"caecilia.event"})";
}

} // namespace caecilia::control
