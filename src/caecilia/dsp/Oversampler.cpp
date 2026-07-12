/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/dsp/Oversampler.h"

#include "caecilia/dsp/DspMath.h"

#include <algorithm>

namespace caecilia::dsp
{

namespace
{
/// Round @p f up to the nearest power of two within [1, 8].
[[nodiscard]] std::size_t clampFactor(std::size_t f) noexcept
{
    if (f <= 1)
        return 1;
    if (f <= 2)
        return 2;
    if (f <= 4)
        return 4;
    return 8;
}
} // namespace

void Oversampler::prepare(std::size_t maxBlockFrames, std::size_t factor)
{
    factor_        = clampFactor(factor);
    maxBaseFrames_ = maxBlockFrames;

    work_.assign(maxBlockFrames * factor_, 0.0f);

    // One half-band interpolation/decimation state slot per doubling stage.
    std::size_t stages = 0;
    for (std::size_t f = factor_; f > 1; f >>= 1)
        ++stages;
    upState_.assign(stages == 0 ? 1 : stages, 0.0f);
    downState_.assign(stages == 0 ? 1 : stages, 0.0f);

    // A symmetric half-band FIR of this order has an integer group delay; report
    // a representative value so PDC is wired. TODO(phase6): compute exactly from
    // the designed taps.
    latencySamples_ = stages; // placeholder base-rate group delay
    reset();
}

std::size_t Oversampler::upsample(const float* input, std::size_t baseCount) noexcept
{
    const std::size_t clamped = std::min(baseCount, maxBaseFrames_);
    oversampledFrames_        = clamped * factor_;

    // TODO(phase6): replace this zero-stuff + linear pre-smoothing with the true
    // half-band interpolation cascade. This fill keeps the working buffer valid
    // and continuous so the nonlinear stage and downsample() are wired end-to-end.
    for (std::size_t i = 0; i < clamped; ++i)
    {
        const float x0 = input[i];
        const float x1 = (i + 1 < clamped) ? input[i + 1] : x0;
        for (std::size_t s = 0; s < factor_; ++s)
        {
            const float frac                = static_cast<float>(s) / static_cast<float>(factor_);
            work_[i * factor_ + s]          = x0 + (x1 - x0) * frac;
        }
    }
    return oversampledFrames_;
}

void Oversampler::downsample(float* output, std::size_t baseCount) noexcept
{
    const std::size_t clamped = std::min(baseCount, maxBaseFrames_);

    // TODO(phase6): replace point-decimation with the mirror half-band cascade so
    // images produced by the nonlinearity are properly rejected. Averaging the
    // factor_ sub-samples gives a gentle anti-image low-pass in the meantime.
    for (std::size_t i = 0; i < clamped; ++i)
    {
        float acc = 0.0f;
        for (std::size_t s = 0; s < factor_; ++s)
            acc += work_[i * factor_ + s];
        output[i] = flushDenormal(acc / static_cast<float>(factor_));
    }
}

void Oversampler::reset() noexcept
{
    std::fill(work_.begin(), work_.end(), 0.0f);
    std::fill(upState_.begin(), upState_.end(), 0.0f);
    std::fill(downState_.begin(), downState_.end(), 0.0f);
    oversampledFrames_ = 0;
}

} // namespace caecilia::dsp
