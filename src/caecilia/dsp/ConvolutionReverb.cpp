// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/ConvolutionReverb.h"

#include "caecilia/dsp/DspMath.h"

#include <algorithm>

namespace caecilia::dsp
{

void ConvolutionReverb::prepare(core::SampleRate sampleRate,
                                std::size_t      maxBlockFrames,
                                std::size_t      numChannels)
{
    sampleRate_     = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockFrames_ = maxBlockFrames;
    numChannels_    = numChannels == 0 ? 1 : numChannels;

    // TODO(phase9): choose the uniform partition size (a power of two around the
    // host block size) and pre-plan the FFT. The direct-form fallback below has
    // zero added latency, so report 0 until the partitioned path lands.
    partitionSize_  = maxBlockFrames_;
    latencySamples_ = 0;

    if (!ir_.empty())
        history_.assign(ir_.size(), 0.0f);
    historyPos_ = 0;
}

void ConvolutionReverb::setParams(const core::ReverbParams& params) noexcept
{
    // Same clamp as the FDN path, so params() reports a legal set whichever
    // reverb a caller is holding — and so one NaN cannot multiply the output away.
    params_ = core::clampReverbParams(params, sampleRate_);
    // TODO(phase9): apply preDelayMs / widthNorm taps once the partitioned engine
    // exposes per-channel output routing.
}

void ConvolutionReverb::loadImpulseResponse(const float* ir, std::size_t length)
{
    if (ir == nullptr || length == 0)
    {
        ir_.clear();
        history_.clear();
        historyPos_ = 0;
        return;
    }

    ir_.assign(ir, ir + length);
    history_.assign(length, 0.0f);
    historyPos_ = 0;

    // TODO(phase9): split ir_ into uniform partitions and forward-transform each.
}

void ConvolutionReverb::process(core::AudioBlock& block) noexcept
{
    if (block.isEmpty())
        return;

    // With no IR loaded the reverb is a transparent pass-through.
    if (ir_.empty() || history_.empty())
        return;

    const std::size_t frames   = block.numFrames();
    const std::size_t channels = block.numChannels();
    const std::size_t irLen    = ir_.size();
    const float       wet      = params_.mix;
    const float       dry      = 1.0f - params_.mix;

    // TODO(phase9): replace this direct O(N*L) time-domain convolution (correct,
    // but only cheap for short IRs) with uniform-partitioned FFT overlap-add.
    // The shared mono history is driven by channel 0 so the structure is wired.
    float* ch0 = block.channel(0);
    for (std::size_t n = 0; n < frames; ++n)
    {
        history_[historyPos_] = ch0[n];

        float acc = 0.0f;
        std::size_t idx = historyPos_;
        for (std::size_t k = 0; k < irLen; ++k)
        {
            acc += ir_[k] * history_[idx];
            idx = (idx == 0) ? irLen - 1 : idx - 1;
        }

        ch0[n]      = dry * ch0[n] + wet * flushDenormal(acc);
        historyPos_ = (historyPos_ + 1) % irLen;
    }

    // Copy the processed channel-0 result to remaining channels for now.
    for (std::size_t c = 1; c < channels; ++c)
    {
        float* dst = block.channel(c);
        for (std::size_t n = 0; n < frames; ++n)
            dst[n] = ch0[n];
    }
}

void ConvolutionReverb::reset() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
    historyPos_ = 0;
}

} // namespace caecilia::dsp
