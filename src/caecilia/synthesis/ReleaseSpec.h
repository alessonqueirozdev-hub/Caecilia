// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

namespace caecilia::synth
{

/**
 * @brief Multi-stage release behaviour, keyed to how long the note was held.
 *
 * A quickly-tapped pipe releases differently from a long-held one: the reservoir
 * collapse and room tail differ. @ref releaseMsForHold implements that much —
 * it interpolates a release time from the hold duration — and @ref SampleVoice
 * and @ref PhysicalPipeVoice call it. Neither voice is reachable from the
 * plugin, which plays @ref AdditiveVoice and never consults a ReleaseSpec.
 *
 * @todo The pressure-collapse chiff and the modeled tail are unimplemented; see
 *       @ref pressureCollapseChiff and @ref modeledTailSeconds.
 */
struct ReleaseSpec
{
    float shortReleaseMs        = 60.0f;  ///< Release time for a briefly-held note.
    float longReleaseMs         = 220.0f; ///< Release time for a long-held note.
    float holdThresholdSeconds  = 0.75f;  ///< Hold time above which @ref longReleaseMs applies.
    bool  pressureCollapseChiff = true;   ///< @todo Unread: no chiff-on-release exists.
    float modeledTailSeconds    = 0.4f;   ///< @todo Unread: no modeled release tail exists.

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
