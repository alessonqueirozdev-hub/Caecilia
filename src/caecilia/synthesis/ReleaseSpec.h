/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

namespace caecilia::synth
{

/**
 * @brief Multi-stage release behaviour, keyed to how long the note was held.
 *
 * A quickly-tapped pipe releases differently from a long-held one: the reservoir
 * collapse and room tail differ. ReleaseSpec selects a release time by hold time
 * and optionally injects a modeled pressure-collapse chiff as the wind falls
 * away, so the release is not a single canned fade.
 */
struct ReleaseSpec
{
    float shortReleaseMs        = 60.0f;  ///< Release time for a briefly-held note.
    float longReleaseMs         = 220.0f; ///< Release time for a long-held note.
    float holdThresholdSeconds  = 0.75f;  ///< Hold time above which @ref longReleaseMs applies.
    bool  pressureCollapseChiff = true;   ///< Inject a modeled chiff as wind collapses on release.
    float modeledTailSeconds    = 0.4f;   ///< Length of the modeled (non-sampled) release tail.

    /**
     * @brief Resolve the release time for a given hold duration.
     * @param heldSeconds How long the note was held before release.
     * @return Release time in milliseconds, interpolated between the short and
     *         long release across the threshold.
     */
    [[nodiscard]] constexpr float releaseMsForHold(double heldSeconds) const noexcept
    {
        if (holdThresholdSeconds <= 0.0f)
            return longReleaseMs;
        const double t = heldSeconds / static_cast<double>(holdThresholdSeconds);
        const double clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return shortReleaseMs
             + static_cast<float>(clamped) * (longReleaseMs - shortReleaseMs);
    }
};

} // namespace caecilia::synth
