/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/AudioBlock.h"
#include "ceciliae/core/EngineTypes.h"

namespace ceciliae::core
{

/**
 * @brief A single sounding voice: the render unit the engine schedules.
 *
 * Conceptually an IVoice is a COMPOSITION of layers (attack + sustain +
 * release) sharing one spectral seed and a per-pipe voicing profile, not a
 * single monolithic engine. Pure-sample and pure-additive voices are simply
 * degenerate configurations of the same interface.
 *
 * ## Real-time contract
 * - @ref prepare is the ONLY method allowed to allocate or precompute. It runs
 *   off the audio thread before the voice ever renders.
 * - @ref noteOn, @ref noteOff, @ref renderAdd, @ref isActive and @ref kind /
 *   @ref tier are called on the audio thread and MUST be allocation-free,
 *   lock-free, exception-free. They are marked @c noexcept accordingly.
 * - @ref renderAdd MUST accumulate (+=) into the block, never overwrite, so
 *   many voices sum into the same per-windchest bus.
 */
class IVoice
{
public:
    virtual ~IVoice() = default;

    /**
     * @brief One-time (re)configuration. Allocation and precompute happen here.
     * @param sampleRate      Host sample rate in Hz (> 0).
     * @param maxBlockFrames  Largest block @ref renderAdd will be asked for.
     *
     * Not real-time safe by design; never call from the audio callback.
     */
    virtual void prepare(SampleRate sampleRate, std::size_t maxBlockFrames) = 0;

    /**
     * @brief Begin (or retrigger) the voice for a pipe at a velocity.
     * @param pipe      Stable identity of the pipe this voice sounds.
     * @param velocity  MIDI velocity in [1, 127].
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    virtual void noteOn(PipeId pipe, Velocity velocity) noexcept = 0;

    /**
     * @brief Request release. The voice enters its release layer but keeps
     *        rendering (and stays @ref isActive) until the tail finishes.
     *
     * RT-safe.
     */
    virtual void noteOff() noexcept = 0;

    /**
     * @brief Render this voice's contribution and ADD it into @p block.
     * @param block Destination bus (a per-windchest accumulation buffer).
     *
     * Must accumulate, never overwrite. RT-safe and @c noexcept.
     */
    virtual void renderAdd(AudioBlock& block) noexcept = 0;

    /**
     * @return true while the voice is producing audio (attack, sustain, or an
     *         unfinished release tail). When false the pool may reclaim it.
     *
     * RT-safe.
     */
    [[nodiscard]] virtual bool isActive() const noexcept = 0;

    /// @return The engine kind currently backing this voice. RT-safe.
    [[nodiscard]] virtual EngineKind kind() const noexcept = 0;

    /// @return The quality tier this voice currently renders at. RT-safe.
    [[nodiscard]] virtual VoiceTier tier() const noexcept = 0;

    /**
     * @brief Relative CPU cost estimate (arbitrary units) used by the deadline
     *        budget to decide stealing / tier demotion. RT-safe.
     */
    [[nodiscard]] virtual float cpuCostEstimate() const noexcept = 0;
};

} // namespace ceciliae::core
