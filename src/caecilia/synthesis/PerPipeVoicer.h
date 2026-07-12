/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/VoicingProfile.h"

#include <cstdint>

namespace caecilia::synth
{

/**
 * @brief Resolves deterministic, repeatable per-pipe hand-voicing variance.
 *
 * A real organ is voiced pipe by pipe; Caecilia reproduces that by scattering a
 * @ref VoicingProfile within the rank's @ref VoicingParams, seeded from the
 * pipe's stable @ref caecilia::core::PipeId. Because the seed is stable, a given
 * pipe always receives the same voicing across runs and sessions, and because
 * each rank's seed stream is independent, a mixture shimmers instead of sounding
 * cloned.
 *
 * The generator is a small deterministic hash (SplitMix64-style), so it needs no
 * heap and no @c <random> machinery and is safe to call anywhere, though it is
 * intended to run at voice setup time off the audio thread.
 */
class PerPipeVoicer
{
public:
    PerPipeVoicer() = default;

    /// Construct with the rank's voicing recipe.
    explicit PerPipeVoicer(const VoicingParams& params) noexcept : params_(params) {}

    /// Set the rank's voicing recipe (the scatter ranges). RT-safe.
    void setParams(const VoicingParams& params) noexcept { params_ = params; }

    /// @return The rank's current voicing recipe.
    [[nodiscard]] const VoicingParams& params() const noexcept { return params_; }

    /**
     * @brief Resolve the deterministic voicing profile for a pipe.
     * @param pipe The pipe's stable identity (the seed).
     * @return A repeatable @ref VoicingProfile scattered within @ref params().
     *
     * Deterministic and allocation-free; the same @p pipe always yields the same
     * profile.
     */
    [[nodiscard]] VoicingProfile voice(core::PipeId pipe) const noexcept;

    /**
     * @brief Mix an extra salt into the seed (e.g. an organ-definition id) so two
     *        organs voice the "same" pipe differently. RT-safe.
     */
    void setSeedSalt(std::uint64_t salt) noexcept { salt_ = salt; }

private:
    VoicingParams params_{};
    std::uint64_t salt_ = 0x9E3779B97F4A7C15ull;
};

} // namespace caecilia::synth
