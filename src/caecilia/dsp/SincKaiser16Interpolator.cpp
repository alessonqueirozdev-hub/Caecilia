// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/SincKaiser16Interpolator.h"

#include "caecilia/dsp/DspMath.h"
#include "caecilia/dsp/Kernels.h"

namespace caecilia::dsp
{

void SincKaiser16Interpolator::build(std::size_t numPhases, double kaiserBeta)
{
    if (numPhases == 0)
        numPhases = 1;

    numPhases_ = numPhases;
    table_.assign(numPhases_ * kTaps, 0.0f);

    constexpr double halfWidth = static_cast<double>(kTaps) / 2.0; // 8.0
    // The interpolation point sits between taps (kTaps/2 - 1) and (kTaps/2).
    // For a fractional offset `frac`, tap j approximates the ideal sinc sample
    // at argument (j - (centre + frac)), windowed by a Kaiser of half-width 8.
    const double centre = halfWidth - 1.0; // 7.0

    for (std::size_t p = 0; p < numPhases_; ++p)
    {
        const double frac = static_cast<double>(p) / static_cast<double>(numPhases_);
        double       sum  = 0.0;
        for (std::size_t j = 0; j < kTaps; ++j)
        {
            const double arg = static_cast<double>(j) - (centre + frac);
            const double w   = kaiserWindow(arg / halfWidth, kaiserBeta);
            const double h   = sinc(arg) * w;
            table_[p * kTaps + j] = static_cast<float>(h);
            sum += h;
        }
        // Normalise each phase row to unity DC gain so a constant signal passes
        // through untouched regardless of the truncation window.
        if (sum != 0.0)
        {
            const float inv = static_cast<float>(1.0 / sum);
            for (std::size_t j = 0; j < kTaps; ++j)
                table_[p * kTaps + j] *= inv;
        }
    }
}

float SincKaiser16Interpolator::process(const float* x16, float frac) const noexcept
{
    if (numPhases_ == 0)
        return 0.0f;

    float f = frac;
    if (f < 0.0f)
        f = 0.0f;
    else if (f >= 1.0f)
        f = 0.999999f;

    std::size_t phase = static_cast<std::size_t>(f * static_cast<float>(numPhases_));
    if (phase >= numPhases_)
        phase = numPhases_ - 1;

    return kernels::dotProduct(&table_[phase * kTaps], x16, kTaps);
}

} // namespace caecilia::dsp
