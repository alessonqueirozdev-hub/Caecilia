/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/model/Coupler.h"
#include "ceciliae/registration/StopSet.h"

#include <span>
#include <vector>

namespace ceciliae::registration
{

/**
 * @brief The live registration: which drawstops and couplers are currently
 *        engaged.
 *
 * This is the STATE layer of the registration brain (as opposed to the
 * immutable SPECIFICATION in @c model::OrganSpec and the declarative INTENT in
 * a @c Selector). Couplers are first-class here because a key expansion has to
 * fold coupled divisions in before the model's pipe-activation mapping is
 * queried.
 *
 * A RegistrationState is a plain off-thread value type: it is folded from the
 * command log by the @c RegistrationEngine, snapshotted into the
 * @c RegistrationHistory, and diffed into a fixed-capacity @c StateDelta that is
 * the only thing handed across the audio seam. Equality is by engaged-set
 * content, so re-deriving the same registration always compares equal.
 */
struct RegistrationState
{
    RegistrationState() = default;

    // --- Stops --------------------------------------------------------------
    [[nodiscard]] bool isEngaged(core::StopId id) const noexcept { return stops.contains(id); }
    [[nodiscard]] const StopSet& engagedStops() const noexcept { return stops; }

    /// Engage @p id; @return true if it changed state.
    bool engage(core::StopId id) { return stops.insert(id); }
    /// Disengage @p id; @return true if it changed state.
    bool disengage(core::StopId id) { return stops.erase(id); }
    /// Toggle @p id; @return the new engaged state.
    bool toggle(core::StopId id) { return stops.toggle(id); }

    // --- Couplers -----------------------------------------------------------
    [[nodiscard]] bool isCouplerEngaged(model::CouplerId id) const noexcept;
    [[nodiscard]] std::span<const model::CouplerId> engagedCouplers() const noexcept { return couplers; }

    bool engageCoupler(model::CouplerId id);
    bool disengageCoupler(model::CouplerId id);
    bool toggleCoupler(model::CouplerId id);

    // --- Whole-state ops ----------------------------------------------------
    /// Disengage everything (stops and couplers).
    void clearAll() noexcept;

    /// @return total number of engaged stops + couplers (registration "weight").
    [[nodiscard]] std::size_t drawnCount() const noexcept { return stops.size() + couplers.size(); }

    friend bool operator==(const RegistrationState& a, const RegistrationState& b) noexcept
    {
        return a.stops == b.stops && a.couplers == b.couplers;
    }
    friend bool operator!=(const RegistrationState& a, const RegistrationState& b) noexcept
    {
        return !(a == b);
    }

    StopSet                       stops;    ///< Engaged drawstops (sorted, unique).
    std::vector<model::CouplerId> couplers; ///< Engaged couplers (sorted by value, unique).
};

} // namespace ceciliae::registration
