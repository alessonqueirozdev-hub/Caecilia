/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/control/IControlTransport.h"

#include <vector>

namespace ceciliae::control
{

/**
 * @brief An in-process, wire-free transport — the reference implementation and
 *        the headless-test harness for the control bus.
 *
 * Its "codec" is the identity function: @ref send takes a fully-formed
 * @c ControlCommand and dispatches it straight through the connected
 * @c IControlEndpoint, returning the @c ControlResult. Outbound feedback
 * (@c StateEvent) is captured into an inspectable buffer instead of being
 * serialised anywhere.
 *
 * This lets Catch2 tests exercise the entire @c ControlServer / @c ICommandSink
 * path — and lets an embedded script host drive Ceciliae — without any socket,
 * OSC library, or JSON parser. The OSC and JSON-RPC transports are thin codecs
 * over this exact same dispatch, so anything proven here holds for them too.
 */
class LoopbackTransport final : public IControlTransport
{
public:
    LoopbackTransport() = default;

    // --- IControlTransport --------------------------------------------------

    [[nodiscard]] std::string_view name() const noexcept override { return "loopback"; }

    void connect(IControlEndpoint& endpoint) noexcept override { endpoint_ = &endpoint; }

    [[nodiscard]] bool start() override
    {
        running_ = true;
        return true;
    }

    void stop() noexcept override { running_ = false; }

    [[nodiscard]] bool isRunning() const noexcept override { return running_; }

    /// Capture an outbound state change for later inspection. Off-thread.
    void onStateEvent(const StateEvent& event) override;

    // --- In-proc client surface --------------------------------------------

    /**
     * @brief Dispatch @p command synchronously through the connected endpoint.
     * @return The endpoint's @c ControlResult, or a @c NotReady failure if the
     *         transport has not been connected to a server yet.
     *
     * The direct programmatic entry point used by tests and embedded scripts.
     */
    [[nodiscard]] ControlResult send(const ControlCommand& command);

    // --- Test / inspection helpers -----------------------------------------

    /// @return the outbound state events captured so far, in arrival order.
    [[nodiscard]] const std::vector<StateEvent>& capturedEvents() const noexcept { return events_; }

    /// Discard all captured outbound events.
    void clearCapturedEvents() noexcept { events_.clear(); }

private:
    IControlEndpoint*       endpoint_ = nullptr; ///< Non-owning; set by @ref connect.
    bool                    running_  = false;
    std::vector<StateEvent> events_;             ///< Captured outbound feedback.
};

} // namespace ceciliae::control
