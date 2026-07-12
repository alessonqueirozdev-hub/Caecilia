/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <array>
#include <cstddef>

#include "ceciliae/core/ITuning.h"
#include "ceciliae/tuning/Temperament.h"

namespace ceciliae::tuning
{

/**
 * @brief Catalogue of the built-in historical temperaments.
 *
 * Provides the canonical cent-deviation table for each @c TemperamentId the
 * engine ships with (equal, quarter-comma meantone, Werckmeister III,
 * Kirnberger III, Pythagorean, Young). @c TemperamentId::Custom is intentionally
 * @e not a built-in: a custom temperament is supplied as data through
 * @ref Temperament::custom, never fabricated here.
 *
 * All lookups are pure, allocation-free and thread-safe (they return copies of
 * static tables). Temperament selection happens off the audio thread; the
 * resulting table is compiled into a @ref TuningTable and read RT-safely.
 */
class TemperamentLibrary
{
public:
    /// Number of temperaments with hard-coded built-in cent tables.
    static constexpr std::size_t kBuiltInCount = 6;

    /**
     * @brief Fetch a built-in temperament by id.
     * @param id Any @c TemperamentId.
     * @return The matching built-in temperament. For @c TemperamentId::Custom
     *         (or any unrecognised id) an equal-temperament fallback is returned,
     *         since custom tables must be provided via @ref Temperament::custom.
     *
     * RT-safe on paper (no allocation), but temperament changes are applied off
     * the audio thread, so treat this as an off-thread configuration call.
     */
    [[nodiscard]] static Temperament get(core::TemperamentId id) noexcept;

    /// @return true if @p id has a hard-coded built-in table (i.e. not Custom).
    [[nodiscard]] static bool isBuiltIn(core::TemperamentId id) noexcept;

    /// @return A static, null-terminated display name for @p id.
    [[nodiscard]] static const char* displayName(core::TemperamentId id) noexcept;

    /// @return The ids of every built-in temperament, in catalogue order.
    [[nodiscard]] static std::array<core::TemperamentId, kBuiltInCount> builtInIds() noexcept;
};

} // namespace ceciliae::tuning
