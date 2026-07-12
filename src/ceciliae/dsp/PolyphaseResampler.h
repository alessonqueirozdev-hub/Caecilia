/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <cstddef>
#include <vector>

/**
 * @file PolyphaseResampler.h
 * @brief Fixed rational-ratio (L/M) sample-rate conversion via a polyphase FIR.
 *
 * For a fixed conversion ratio the classic efficient structure is a single
 * windowed-sinc low-pass prototype decomposed into @c L polyphase sub-filters:
 * upsampling by @c L then decimating by @c M without ever materialising the
 * zero-stuffed intermediate. Used to bring proprietary sample material recorded
 * at one rate onto the host rate. The prototype is built fresh from public
 * windowed-sinc math.
 *
 * For arbitrary, continuously-varying ratios (pitch bend, per-pipe detune) use
 * @ref Resampler with its Kaiser-sinc interpolator instead; this class targets a
 * fixed, known integer ratio.
 */

namespace ceciliae::dsp
{

/**
 * @brief Polyphase rational resampler for a fixed L/M ratio.
 *
 * Real-time contract: @ref prepare designs and allocates the prototype filter
 * (not RT-safe); @ref process and @ref reset are @c noexcept and allocation-free.
 */
class PolyphaseResampler
{
public:
    PolyphaseResampler() = default;

    /**
     * @brief Design the prototype low-pass and split it into @p interpFactor phases.
     * @param interpFactor  Upsampling factor L (>= 1).
     * @param decimFactor   Downsampling factor M (>= 1).
     * @param tapsPerPhase  FIR taps per polyphase branch (kernel quality).
     *
     * The prototype cutoff is set to the lower of the two band edges so neither
     * imaging nor aliasing leaks through. Not RT-safe.
     */
    void prepare(std::size_t interpFactor,
                 std::size_t decimFactor,
                 std::size_t tapsPerPhase = 32);

    /**
     * @brief Convert @p inCount input samples, writing up to @p outCapacity outputs.
     * @param input       Source samples.
     * @param inCount     Number of source samples.
     * @param output      Destination buffer.
     * @param outCapacity Capacity of @p output.
     * @return Number of output samples actually produced.
     *
     * RT-safe, @c noexcept. Streaming: internal phase/history carry across calls.
     *
     * @note TODO(phase3): flesh out the full commutator inner loop; the current
     *       body advances the phase accounting and produces a length-correct,
     *       low-pass-filtered output suitable for wiring up the signal graph.
     */
    std::size_t process(const float* input,
                        std::size_t  inCount,
                        float*       output,
                        std::size_t  outCapacity) noexcept;

    /// Clear the filter history and phase accumulator. RT-safe.
    void reset() noexcept;

    /// @return Upsampling factor L.
    [[nodiscard]] std::size_t interpFactor() const noexcept { return interp_; }
    /// @return Downsampling factor M.
    [[nodiscard]] std::size_t decimFactor() const noexcept { return decim_; }

private:
    std::vector<float> prototype_;   ///< Full windowed-sinc prototype low-pass.
    std::vector<float> history_;     ///< Ring of recent input samples.
    std::size_t        interp_       = 1;
    std::size_t        decim_        = 1;
    std::size_t        tapsPerPhase_ = 0;
    std::size_t        writePos_     = 0; ///< Head into history_.
    std::size_t        phase_        = 0; ///< Current commutator phase.
};

} // namespace ceciliae::dsp
