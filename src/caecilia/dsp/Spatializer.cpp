/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/Spatializer.h"

#include "caecilia/dsp/DspMath.h"

#include <algorithm>

namespace caecilia::dsp
{

void Spatializer::prepare(core::SampleRate sampleRate, float maxDepthMs)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto maxLen =
        static_cast<std::size_t>(std::max(maxDepthMs, 0.0f) * 0.001f * sampleRate_) + 1;
    preDelay_.assign(std::max<std::size_t>(maxLen, 2), 0.0f);
    preDelayPos_ = 0;
    preDelayLen_ = 0;
}

void Spatializer::setPlacement(const PipeSpatialParams& params) noexcept
{
    equalPowerPan(params.panNorm, gainLeft_, gainRight_);

    // Distance attenuation: a simple 1/(1 + k*d) roll-off keeps it monotonic and
    // bounded. TODO(phase4): add an air-absorption low-pass proportional to d.
    const float dist = clamp(params.distanceNorm, 0.0f, 1.0f);
    const float atten = 1.0f / (1.0f + 1.5f * dist);
    gainLeft_ *= atten;
    gainRight_ *= atten;

    earlyLevel_ = clamp(params.earlyLevel, 0.0f, 1.0f);

    if (!preDelay_.empty())
    {
        const auto req =
            static_cast<std::size_t>(std::max(params.depthMs, 0.0f) * 0.001f * sampleRate_);
        preDelayLen_ = clamp<std::size_t>(req, 0, preDelay_.size() - 1);
    }
}

void Spatializer::processAdd(const float* mono,
                             std::size_t  count,
                             float*       left,
                             float*       right) noexcept
{
    if (preDelay_.empty())
        return;

    for (std::size_t n = 0; n < count; ++n)
    {
        const float x = mono[n];

        // Depth-delayed early reflection tap.
        float early = 0.0f;
        if (preDelayLen_ > 0 && earlyLevel_ > 0.0f)
        {
            const std::size_t r =
                (preDelayPos_ + preDelay_.size() - preDelayLen_) % preDelay_.size();
            early = preDelay_[r] * earlyLevel_;
        }
        preDelay_[preDelayPos_] = x;
        preDelayPos_            = (preDelayPos_ + 1) % preDelay_.size();

        const float s = x + early;
        left[n] += s * gainLeft_;
        right[n] += s * gainRight_;
    }
}

void Spatializer::processReplace(const float* mono,
                                 std::size_t  count,
                                 float*       left,
                                 float*       right) noexcept
{
    for (std::size_t n = 0; n < count; ++n)
    {
        left[n]  = 0.0f;
        right[n] = 0.0f;
    }
    processAdd(mono, count, left, right);
}

void Spatializer::reset() noexcept
{
    std::fill(preDelay_.begin(), preDelay_.end(), 0.0f);
    preDelayPos_ = 0;
}

} // namespace caecilia::dsp
