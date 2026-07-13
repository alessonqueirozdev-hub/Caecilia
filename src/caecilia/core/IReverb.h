/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"

namespace caecilia::core
{

/**
 * @brief Common control parameters for both reverb implementations
 *        (algorithmic FDN and convolution).
 *
 * All fields are plain values a controller sets off the audio thread; the
 * reverb reads a snapshot of them. Independently authored from public math; no
 * GPL DSP is referenced.
 */
struct ReverbParams
{
    float mix        = 0.25f; ///< Dry/wet mix in [0, 1] (0 = dry, 1 = wet).
    float decaySec   = 2.5f;  ///< Approx. RT60 decay time in seconds.
    float preDelayMs = 12.0f; ///< Pre-delay before the tail, in milliseconds.
    float dampingHz  = 6000.f;///< High-frequency damping corner in Hz.
    float widthNorm  = 1.0f;  ///< Stereo width of the tail in [0, 1].
    float bassBloom  = 1.0f;  ///< Low-frequency RT60 multiplier (1 = flat; >1 makes
                              ///< the bass "bloom" with a longer tail than the mids,
                              ///< the signature of a large stone room). Clamped >= 1.
};

/**
 * @brief A stereo reverb processor. Implemented by both the fresh Jot-style FDN
 *        reverb and the convolution reverb.
 *
 * ## Real-time contract
 * - @ref prepare allocates delay lines / partitions and precomputes coefficients.
 *   Not RT-safe; call off the audio thread.
 * - @ref setParams swaps in a parameter snapshot; RT-safe (no reallocation).
 * - @ref process runs in place on the block, RT-safe, @c noexcept, with FTZ/DAZ
 *   and explicit denormal flushing in the feedback path.
 * - @ref reset clears internal state (delay lines, filter memory); RT-safe.
 */
class IReverb
{
public:
    virtual ~IReverb() = default;

    /**
     * @brief Allocate and precompute for a sample rate and maximum block size.
     * @param sampleRate     Host sample rate in Hz (> 0).
     * @param maxBlockFrames Largest block @ref process will receive.
     * @param numChannels    Channel count to process (typically 2).
     */
    virtual void prepare(SampleRate sampleRate,
                         std::size_t maxBlockFrames,
                         std::size_t numChannels) = 0;

    /// Swap in new parameters. RT-safe, no reallocation.
    virtual void setParams(const ReverbParams& params) noexcept = 0;

    /**
     * @brief Process @p block in place (dry/wet mixed per @ref ReverbParams::mix).
     *
     * RT-safe, @c noexcept, denormal-flushed feedback.
     */
    virtual void process(AudioBlock& block) noexcept = 0;

    /// Clear all internal state so the next block starts from silence. RT-safe.
    virtual void reset() noexcept = 0;

    /**
     * @brief Additional latency this reverb introduces, in samples (e.g. a
     *        partitioned-convolution first-partition delay), so the host can
     *        report plugin delay compensation. RT-safe.
     */
    [[nodiscard]] virtual std::size_t latencySamples() const noexcept = 0;
};

} // namespace caecilia::core
