/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/IWindSupply.h"

#include <string>
#include <vector>

namespace ceciliae::model
{

/**
 * @brief A windchest / bellows reservoir: the wind-supply grouping that ranks
 *        draw from and that the wind model integrates as one pressure ODE.
 *
 * The @c nominalPressurePa is the target pressure (organ builders quote wind in
 * millimetres of water column; ~83 mm ≈ 812 Pa is a common house pressure). All
 * ranks fed by the same chest share sag and — if @c hasTremulant — the same
 * tremulant modulation, which is why the chest, not the rank, owns those flags.
 *
 * This is immutable spec data compiled from the organ definition; it is aliased
 * as @c WindchestSpec for modules (registration, wind) that speak in "spec"
 * terms.
 */
struct Windchest
{
    core::WindchestId       id{};                 ///< Stable identity (dense index within the organ).
    std::string             name;                 ///< Human-readable label ("Great chest").
    float                   nominalPressurePa = 812.0f; ///< Target pressure in pascals.
    bool                    hasTremulant       = false; ///< Whether a tremulant modulates this chest.
    std::vector<core::RankId> ranks;              ///< Ranks fed by this chest.

    /// @return Number of ranks fed by this chest.
    [[nodiscard]] std::size_t rankCount() const noexcept { return ranks.size(); }
};

/// Spec-vocabulary alias: the immutable windchest description.
using WindchestSpec = Windchest;

} // namespace ceciliae::model
