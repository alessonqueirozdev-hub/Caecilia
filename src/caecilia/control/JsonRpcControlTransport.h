// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/control/IControlTransport.h"

#include <optional>
#include <string>
#include <string_view>

namespace caecilia::control
{

/**
 * @brief JSON-RPC 2.0 adapter for the control bus (TODO: concrete socket + JSON
 *        codec backend).
 *
 * Exposes the command surface as JSON-RPC 2.0 methods named `caecilia.<token>`,
 * where `<token>` is exactly @c opcodeName, so the method set never drifts from
 * the OSC address space or the in-proc surface:
 *
 * @code
 *   --> {"jsonrpc":"2.0","id":7,"method":"caecilia.stops.engage",
 *        "params":{"selector":"family:reed & pitch:8"}}
 *   <-- {"jsonrpc":"2.0","id":7,"result":{"status":"ok"}}
 *
 *   --> {"jsonrpc":"2.0","id":8,"method":"caecilia.query.resolve",
 *        "params":{"selector":"family:principal"}}
 *   <-- {"jsonrpc":"2.0","id":8,"result":{"status":"ok","payload":"[...]"}}
 *
 *   --> {"jsonrpc":"2.0","id":9,"method":"caecilia.note.on",
 *        "params":{"division":1,"note":60,"velocity":100}}
 *   <-- {"jsonrpc":"2.0","id":9,"result":{"status":"ok"}}
 *
 *   ; a failure maps status -> JSON-RPC error object:
 *   <-- {"jsonrpc":"2.0","id":9,"error":{"code":101,"message":"parseError: ..."}}
 *
 *   ; outbound state changes are JSON-RPC notifications (no id):
 *   <-- {"jsonrpc":"2.0","method":"caecilia.event.stop",
 *        "params":{"stopId":42,"engaged":true,"label":"Great Principal 8'"}}
 * @endcode
 *
 * @warning This is a stub adapter. @ref handleMessage wires the request/response
 *          shape end-to-end, but the JSON parse/serialise steps are
 *          `// TODO(phase8)`: NO third-party JSON library is pulled into
 *          @c caecilia_core. A concrete codec + socket/websocket backend lands as
 *          an isolated shell in phase 8. The transport already plugs into
 *          @c ControlServer and routes decoded commands through its endpoint.
 */
class JsonRpcControlTransport final : public IControlTransport
{
public:
    /// @param listenPort TCP/WebSocket port the concrete backend will bind (phase 8).
    explicit JsonRpcControlTransport(std::uint16_t listenPort = 9010) noexcept
        : listenPort_(listenPort)
    {
    }

    // --- IControlTransport --------------------------------------------------

    [[nodiscard]] std::string_view name() const noexcept override { return "jsonrpc"; }
    void connect(IControlEndpoint& endpoint) noexcept override { endpoint_ = &endpoint; }
    [[nodiscard]] bool start() override;
    void stop() noexcept override;
    [[nodiscard]] bool isRunning() const noexcept override { return running_; }
    void onStateEvent(const StateEvent& event) override;

    // --- JSON-RPC codec seam (isolated, dependency-free) -------------------

    /**
     * @brief Handle one JSON-RPC request string and return the response string.
     * @param requestJson A single JSON-RPC 2.0 request object.
     * @return The JSON-RPC response object (result or error). Empty for a valid
     *         notification (a request without an `id`).
     *
     * Decodes -> dispatches through the connected endpoint -> encodes. The JSON
     * (de)serialisation is TODO(phase8); the routing and status->error mapping
     * are real so the seam is exercisable end-to-end once the codec is dropped in.
     */
    [[nodiscard]] std::string handleMessage(std::string_view requestJson);

    /// @return the JSON-RPC method name for @p opcode (e.g. `caecilia.stops.engage`).
    [[nodiscard]] static std::string methodFor(ControlOpcode opcode);

    /// @return the opcode for a `caecilia.<token>` @p method, or @c None if unknown.
    [[nodiscard]] static ControlOpcode opcodeForMethod(std::string_view method);

private:
    /// Decode a request object into a command (TODO: real JSON parse).
    [[nodiscard]] std::optional<ControlCommand> decodeRequest(std::string_view requestJson) const;

    /// Encode a result/error response object (TODO: real JSON serialise).
    [[nodiscard]] std::string encodeResponse(const ControlResult& result) const;

    /// Encode an outbound `caecilia.event.*` notification (TODO: real JSON serialise).
    [[nodiscard]] std::string encodeNotification(const StateEvent& event) const;

    IControlEndpoint* endpoint_   = nullptr; ///< Non-owning; set by @ref connect.
    std::uint16_t     listenPort_ = 9010;
    bool              running_    = false;
};

} // namespace caecilia::control
