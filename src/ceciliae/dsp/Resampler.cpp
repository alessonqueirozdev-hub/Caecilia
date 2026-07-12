/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/dsp/Resampler.h"

#include "ceciliae/dsp/DspMath.h"

#include <cmath>

namespace ceciliae::dsp
{

void Resampler::prepare(Quality quality, std::size_t numPhases)
{
    quality_ = quality;
    interp_.build(numPhases, 8.6);
}

float Resampler::interpolateAt(const float* source, std::size_t length, double pos) const noexcept
{
    if (source == nullptr || length == 0 || pos < 0.0)
        return 0.0f;

    const double  floorPos = std::floor(pos);
    const auto    i0       = static_cast<long long>(floorPos);
    const float   frac     = static_cast<float>(pos - floorPos);

    if (quality_ == Quality::Linear || !interp_.isBuilt())
    {
        const std::size_t a = static_cast<std::size_t>(i0 < 0 ? 0 : i0);
        if (a >= length)
            return 0.0f;
        const std::size_t b  = (a + 1 < length) ? a + 1 : a;
        return source[a] + (source[b] - source[a]) * frac;
    }

    // Gather the 16 taps centred on the interpolation point, clamping indices to
    // the valid range so edges are handled without reading out of bounds.
    constexpr int kTaps  = static_cast<int>(SincKaiser16Interpolator::kTaps);
    constexpr int kStart = -(kTaps / 2 - 1); // -7 .. +8 around i0
    float         window[SincKaiser16Interpolator::kTaps];
    for (int t = 0; t < kTaps; ++t)
    {
        long long idx = i0 + kStart + t;
        if (idx < 0)
            idx = 0;
        else if (idx >= static_cast<long long>(length))
            idx = static_cast<long long>(length) - 1;
        window[t] = source[static_cast<std::size_t>(idx)];
    }
    return interp_.process(window, frac);
}

void Resampler::readBlock(const float* source,
                          std::size_t  length,
                          double&      readPos,
                          double       ratio,
                          float*       output,
                          std::size_t  outCount) const noexcept
{
    for (std::size_t i = 0; i < outCount; ++i)
    {
        if (readPos >= static_cast<double>(length))
        {
            output[i] = 0.0f; // Ran past the source: emit silence.
            continue;
        }
        output[i] = interpolateAt(source, length, readPos);
        readPos += ratio;
    }
}

} // namespace ceciliae::dsp
