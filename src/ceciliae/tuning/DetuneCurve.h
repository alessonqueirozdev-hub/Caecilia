/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstdint>

#include "ceciliae/core/EngineTypes.h"

namespace ceciliae::tuning
{

/**
 * @brief Deterministic per-pipe micro-detune scatter keyed to @c PipeId.
 *
 * Real pipes are never in perfect tune with one another; that tiny, fixed
 * mistuning is a large part of why a rank sounds like many pipes rather than one
 * cloned oscillator. This curve derives a small, repeatable cent offset from the
 * pipe's stable identity, so the same pipe always drifts the same way across runs
 * and sessions. Combined with the synthesis-side PerPipeVoicer (which is seeded
 * from the same @c PipeId), it keeps unisons and mixtures shimmering instead of
 * phase-locking.
 *
 * The default amplitude of 0 disables scatter (a perfectly tuned rank). The hash
 * is a pure integer function, so @ref centsForPipe is fully RT-safe.
 */
struct DetuneCurve
{
    /// Peak scatter in cents (a value in [-amplitude, +amplitude] is produced).
    float amplitudeCents = 0.0f;

    /// Salt so different ranks/instruments can scatter independently.
    std::uint32_t seed = 0x9E3779B9u;

    /// @return A curve that applies no detune at all.
    [[nodiscard]] static constexpr DetuneCurve none() noexcept { return {}; }

    /**
     * @brief Deterministic cent offset for a specific pipe.
     * @param pipe Stable pipe identity (rank + note).
     * @return A signed cent offset in [-amplitudeCents, +amplitudeCents].
     *
     * RT-safe: a fixed sequence of integer ops plus one multiply; no allocation,
     * no locks, no branches on the hot value other than the amplitude short-cut.
     */
    [[nodiscard]] double centsForPipe(core::PipeId pipe) const noexcept
    {
        if (amplitudeCents == 0.0f)
            return 0.0;

        // Mix the pipe identity into the seed, then run a splitmix32-style
        // finaliser for a well-distributed 32-bit hash.
        std::uint32_t h = seed;
        h ^= static_cast<std::uint32_t>(pipe.rankId) * 0x85EBCA6Bu;
        h += static_cast<std::uint32_t>(pipe.midiNote) * 0xC2B2AE35u;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;

        // Map to [0, 1], then to [-1, 1], then scale by the amplitude.
        const double unit = static_cast<double>(h) / static_cast<double>(0xFFFFFFFFu);
        return (unit * 2.0 - 1.0) * static_cast<double>(amplitudeCents);
    }
};

} // namespace ceciliae::tuning
