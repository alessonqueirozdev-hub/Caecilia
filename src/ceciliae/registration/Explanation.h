/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <cstddef>
#include <string>

namespace ceciliae::registration
{

/**
 * @brief Provenance for one stop's state: the answer to "why is this on?".
 *
 * The rules/derivation engine records an Explanation for every stop it engages,
 * so @c RegistrationEngine::explain(StopId) can attribute the current sound to a
 * user action, a plenum build, a coupler, or a ported intent — the kind of
 * transparency no mainstream organ VST offers.
 */
struct Explanation
{
    /// The cause that put (or kept) a stop in its current state.
    enum class Reason
    {
        NotEngaged,      ///< The stop is currently off.
        DirectCommand,   ///< Drawn explicitly by the user (engage/toggle).
        SelectorMatch,   ///< Drawn because it matched a selector command.
        PlenumPrincipal, ///< Part of the principal chorus of a build-plenum.
        PlenumMixture,   ///< The mixture crown of a build-plenum.
        PlenumReed,      ///< A chorus reed added by a build-plenum.
        CouplerImplied,  ///< Sounding via an engaged coupler (not itself drawn).
        IntentPort,      ///< Resolved from a ported RegistrationIntent.
        SnapshotRecall   ///< Restored from a captured snapshot / piston.
    };

    core::StopId id{};                        ///< The stop being explained.
    Reason       reason = Reason::NotEngaged; ///< Why it is in its current state.
    std::string  detail;                      ///< Human sentence, e.g. "matched family:reed".
    std::size_t  commandIndex = 0;            ///< History index of the causing command.

    /// @return true if the stop is currently engaged for any reason.
    [[nodiscard]] bool isEngaged() const noexcept { return reason != Reason::NotEngaged; }
};

/// @return a short human label for @p reason (off-thread; for tooltips/logs).
[[nodiscard]] const char* toString(Explanation::Reason reason) noexcept;

} // namespace ceciliae::registration
