/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>

namespace caecilia::synth
{

/**
 * @brief A nonlinear, wind-driven excitation source for the physical voice tier.
 *
 * The excitation is where chiff and speech EMERGE: a flue's jet takes a few
 * milliseconds to establish, a reed's tongue takes time to lock to the
 * resonator, and both are functions of the instantaneous wind pressure rather
 * than a stored transient. Concrete implementations include @ref NonlinearJet
 * (flues) and @ref BeatingReed (reeds).
 *
 * The excitation runs inside an oversampled polyphase island (set up by the
 * @c dsp module in a later phase) with bandlimited residuals, so its nonlinear
 * output does not alias.
 *
 * ## Real-time contract
 * - @ref prepare allocates/precomputes; not RT-safe.
 * - @ref trigger, @ref setWind, @ref process, @ref reset, @ref isSettled are
 *   audio-thread methods: allocation-free, lock-free, @c noexcept.
 */
class IExcitation
{
public:
    virtual ~IExcitation() = default;

    /// One-time allocation / precompute. Not RT-safe.
    virtual void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) = 0;

    /**
     * @brief Arm the excitation for a note.
     * @param frequencyHz Target sounding frequency.
     * @param velocity    MIDI velocity in [1, 127].
     */
    virtual void trigger(double frequencyHz, core::Velocity velocity) noexcept = 0;

    /**
     * @brief Update the driving wind for the current sample/block.
     * @param pressurePa        Absolute chest pressure in pascals.
     * @param pressureDeviation Normalised deviation (actual - nominal)/nominal.
     *
     * This is the coupling that makes the pipe breathe; sag reduces drive,
     * tremulant modulates it (AM+FM+timbral) because it modulates the driver.
     */
    virtual void setWind(float pressurePa, float pressureDeviation) noexcept = 0;

    /// @return One sample of excitation signal. RT-safe, @c noexcept.
    [[nodiscard]] virtual float process() noexcept = 0;

    /// Clear internal state. RT-safe.
    virtual void reset() noexcept = 0;

    /// @return true once the startup transient (chiff/speech) has settled. RT-safe.
    [[nodiscard]] virtual bool isSettled() const noexcept = 0;
};

} // namespace caecilia::synth
