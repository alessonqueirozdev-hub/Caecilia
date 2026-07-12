/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"

#include <cstddef>
#include <vector>

/**
 * @file Oversampler.h
 * @brief Power-of-two oversampling for the nonlinear synthesis island.
 *
 * Nonlinear stages (jet saturation, beating-reed clipping) create harmonics that
 * would alias at the base rate. This oversampler runs a cascade of half-band
 * stages to raise the rate by a power of two, hands the caller a wider working
 * buffer to apply the nonlinearity in, then decimates back through the mirror
 * cascade. Half-band designs are the efficient choice because every other
 * polyphase coefficient is zero. Built fresh from public windowed-sinc math.
 */

namespace caecilia::dsp
{

/**
 * @brief 1x/2x/4x/8x oversampling wrapper around an in-place nonlinearity.
 *
 * Usage: @ref upsample a base-rate block into the internal buffer, apply the
 * nonlinearity to @ref oversampledData across @ref oversampledFrames samples,
 * then @ref downsample back into the base-rate block.
 *
 * Real-time contract: @ref prepare allocates the working buffers and designs the
 * half-band filters (not RT-safe). @ref upsample, @ref downsample and @ref reset
 * are @c noexcept and allocation-free.
 */
class Oversampler
{
public:
    Oversampler() = default;

    /**
     * @brief Allocate buffers and design the half-band cascade.
     * @param maxBlockFrames Largest base-rate block that will be processed.
     * @param factor         Oversampling factor; rounded to a power of two in [1, 8].
     *
     * Not RT-safe.
     */
    void prepare(std::size_t maxBlockFrames, std::size_t factor);

    /// @return The effective oversampling factor (power of two).
    [[nodiscard]] std::size_t factor() const noexcept { return factor_; }

    /**
     * @brief Upsample @p baseCount base-rate samples into the internal buffer.
     * @param input     Base-rate samples (single channel).
     * @param baseCount Number of base-rate samples (<= maxBlockFrames).
     * @return Number of oversampled samples now available (baseCount * factor).
     *
     * RT-safe, @c noexcept.
     */
    std::size_t upsample(const float* input, std::size_t baseCount) noexcept;

    /**
     * @brief Decimate the internal buffer back to base rate into @p output.
     * @param output    Destination for @p baseCount base-rate samples.
     * @param baseCount Number of base-rate samples to reconstruct.
     *
     * RT-safe, @c noexcept.
     */
    void downsample(float* output, std::size_t baseCount) noexcept;

    /// @return Writable pointer to the oversampled working buffer. RT-safe.
    [[nodiscard]] float* oversampledData() noexcept { return work_.data(); }

    /// @return Number of valid oversampled samples after the last @ref upsample.
    [[nodiscard]] std::size_t oversampledFrames() const noexcept { return oversampledFrames_; }

    /// Clear the half-band filter memory. RT-safe.
    void reset() noexcept;

    /**
     * @brief Group delay introduced by the half-band cascade, in BASE-rate samples.
     *
     * Reported to the host for plugin delay compensation. RT-safe.
     */
    [[nodiscard]] std::size_t latencySamples() const noexcept { return latencySamples_; }

private:
    std::vector<float> work_;              ///< Oversampled working buffer.
    std::vector<float> upState_;           ///< Interpolation half-band memory.
    std::vector<float> downState_;         ///< Decimation half-band memory.
    std::size_t        factor_            = 1;
    std::size_t        maxBaseFrames_     = 0;
    std::size_t        oversampledFrames_ = 0;
    std::size_t        latencySamples_    = 0;
};

} // namespace caecilia::dsp
