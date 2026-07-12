/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/Bandlimited.h"

// The polyBLEP / polyBLAMP residuals are closed-form and live inline in the
// header. This translation unit exists so the module has a compiled home for the
// higher-order band-limited correction that will replace them for very low
// oscillator frequencies.

namespace caecilia::dsp
{

/**
 * @brief Placeholder for a minBLEP-table residual (higher order than polyBLEP).
 * @param t  Normalised phase in [0, 1).
 * @param dt Phase increment per sample.
 * @return Currently forwards to @ref polyBlep. RT-safe.
 *
 * @note TODO(phase6): precompute an oversampled, integrated band-limited step
 *       table at prepare()-time and index it here for lower residual aliasing
 *       than the 2-sample polynomial, especially at sub-audio phase increments.
 */
float minBlepResidual(float t, float dt) noexcept
{
    return polyBlep(t, dt);
}

} // namespace caecilia::dsp
