// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/control/ControlCommand.h"
#include "caecilia/control/ControlResult.h"
#include "caecilia/control/IStatePublisher.h"

#include <string_view>

namespace caecilia::control
{

/**
 * @brief The inbound half of the control bus, as seen by a transport.
 *
 * A transport decodes a wire message into a @c ControlCommand and calls
 * @ref handle to service it synchronously, receiving a @c ControlResult to
 * encode back to the caller. @c ControlServer implements this and simply
 * forwards to its @c ICommandSink — so a transport never sees the sink or any
 * registration type directly, keeping the encode/decode layer perfectly thin.
 */
class IControlEndpoint
{
public:
    virtual ~IControlEndpoint() = default;

    /// Service one decoded command and return its result. Off-thread only.
    [[nodiscard]] virtual ControlResult handle(const ControlCommand& command) = 0;
};

/**
 * @brief A pluggable control transport: OSC, JSON-RPC, in-proc loopback, ....
 *
 * A transport is a pure codec + I/O shell around the bus. Inbound: it reads its
 * wire format, decodes a @c ControlCommand, and dispatches it through the
 * @c IControlEndpoint it was @ref connect "connected" to. Outbound: it is also
 * an @c IStateObserver, so the server's @c publish fans settled state changes to
 * it and it encodes them as feedback (an OSC bundle, a JSON-RPC notification, a
 * MIDI LED update).
 *
 * All transport I/O lives OFF the audio thread. Concrete socket-based transports
 * (@c OscControlTransport, @c JsonRpcControlTransport) isolate their OS/network
 * glue behind this interface so the pure command algebra stays headless-testable
 * via @c LoopbackTransport.
 */
class IControlTransport : public IStateObserver
{
public:
    ~IControlTransport() override = default;

    /// @return a short, stable identifier for logs / diagnostics (e.g. `"osc"`).
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /**
     * @brief Bind the transport to the endpoint that will service its commands.
     *
     * Called once by @c ControlServer::addTransport before @ref start. The
     * endpoint outlives the transport by construction (both owned by the host).
     */
    virtual void connect(IControlEndpoint& endpoint) noexcept = 0;

    /**
     * @brief Begin serving (open sockets, spawn the reader thread, ...).
     * @return true on success; false if the transport could not start.
     *
     * Off-thread; may allocate and touch the OS. Idempotent: starting a running
     * transport is a successful no-op.
     */
    [[nodiscard]] virtual bool start() = 0;

    /// Stop serving and release I/O resources. Idempotent; never throws.
    virtual void stop() noexcept = 0;

    /// @return true while the transport is actively serving.
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;

    // Inherited from IStateObserver:
    //   void onStateEvent(const StateEvent&)  -> encode & emit outbound feedback.
};

} // namespace caecilia::control
