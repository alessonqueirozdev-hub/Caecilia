/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/OnePole.h"

#include "caecilia/dsp/DspMath.h"

#include <cmath>

namespace caecilia::dsp
{

void OnePole::setLowpass(float cutoffHz) noexcept
{
    const double fs = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
    const double fc = clamp(static_cast<double>(cutoffHz), 1.0, fs * 0.49);
    // Pole from the impulse-invariant one-pole low-pass: a = exp(-2*pi*fc/fs).
    const double a = std::exp(-kTwoPi * fc / fs);
    b0_            = static_cast<float>(1.0 - a);
    b1_            = 0.0f;
    a1_            = static_cast<float>(-a);
}

void OnePole::setHighpass(float cutoffHz) noexcept
{
    const double fs = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
    const double fc = clamp(static_cast<double>(cutoffHz), 1.0, fs * 0.49);
    const double a  = std::exp(-kTwoPi * fc / fs);
    // Complementary one-pole/one-zero high-pass sharing the same pole.
    const double g = 0.5 * (1.0 + a);
    b0_            = static_cast<float>(g);
    b1_            = static_cast<float>(-g);
    a1_            = static_cast<float>(-a);
}

} // namespace caecilia::dsp
