// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

namespace caecilia::wind
{

/**
 * @brief How a tonal family reacts to a normalised wind-pressure deviation.
 *
 * The deviation fed in is @c IWindSupply::pressureDeviation, i.e.
 * (actual − nominal) / nominal. Each coefficient maps that deviation onto one
 * axis of the voice's response. Different families breathe on different curves:
 * reeds swing sharply in pitch and brightness as pressure moves, flues far
 * less, strings sit in between and shimmer. Applying these curves is what turns
 * the shared wind signal into audible, family-specific realism.
 *
 * These are plain coefficients (a linearisation about nominal); the synthesis
 * layer is free to feed the results through further nonlinearities.
 */
struct WindResponseCurve
{
    float centsPerDeviation      = 0.0f; ///< Pitch shift (cents) per unit deviation → FM.
    float dbPerDeviation         = 0.0f; ///< Level change (dB) per unit deviation → AM.
    float brightnessPerDeviation = 0.0f; ///< Harmonic-development / brightness change per unit deviation.
    float attackPerDeviation     = 0.0f; ///< Speech / attack-onset sensitivity per unit deviation.

    /// @return Pitch offset in cents for a given normalised deviation.
    [[nodiscard]] constexpr float pitchCents(float deviation) const noexcept
    {
        return centsPerDeviation * deviation;
    }

    /// @return Level offset in decibels for a given normalised deviation.
    [[nodiscard]] constexpr float levelDb(float deviation) const noexcept
    {
        return dbPerDeviation * deviation;
    }

    /// @return Brightness/harmonic-development offset for a given deviation.
    [[nodiscard]] constexpr float brightness(float deviation) const noexcept
    {
        return brightnessPerDeviation * deviation;
    }

    /// @return Attack/speech sensitivity offset for a given deviation.
    [[nodiscard]] constexpr float attack(float deviation) const noexcept
    {
        return attackPerDeviation * deviation;
    }
};

/**
 * @brief The default, hand-tuned wind response for a tonal family.
 * @param family The tonal family whose characteristic curve is requested.
 * @return A sane starting-point WindResponseCurve. RT-safe.
 *
 * Values are conservative first approximations to be refined against the
 * offline wind-sensitivity analysis in later phases.
 */
[[nodiscard]] WindResponseCurve defaultResponseFor(core::TonalFamily family) noexcept;

} // namespace caecilia::wind
