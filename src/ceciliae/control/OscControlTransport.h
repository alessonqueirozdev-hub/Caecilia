/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/control/IControlTransport.h"

#include <optional>
#include <string>
#include <string_view>

namespace ceciliae::control
{

/**
 * @brief OSC adapter for the control bus (TODO: concrete UDP backend).
 *
 * Maps the Open Sound Control address space onto @c ControlCommand and encodes
 * @c StateEvent feedback back as OSC bundles. The address space is derived
 * mechanically from @c opcodeName by replacing `'.'` with `'/'` under a common
 * root, so it never drifts from the other transports:
 *
 * @code
 *   /ceciliae/stops/engage      "family:reed & pitch:8"        ; s
 *   /ceciliae/stops/disengage   "div:swell"                    ; s
 *   /ceciliae/note/on           <division:int> <note:int> <vel:int>
 *   /ceciliae/note/off          <division:int> <note:int>
 *   /ceciliae/general/recall    <index:int>
 *   /ceciliae/sequencer/next
 *   /ceciliae/query/resolve     "family:reed"                  ; -> /ceciliae/reply
 *   /ceciliae/meta/ping         <requestId:int>
 *
 *   ; outbound feedback:
 *   /ceciliae/state/stop        <stopId:int> <engaged:int> "Great Principal 8'"
 *   /ceciliae/reply             <requestId:int> <status:string> "<payload>"
 * @endcode
 *
 * @warning This is a stub adapter. The message CODEC seam (@ref decodeAddress /
 *          @ref encodeFeedback) is stubbed with `// TODO(phase8)` bodies and NO
 *          UDP socket is opened — the class intentionally pulls no networking
 *          dependency into @c ceciliae_core. A concrete socket backend lands as
 *          a separate, isolated shell in phase 8. Until then the transport is
 *          inert but plugs cleanly into @c ControlServer, and its codec can be
 *          exercised directly through @ref decodeAddress for testing.
 */
class OscControlTransport final : public IControlTransport
{
public:
    /// @param listenPort UDP port the concrete backend will bind (phase 8).
    explicit OscControlTransport(std::uint16_t listenPort = 9000) noexcept
        : listenPort_(listenPort)
    {
    }

    // --- IControlTransport --------------------------------------------------

    [[nodiscard]] std::string_view name() const noexcept override { return "osc"; }
    void connect(IControlEndpoint& endpoint) noexcept override { endpoint_ = &endpoint; }
    [[nodiscard]] bool start() override;
    void stop() noexcept override;
    [[nodiscard]] bool isRunning() const noexcept override { return running_; }
    void onStateEvent(const StateEvent& event) override;

    // --- OSC codec seam (isolated, dependency-free) ------------------------

    /**
     * @brief Decode a single OSC address + already-parsed string argument into a
     *        @c ControlCommand.
     * @param address The OSC address pattern (e.g. `/ceciliae/stops/engage`).
     * @param selectorArg The first string argument, if any (the selector text).
     * @return The decoded command, or @c std::nullopt if the address is unknown.
     *
     * Pure and dependency-free so it is unit-testable without a socket. Numeric
     * argument extraction (note/velocity/index) is TODO(phase8) once the OSC
     * type-tag parser lands.
     */
    [[nodiscard]] std::optional<ControlCommand> decodeAddress(std::string_view address,
                                                              std::string_view selectorArg = {}) const;

    /// @return the OSC address for @p opcode (e.g. `/ceciliae/stops/engage`).
    [[nodiscard]] static std::string addressFor(ControlOpcode opcode);

private:
    /// Encode @p event as the outbound `/ceciliae/state/...` bundle (TODO: emit).
    [[nodiscard]] std::string encodeFeedback(const StateEvent& event) const;

    IControlEndpoint* endpoint_   = nullptr; ///< Non-owning; set by @ref connect.
    std::uint16_t     listenPort_ = 9000;
    bool              running_    = false;
};

} // namespace ceciliae::control
