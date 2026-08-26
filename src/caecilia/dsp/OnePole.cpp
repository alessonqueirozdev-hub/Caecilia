// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/OnePole.h"

#include "caecilia/dsp/DspMath.h"

#include <cmath>

namespace caecilia::dsp
{

OnePoleCoeffs OnePoleCoeffs::lowpass(core::SampleRate sampleRate, float cutoffHz) noexcept
{
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double fc = clamp(static_cast<double>(cutoffHz), 1.0, fs * 0.49);
    // Pole from the impulse-invariant one-pole low-pass: a = exp(-2*pi*fc/fs).
    const double a = std::exp(-kTwoPi * fc / fs);

    OnePoleCoeffs c;
    c.b0 = static_cast<float>(1.0 - a);
    c.b1 = 0.0f;
    c.a1 = static_cast<float>(-a);
    return c;
}

OnePoleCoeffs OnePoleCoeffs::highpass(core::SampleRate sampleRate, float cutoffHz) noexcept
{
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double fc = clamp(static_cast<double>(cutoffHz), 1.0, fs * 0.49);
    const double a  = std::exp(-kTwoPi * fc / fs);
    // Complementary one-pole/one-zero high-pass sharing the same pole.
    const double g = 0.5 * (1.0 + a);

    OnePoleCoeffs c;
    c.b0 = static_cast<float>(g);
    c.b1 = static_cast<float>(-g);
    c.a1 = static_cast<float>(-a);
    return c;
}

// The two single-filter setters are now thin wrappers, so there is exactly one
// place where a one-pole response is derived and no way for the bank path and
// the single path to disagree.
void OnePole::setLowpass(float cutoffHz) noexcept
{
    setCoeffs(OnePoleCoeffs::lowpass(sampleRate_, cutoffHz));
}

void OnePole::setHighpass(float cutoffHz) noexcept
{
    setCoeffs(OnePoleCoeffs::highpass(sampleRate_, cutoffHz));
}

} // namespace caecilia::dsp
