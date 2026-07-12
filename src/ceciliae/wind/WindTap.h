/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/IWindSupply.h"

#include <cstddef>

namespace ceciliae::wind
{

/**
 * @brief A lightweight, non-owning binding of one windchest to a wind supply.
 *
 * A voice acquires a WindTap for its pipe's chest in prepare()/noteOn() and then
 * samples pressure sample-accurately while rendering, without repeating the
 * chest lookup or holding the whole IWindSupply surface. It is a trivially
 * copyable value: a pointer plus an id.
 *
 * ## Real-time contract
 * Every method is @c noexcept, allocation-free and lock-free. The tap only
 * READS the immutable per-block snapshot, so it is race-free by construction.
 */
class WindTap
{
public:
    /// Construct an invalid tap (returns zeros until bound).
    constexpr WindTap() noexcept = default;

    /// Bind a tap to @p chest within @p supply.
    WindTap(const core::IWindSupply& supply, core::WindchestId chest) noexcept
        : supply_(&supply)
        , chest_(chest)
    {
    }

    /// @return true if bound to a live supply.
    [[nodiscard]] bool isValid() const noexcept { return supply_ != nullptr; }

    /// @return The windchest this tap samples.
    [[nodiscard]] core::WindchestId chest() const noexcept { return chest_; }

    /// @return Nominal pressure (Pa) of the tapped chest, or 0 if unbound.
    [[nodiscard]] float nominalPressurePa() const noexcept
    {
        return supply_ != nullptr ? supply_->nominalPressurePa(chest_) : 0.0f;
    }

    /// @return Sample-accurate pressure (Pa) at @p frameInBlock, or 0 if unbound.
    [[nodiscard]] float pressureAt(std::size_t frameInBlock) const noexcept
    {
        return supply_ != nullptr ? supply_->pressureAt(chest_, frameInBlock) : 0.0f;
    }

    /// @return Sample-accurate normalised deviation at @p frameInBlock, or 0 if unbound.
    [[nodiscard]] float deviationAt(std::size_t frameInBlock) const noexcept
    {
        return supply_ != nullptr ? supply_->pressureDeviation(chest_, frameInBlock) : 0.0f;
    }

private:
    const core::IWindSupply* supply_ = nullptr;
    core::WindchestId        chest_{};
};

} // namespace ceciliae::wind
