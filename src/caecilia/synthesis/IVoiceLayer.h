/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <cstddef>

namespace caecilia::synth
{

/**
 * @brief One stage of a voice's layered pipeline (attack, sustain or release).
 *
 * Realism in Caecilia lives in composition: a voice is an AttackLayer spliced
 * into a modeled SustainLayer plus a ReleaseLayer, all sharing one spectral seed
 * and one per-pipe voicing. IVoiceLayer is the common contract those stages
 * implement so a voice can drive them uniformly.
 *
 * ## Real-time contract
 * - @ref prepare is the only allocating method; it runs off the audio thread.
 * - @ref trigger, @ref release, @ref renderAdd and @ref isActive are audio-thread
 *   methods and MUST be allocation-free, lock-free and @c noexcept.
 * - @ref renderAdd accumulates (+=) into the block; it never overwrites.
 */
class IVoiceLayer
{
public:
    virtual ~IVoiceLayer() = default;

    /// One-time allocation / precompute. Not RT-safe; call off the audio thread.
    virtual void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) = 0;

    /**
     * @brief Start this layer sounding.
     * @param pipe       Stable pipe identity (voicing/seed source).
     * @param velocity   MIDI velocity in [1, 127].
     * @param frequencyHz Resolved sounding frequency for the pipe.
     */
    virtual void trigger(core::PipeId pipe, core::Velocity velocity, double frequencyHz) noexcept = 0;

    /// Request the layer begin its release / fade-out. RT-safe.
    virtual void release() noexcept = 0;

    /// Accumulate this layer's output into @p block. RT-safe, @c noexcept.
    virtual void renderAdd(core::AudioBlock& block) noexcept = 0;

    /// @return true while the layer still produces audio. RT-safe.
    [[nodiscard]] virtual bool isActive() const noexcept = 0;
};

/**
 * @brief A read-only provider of recorded sample data for an attack (or a full
 *        pure-sample voice), in Caecilia's own proprietary sample format.
 *
 * The concrete streaming/loading lives in the @c model module (off the audio
 * thread); the RT path only reads frames through this interface, which a
 * resampler drives. Caecilia prefers a loop-free modeled sustain, so
 * @ref loopEnd may be 0 to signal a one-shot recording whose tail hands off to a
 * modeled sustain.
 *
 * Real-time contract: every accessor is @c noexcept and allocation-free.
 */
class ISampleSource
{
public:
    virtual ~ISampleSource() = default;

    /// @return Channel count of the recording (1 = mono, 2 = stereo).
    [[nodiscard]] virtual std::size_t channelCount() const noexcept = 0;

    /// @return Sample rate the recording was captured at, in Hz.
    [[nodiscard]] virtual double sourceSampleRate() const noexcept = 0;

    /// @return Recorded pitch of the sample in Hz (for pitch-ratio resampling).
    [[nodiscard]] virtual double rootFrequencyHz() const noexcept = 0;

    /// @return Total number of frames available in the recording.
    [[nodiscard]] virtual std::size_t frameCount() const noexcept = 0;

    /// @return First frame of the sustain loop region.
    [[nodiscard]] virtual std::size_t loopStart() const noexcept = 0;

    /// @return One-past-last frame of the loop region; 0 signals a one-shot.
    [[nodiscard]] virtual std::size_t loopEnd() const noexcept = 0;

    /**
     * @brief Read one raw (un-interpolated) sample.
     * @param channel    Channel index in [0, channelCount()).
     * @param frameIndex Frame index in [0, frameCount()).
     * @return The sample value, or 0 for out-of-range requests.
     *
     * Interpolation is the caller's job (a resampler); this is a bounds-checked
     * point accessor. RT-safe.
     */
    [[nodiscard]] virtual float sampleAt(std::size_t channel, std::size_t frameIndex) const noexcept = 0;
};

/**
 * @brief A loop-free modeled sustain layer that can be seeded from a
 *        @ref SpectralModel so its timbre matches a recorded attack.
 *
 * This is the heart of the hybrid: instead of looping a recording (which kills
 * the note's life), the sustain is reconstructed from tracked partials that
 * remain functions of the wind supply. @ref seedFrom copies the analysed timbre
 * into pre-allocated storage so it is RT-safe.
 */
class IModeledSustain : public IVoiceLayer
{
public:
    /**
     * @brief Seed the modeled sustain's timbre from an analysed spectral model.
     * @param model           The partials/formants to reconstruct.
     * @param phaseAlignSeconds Time offset used to phase-align with a spliced attack.
     *
     * RT-safe: fills only pre-allocated storage reserved in @ref prepare; never
     * allocates. Partials beyond the reserved capacity are dropped.
     */
    virtual void seedFrom(const SpectralModel& model, float phaseAlignSeconds) noexcept = 0;
};

} // namespace caecilia::synth
