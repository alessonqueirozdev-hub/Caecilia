/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

namespace ceciliae::model
{

/**
 * @brief Per-pipe physical placement in the (virtual) case, consumed by the dsp
 *        spatialization + early-reflection stage.
 *
 * A pipe's position colours its sound before any reverb: a stereo pan, a
 * distance-dependent level/pre-delay, and an azimuth/elevation that feed the
 * pipe-position early reflections. These are authored off the audio thread (from
 * the organ definition) and then read-only while rendering, so the fields are a
 * plain value type with no invariants beyond sane ranges.
 *
 * Conventions:
 * - @c panNorm is in [-1, +1] (-1 hard left, +1 hard right).
 * - Angles are in degrees; distance is in metres from the listening position.
 * - @c preDelayMs is the geometric propagation pre-delay to the first pickup.
 */
struct PipeSpatial
{
    float panNorm       = 0.0f; ///< Stereo pan in [-1, +1].
    float distanceMeters = 8.0f; ///< Distance from listener in metres.
    float azimuthDeg    = 0.0f; ///< Horizontal angle in degrees (0 = front).
    float elevationDeg  = 0.0f; ///< Vertical angle in degrees (0 = ear height).
    float preDelayMs    = 0.0f; ///< Geometric propagation pre-delay in ms.
    float caseDepthNorm = 0.0f; ///< Depth into the case in [0, 1] (0 = façade).

    friend constexpr bool operator==(const PipeSpatial&, const PipeSpatial&) noexcept = default;
};

} // namespace ceciliae::model
