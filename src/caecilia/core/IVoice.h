// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"

namespace caecilia::core
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
     * @brief Stop immediately, with no release tail, and report inactive.
     *
     * This is what a host means by reset, and @ref noteOff is the wrong tool for
     * it: a release keeps the voice sounding for as much as a third of a second
     * after the host has been told the instrument is silent. That fails an Audio
     * Unit validation pass outright, and it makes an offline render depend on what
     * was played before it.
     *
     * RT-safe, @c noexcept: it is reachable from the audio thread.
     */
    virtual void silence() noexcept = 0;

    /**
     * @brief Set this block's swell-shoe gain, as a per-sample ramp.
     * @param startGain    Gain at the first frame of the block.
     * @param incPerSample Added to it each frame.
     *
     * A ramp rather than a level because a shoe is dragged, not stepped: a
     * per-block gain jump under a sustained chord is a zipper, and a sustained
     * chord is what an organ mostly plays.
     *
     * Called by the scheduler immediately before @ref renderAdd, so a voice may
     * simply store the pair. RT-safe, @c noexcept.
     */
    virtual void setExpression(float startGain, float incPerSample) noexcept = 0;

    /**
     * @brief Become the rank described by @p voicing before the next note-on.
     * @param voicing Opaque handle from @c engine::EngagedRank::voicing. The
     *        engine carries it without knowing what it is; the voice knows.
     *
     * One voice per (rank, note) means a slot from the free list has no idea which
     * rank it is about to sound. This is where it finds out.
     *
     * MUST NOT ALLOCATE. Storage is reserved at @ref prepare; a Tutti chord is
     * twenty-six of these inside one audio callback. RT-safe, @c noexcept.
     *
     * A voice that does not implement per-rank voicing may ignore it.
     */
    virtual void adoptRank(const void* voicing) noexcept = 0;

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
     *        budget to decide tier demotion. RT-safe.
     */
    [[nodiscard]] virtual float cpuCostEstimate() const noexcept = 0;

    /**
     * @brief How audible this voice is right now, as a linear level in [0, 1].
     *
     * The pool steals the LEAST audible voice when it runs out. It used to rank
     * candidates by @ref cpuCostEstimate as a stand-in for audibility, which is
     * no proxy at all when every voice carries the same composite spectrum and
     * therefore reports the same cost: the comparison never fired and the victim
     * was simply whichever slot had the lowest index — potentially a sustained
     * 16' pedal note under a passage.
     *
     * The default is a conservative 1.0 ("assume audible"), so a voice type that
     * has not implemented this is never preferentially sacrificed. RT-safe.
     */
    [[nodiscard]] virtual float levelEstimate() const noexcept { return 1.0f; }
};

} // namespace caecilia::core
