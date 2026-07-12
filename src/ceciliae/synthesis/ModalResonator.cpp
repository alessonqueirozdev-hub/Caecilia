/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/synthesis/ModalResonator.h"

#include <cmath>

namespace ceciliae::synth
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Harmonic-ish mode ratios and decreasing mix weights for a reed-like body.
    constexpr std::array<float, ModalResonator::kMaxModes> kModeRatios{
        1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    constexpr std::array<float, ModalResonator::kMaxModes> kModeGains{
        1.0f, 0.7f, 0.5f, 0.4f, 0.3f, 0.24f, 0.18f, 0.14f };
} // namespace

void ModalResonator::prepare(core::SampleRate sampleRate, std::size_t /*maxBlockFrames*/)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    modeCount_  = kMaxModes;
    reset();
}

void ModalResonator::setFrequency(double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0)
        return;

    const double sr      = sampleRate_ > 1.0 ? sampleRate_ : 48000.0;
    const double nyquist = 0.5 * sr;

    for (std::size_t i = 0; i < kMaxModes; ++i)
    {
        Mode& m = modes_[i];

        const double modeHz = frequencyHz * static_cast<double>(kModeRatios[i]);
        if (modeHz >= nyquist)
        {
            // Mode above Nyquist: silence it to avoid aliasing.
            m.b0 = m.a1 = m.a2 = m.gain = 0.0f;
            continue;
        }

        // Two-pole resonator: pole radius r sets decay, angle w sets pitch.
        const double w = 2.0 * kPi * modeHz / sr;
        const double r = std::exp(-kPi * (modeHz * 0.01 + 2.0) / sr); // gentle, freq-scaled damping

        m.a1   = static_cast<float>(-2.0 * r * std::cos(w));
        m.a2   = static_cast<float>(r * r);
        m.b0   = static_cast<float>(1.0 - r);   // unity-ish resonant gain
        m.gain = kModeGains[i];
    }
}

float ModalResonator::process(float excitation) noexcept
{
    float acc = 0.0f;
    for (std::size_t i = 0; i < kMaxModes; ++i)
    {
        Mode& m = modes_[i];
        // Direct-form-II transposed two-pole: y = b0*x - a1*z1 - a2*z2.
        const float y = m.b0 * excitation - m.a1 * m.z1 - m.a2 * m.z2;
        m.z2 = m.z1;
        m.z1 = y;
        acc += m.gain * y;
    }
    return acc;
}

void ModalResonator::reset() noexcept
{
    for (Mode& m : modes_)
    {
        m.z1 = 0.0f;
        m.z2 = 0.0f;
    }
}

} // namespace ceciliae::synth
