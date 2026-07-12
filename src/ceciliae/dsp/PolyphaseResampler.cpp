/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/dsp/PolyphaseResampler.h"

#include "ceciliae/dsp/DspMath.h"

#include <algorithm>

namespace ceciliae::dsp
{

namespace
{
[[nodiscard]] std::size_t gcd(std::size_t a, std::size_t b) noexcept
{
    while (b != 0)
    {
        const std::size_t t = a % b;
        a                   = b;
        b                   = t;
    }
    return a;
}
} // namespace

void PolyphaseResampler::prepare(std::size_t interpFactor,
                                 std::size_t decimFactor,
                                 std::size_t tapsPerPhase)
{
    interp_       = interpFactor == 0 ? 1 : interpFactor;
    decim_        = decimFactor == 0 ? 1 : decimFactor;
    tapsPerPhase_ = tapsPerPhase == 0 ? 1 : tapsPerPhase;

    // Reduce the ratio so the polyphase decomposition is minimal.
    const std::size_t g = gcd(interp_, decim_);
    interp_ /= g;
    decim_ /= g;

    const std::size_t protoLen = tapsPerPhase_ * interp_;
    prototype_.assign(protoLen, 0.0f);

    // Windowed-sinc prototype low-pass. Cutoff = min(1/L, 1/M) of the upsampled
    // Nyquist so we suppress both images (from upsampling) and pre-aliasing
    // (before decimation). Scale by L to preserve gain across zero-stuffing.
    const double cutoff = 1.0 / static_cast<double>(std::max(interp_, decim_));
    const double centre = static_cast<double>(protoLen - 1) * 0.5;
    double       sum    = 0.0;
    for (std::size_t n = 0; n < protoLen; ++n)
    {
        const double t = static_cast<double>(n) - centre;
        // Kaiser-windowed sinc; cutoff expressed as a fraction of the (upsampled)
        // Nyquist, so the sinc argument is 2*cutoff*t.
        const double h = 2.0 * cutoff * sinc(2.0 * cutoff * t)
                         * kaiserWindow(t / (centre + 1.0), 8.6);
        prototype_[n] = static_cast<float>(h * static_cast<double>(interp_));
        sum += h;
    }
    // Normalise to unity DC (per interpolation gain already folded in above).
    if (sum != 0.0)
    {
        const float inv = static_cast<float>(1.0 / (sum * static_cast<double>(interp_)));
        for (auto& c : prototype_)
            c *= inv * static_cast<float>(interp_);
    }

    history_.assign(tapsPerPhase_, 0.0f);
    writePos_ = 0;
    phase_    = 0;
}

std::size_t PolyphaseResampler::process(const float* input,
                                        std::size_t  inCount,
                                        float*       output,
                                        std::size_t  outCapacity) noexcept
{
    if (prototype_.empty() || history_.empty())
        return 0;

    std::size_t produced = 0;

    // TODO(phase3): replace this with the full polyphase commutator that walks
    // the L sub-filters and advances by M each output. The structure below keeps
    // correct history, honours the L/M output-length ratio, and low-pass filters
    // via the prototype so the graph is wired and audible, if not yet optimal.
    for (std::size_t i = 0; i < inCount && produced < outCapacity; ++i)
    {
        history_[writePos_] = input[i];
        writePos_           = (writePos_ + 1) % history_.size();

        // Emit an output sample every time the fractional phase crosses M/L.
        phase_ += interp_;
        while (phase_ >= decim_ && produced < outCapacity)
        {
            phase_ -= decim_;

            float acc = 0.0f;
            for (std::size_t k = 0; k < tapsPerPhase_; ++k)
            {
                const std::size_t idx =
                    (writePos_ + history_.size() - 1 - k) % history_.size();
                const std::size_t protoTap = k * interp_;
                if (protoTap < prototype_.size())
                    acc += history_[idx] * prototype_[protoTap];
            }
            output[produced++] = flushDenormal(acc);
        }
    }

    return produced;
}

void PolyphaseResampler::reset() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
    writePos_ = 0;
    phase_    = 0;
}

} // namespace ceciliae::dsp
