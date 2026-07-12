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
 * @brief The resonant body an @ref IExcitation drives in the physical voice tier.
 *
 * For flues this is a @ref Waveguide (a tuned bidirectional delay line modelling
 * the air column); for reeds a @ref ModalResonator (a bank of tuned modes). The
 * resonator is linear and stable; all the character comes from the nonlinear
 * coupling with the excitation and the wind.
 *
 * ## Real-time contract
 * - @ref prepare allocates the delay line / mode bank; not RT-safe.
 * - @ref setFrequency, @ref process, @ref reset are @c noexcept, allocation-free.
 */
class IResonator
{
public:
    virtual ~IResonator() = default;

    /// One-time allocation / precompute. Not RT-safe.
    virtual void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) = 0;

    /// Tune the resonator to a sounding frequency (Hz). RT-safe.
    virtual void setFrequency(double frequencyHz) noexcept = 0;

    /**
     * @brief Advance the resonator by one sample.
     * @param excitation The excitation input for this sample.
     * @return The resonator's output sample. RT-safe, @c noexcept.
     */
    [[nodiscard]] virtual float process(float excitation) noexcept = 0;

    /// Clear internal state (delay line / mode memory). RT-safe.
    virtual void reset() noexcept = 0;
};

} // namespace caecilia::synth
