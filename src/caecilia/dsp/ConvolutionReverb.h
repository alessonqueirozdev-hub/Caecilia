/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/IReverb.h"

#include <cstddef>
#include <vector>

/**
 * @file ConvolutionReverb.h
 * @brief An impulse-response (convolution) reverberator.
 *
 * The second reverb path: convolves the signal with a measured room impulse
 * response for maximum realism where a specific acoustic must be reproduced. The
 * production design is uniform-partitioned block convolution (an FFT per
 * partition, overlap-add across partitions) so latency stays at one partition
 * while long tails remain affordable. The impulse response is owned by this
 * object and loaded off the audio thread.
 *
 * Only Caecilia's own / public-domain impulse responses are used; no GPL sample
 * set or GPL convolution code is referenced.
 */

namespace caecilia::dsp
{

/**
 * @brief Convolution reverb implementing @ref core::IReverb.
 *
 * ## Real-time contract
 * - @ref prepare and @ref loadImpulseResponse allocate the IR store and the
 *   partition/overlap buffers. Not RT-safe.
 * - @ref setParams updates the wet/dry and pre-delay taps. RT-safe.
 * - @ref process convolves in place. RT-safe, @c noexcept, allocation-free.
 * - @ref latencySamples reports the first-partition delay for host PDC.
 */
class ConvolutionReverb final : public core::IReverb
{
public:
    ConvolutionReverb() = default;
    ~ConvolutionReverb() override = default;

    // --- core::IReverb -----------------------------------------------------
    void        prepare(core::SampleRate sampleRate,
                        std::size_t      maxBlockFrames,
                        std::size_t      numChannels) override;
    void        setParams(const core::ReverbParams& params) noexcept override;
    void        process(core::AudioBlock& block) noexcept override;
    void        reset() noexcept override;
    [[nodiscard]] std::size_t latencySamples() const noexcept override { return latencySamples_; }

    // --- Convolution-specific ---------------------------------------------

    /**
     * @brief Install a (mono) impulse response used for every channel.
     * @param ir     Impulse-response samples.
     * @param length Number of samples in @p ir.
     *
     * Copies and partitions the IR. Must run off the audio thread; not RT-safe.
     * Passing @p length 0 clears the IR (the reverb then passes audio through dry).
     */
    void loadImpulseResponse(const float* ir, std::size_t length);

    /// @return true once a non-empty IR has been loaded.
    [[nodiscard]] bool hasImpulseResponse() const noexcept { return !ir_.empty(); }

    /// @return The loaded IR length in samples.
    [[nodiscard]] std::size_t impulseLength() const noexcept { return ir_.size(); }

private:
    core::SampleRate   sampleRate_     = 44100.0;
    std::size_t        maxBlockFrames_ = 0;
    std::size_t        numChannels_    = 2;
    std::size_t        partitionSize_  = 0;
    std::size_t        latencySamples_ = 0;
    core::ReverbParams params_{};

    std::vector<float> ir_;         ///< Copied impulse response (mono).
    std::vector<float> history_;    ///< Sliding input history for the convolution.
    std::size_t        historyPos_ = 0;
};

} // namespace caecilia::dsp
