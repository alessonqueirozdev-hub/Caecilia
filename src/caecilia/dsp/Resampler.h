// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/dsp/SincKaiser16Interpolator.h"

#include <cstddef>

/**
 * @file Resampler.h
 * @brief Arbitrary, continuously-variable-ratio sample reader.
 *
 * Sample playback needs its source read at a fractional rate that changes per
 * note (pitch, per-pipe detune, temperament) and per block (wind-driven pitch
 * modulation). This reader walks a source buffer at a floating-point increment
 * and reconstructs each output sample either by cheap linear interpolation or by
 * the fresh 16-tap Kaiser-windowed-sinc kernel. It is complete and unit-tested,
 * but no voice calls it yet: the shipping engine is additive-only. For a FIXED
 * integer ratio prefer @ref PolyphaseResampler; for a moving ratio use this.
 */

namespace caecilia::dsp
{

/**
 * @brief Fractional sample reader with selectable interpolation quality.
 *
 * Real-time contract: @ref prepare builds the sinc table (not RT-safe);
 * @ref readBlock, @ref interpolateAt and @ref reset are @c noexcept and
 * allocation-free.
 */
class Resampler
{
public:
    /// Reconstruction quality / cost trade-off.
    enum class Quality
    {
        Linear,     ///< 2-point linear: cheapest, mild high-frequency loss.
        KaiserSinc16 ///< 16-tap Kaiser-windowed-sinc: near-transparent.
    };

    Resampler() = default;

    /**
     * @brief Build the interpolator table and select a default quality.
     * @param quality   Reconstruction quality.
     * @param numPhases Kaiser-sinc phase resolution (see SincKaiser16Interpolator).
     *
     * Not RT-safe (allocates the polyphase table).
     */
    void prepare(Quality quality = Quality::KaiserSinc16, std::size_t numPhases = 512);

    /// Select quality at run time (both kernels are always available). RT-safe.
    void setQuality(Quality quality) noexcept { quality_ = quality; }

    /// @return The active reconstruction quality.
    [[nodiscard]] Quality quality() const noexcept { return quality_; }

    /**
     * @brief Reconstruct the source at fractional position @p pos (in samples).
     * @param source   Source sample buffer.
     * @param length   Number of valid samples in @p source.
     * @param pos      Read position in samples; the integer part indexes @p source
     *                 and the fraction selects the sub-sample phase.
     * @return The interpolated sample (0 outside the valid range).
     *
     * Edge taps are clamped so the 16-tap kernel never reads out of bounds.
     * RT-safe, @c noexcept.
     */
    [[nodiscard]] float interpolateAt(const float* source,
                                      std::size_t  length,
                                      double       pos) const noexcept;

    /**
     * @brief Read @p outCount samples advancing @p readPos by @p ratio each step.
     * @param source      Source buffer.
     * @param length      Valid source samples.
     * @param readPos     In/out fractional read cursor (advanced in place).
     * @param ratio       Source samples consumed per output sample
     *                    (= srcRate/dstRate * pitchScale).
     * @param output      Destination buffer.
     * @param outCount    Number of output samples to produce.
     *
     * RT-safe, @c noexcept. Stops early (zero-filling) if @p readPos runs past
     * the end of @p source.
     */
    void readBlock(const float* source,
                   std::size_t  length,
                   double&      readPos,
                   double       ratio,
                   float*       output,
                   std::size_t  outCount) const noexcept;

    /// No streaming state to clear today; provided for interface symmetry. RT-safe.
    void reset() noexcept {}

private:
    SincKaiser16Interpolator interp_{};
    Quality                  quality_ = Quality::KaiserSinc16;
};

} // namespace caecilia::dsp
