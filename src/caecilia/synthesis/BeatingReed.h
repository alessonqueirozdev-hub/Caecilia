/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/IExcitation.h"

#include <cstddef>
#include <cstdint>

namespace caecilia::synth
{

/**
 * @brief Beating-reed excitation for reed pipes (Trumpet, Oboe, Krummhorn, ...).
 *
 * Models a brass/reed tongue beating against a shallot: the tongue's motion is a
 * strongly nonlinear function of pressure, and its lock-in to the resonator
 * during onset produces the reed's characteristic speech. Pitch is markedly more
 * pressure-sensitive than a flue's, which is why reeds ride a much steeper
 * @ref WindResponseCurve.
 *
 * @note Phase-0 scaffold: the DSP body is a documented placeholder returning
 *       silence while advancing the settle timer; the bandlimited reed model
 *       (paired with @ref ModalResonator) lands in phase 6.
 */
class BeatingReed final : public IExcitation
{
public:
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void trigger(double frequencyHz, core::Velocity velocity) noexcept override;
    void setWind(float pressurePa, float pressureDeviation) noexcept override;
    [[nodiscard]] float process() noexcept override;
    void reset() noexcept override;
    [[nodiscard]] bool isSettled() const noexcept override;

private:
    double        sampleRate_    = 0.0;
    double        frequencyHz_   = 0.0;
    float         pressurePa_    = 0.0f;
    float         deviation_     = 0.0f;
    float         drive_         = 0.0f;
    float         tonguePos_     = 0.0f; ///< Reed tongue displacement state.
    std::uint32_t settleCounter_ = 0;
    std::uint32_t settleTarget_  = 0;
};

} // namespace caecilia::synth
