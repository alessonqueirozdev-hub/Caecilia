// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"

#include <string>
#include <vector>

namespace caecilia::model
{

/**
 * @brief A windchest / bellows reservoir: the wind-supply grouping that ranks
 *        draw from and that the wind model integrates as one pressure ODE.
 *
 * The @c nominalPressurePa is the target pressure (organ builders quote wind in
 * millimetres of water column; ~83 mm ≈ 812 Pa is a common house pressure). All
 * ranks fed by the same chest share sag, which is why the chest, not the rank,
 * owns the pressure. @c hasTremulant has no reader anywhere: nothing turns it
 * into a @c wind::WindchestConfig tremulant binding, so the flag is inert and a
 * chest's tremulant never modulates anything.
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

} // namespace caecilia::model
