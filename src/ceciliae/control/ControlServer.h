/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/control/GrammarVersion.h"
#include "ceciliae/control/IControlTransport.h"
#include "ceciliae/control/ICommandSink.h"
#include "ceciliae/control/IStatePublisher.h"

#include <cstddef>
#include <vector>

namespace ceciliae::control
{

/**
 * @brief The transport-agnostic hub of the programmability surface.
 *
 * ControlServer wires the two halves of the control bus together and owns
 * nothing domain-specific:
 *
 *  - INBOUND (@c IControlEndpoint): every transport dispatches its decoded
 *    @c ControlCommand here; the server forwards it verbatim to the ONE
 *    @c ICommandSink (the engine-side registration/playback adapter) and returns
 *    the @c ControlResult for the transport to encode back. The server adds no
 *    registration logic — only a null/argument sanity gate.
 *
 *  - OUTBOUND (@c IStatePublisher): the engine-side adapter reports settled
 *    state changes via @ref publish, which the server fans out to every
 *    registered @c IStateObserver. Transports register themselves as observers
 *    (for OSC/JSON-RPC feedback); the UI and MIDI-LED drivers can register too.
 *
 * A registered transport therefore both feeds commands in and receives feedback
 * out through this single object, so adding a new control surface is purely a
 * matter of implementing @c IControlTransport and calling @ref addTransport.
 *
 * Lifetime: the server does not own the sink, the transports, or the observers;
 * the host (plugin/standalone shell) owns them and outlives the server. All
 * methods run OFF the audio thread.
 */
class ControlServer final : public IControlEndpoint, public IStatePublisher
{
public:
    /// Construct bound to @p sink (the engine-side registration/playback adapter).
    explicit ControlServer(ICommandSink& sink) noexcept;

    ControlServer(const ControlServer&)            = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    ~ControlServer() override;

    // --- Transport management ----------------------------------------------

    /**
     * @brief Register @p transport: connect it to this endpoint and subscribe it
     *        for outbound state feedback. No-op if already registered.
     *
     * Does not start it — call @ref start (or the transport's own @c start).
     */
    void addTransport(IControlTransport& transport);

    /// Unregister @p transport (stops feeding it commands and feedback).
    void removeTransport(IControlTransport& transport) noexcept;

    /// Start every registered transport. @return true iff all started.
    [[nodiscard]] bool start();

    /// Stop every registered transport. Idempotent; never throws.
    void stop() noexcept;

    /// @return true once @ref start has brought the transports up.
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    // --- IControlEndpoint ---------------------------------------------------

    /**
     * @brief Service one decoded command by forwarding it to the sink.
     *
     * Applies only a thin gate (reject @c ControlOpcode::None) before delegating;
     * all real work is the sink's. Never throws.
     */
    [[nodiscard]] ControlResult handle(const ControlCommand& command) override;

    // --- IStatePublisher ----------------------------------------------------

    void addObserver(IStateObserver* observer) override;
    void removeObserver(IStateObserver* observer) noexcept override;
    void publish(const StateEvent& event) override;

    // --- Introspection ------------------------------------------------------

    /// @return the control-surface / selector-grammar version the sink honours.
    [[nodiscard]] GrammarVersion grammarVersion() const noexcept;

    /// @return the number of currently registered transports.
    [[nodiscard]] std::size_t transportCount() const noexcept { return transports_.size(); }

private:
    ICommandSink*                    sink_ = nullptr; ///< Non-owning; the command bus.
    std::vector<IControlTransport*>  transports_;     ///< Non-owning; registered transports.
    std::vector<IStateObserver*>     observers_;      ///< Non-owning; feedback observers.
    bool                             running_ = false;
};

} // namespace ceciliae::control
