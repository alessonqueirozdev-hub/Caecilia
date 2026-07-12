/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/synthesis/IExcitation.h"

#include <cstddef>
#include <cstdint>

namespace ceciliae::synth
{

/**
 * @brief Nonlinear air-jet excitation for flue pipes (Principal / Flute / String).
 *
 * Models the oscillating jet of air striking the labium: the jet takes a few
 * milliseconds to establish against the pipe resonance, and THAT startup is
 * where a flue's chiff emerges — never a canned transient. Drive is a nonlinear
 * function of the instantaneous wind pressure, so sag and tremulant couple into
 * pitch, level and brightness together.
 *
 * @note Phase-0 scaffold: the DSP body is a documented placeholder that returns
 *       silence and only advances the settle timer, so the physical voice tier
 *       is structurally complete. The bandlimited nonlinear jet lands in the
 *       oversampled island in phase 6.
 */
class NonlinearJet final : public IExcitation
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
    float         phase_         = 0.0f;
    std::uint32_t settleCounter_ = 0;   ///< Samples since trigger.
    std::uint32_t settleTarget_  = 0;   ///< Samples until the jet is considered settled.
};

} // namespace ceciliae::synth
