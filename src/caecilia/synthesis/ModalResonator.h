/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/IResonator.h"

#include <array>
#include <cstddef>

namespace caecilia::synth
{

/**
 * @brief A bank of tuned two-pole modes modelling a reed pipe's resonator.
 *
 * Reeds are well described by a set of resonant modes with characteristic ratios
 * and decays. Each mode is a stable two-pole resonator (written fresh from
 * standard biquad/resonator math, no GPL DSP); the excitation drives them in
 * parallel and their outputs are summed. Character comes from the nonlinear
 * excitation (@ref BeatingReed) and the wind, not the resonator, which stays
 * linear and unconditionally stable.
 *
 * ## Real-time contract
 * - @ref prepare precomputes; not RT-safe. Storage is a fixed @c std::array, so
 *   nothing on the hot path allocates.
 * - @ref setFrequency, @ref process, @ref reset are @c noexcept, allocation-free.
 */
class ModalResonator final : public IResonator
{
public:
    /// Number of modes in the bank.
    static constexpr std::size_t kMaxModes = 8;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void setFrequency(double frequencyHz) noexcept override;
    [[nodiscard]] float process(float excitation) noexcept override;
    void reset() noexcept override;

private:
    struct Mode
    {
        float b0 = 0.0f; ///< Input gain.
        float a1 = 0.0f; ///< -2 r cos(w).
        float a2 = 0.0f; ///< r^2.
        float z1 = 0.0f; ///< y[n-1].
        float z2 = 0.0f; ///< y[n-2].
        float gain = 0.0f;///< Output mix weight.
    };

    std::array<Mode, kMaxModes> modes_{};
    std::size_t modeCount_  = kMaxModes;
    double      sampleRate_ = 0.0;
};

} // namespace caecilia::synth
