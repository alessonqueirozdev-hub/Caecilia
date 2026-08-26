// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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

/// Below these deltas a change in the four coefficient-driving fields cannot be
/// heard. They live here, next to the struct, so the reverb and every producer
/// that gates on "did this move enough to matter?" answer with the same numbers
/// rather than each carrying a private copy that drifts.
inline constexpr float kReverbDecayEps   = 0.01f;  ///< seconds of RT60
inline constexpr float kReverbDampingEps = 20.0f;  ///< Hz of damping corner
inline constexpr float kReverbWidthEps   = 0.005f; ///< normalised stereo width
inline constexpr float kReverbBloomEps   = 0.01f;  ///< bass RT60 multiplier

/**
 * @brief Clamp @p params to the ranges an @ref IReverb enforces on the way in,
 *        so a caller can predict exactly what @ref IReverb::setParams will store.
 *
 * @ref reverbNeedsRecompute cannot be honest without this. A reverb keeps
 * CLAMPED values; a caller comparing its RAW request against them sees a
 * permanent difference for any request outside the legal range. Automate the
 * damping corner up to 30 kHz at a 48 kHz sample rate and every single block
 * reports "changed", because 30000 never equals the stored 23520 — so the gate
 * that exists to keep the coefficient rebuild off the audio thread fires on
 * every block instead of none. Comparing clamped-to-clamped is the fix.
 *
 * Also the module's NaN gate. Each clamp is written so an unordered comparison
 * falls to the safe end (mix -> 0, damping -> 200 Hz, decay -> 0.05 s): one NaN
 * reaching @c mix used to multiply the entire output by NaN, and one reaching
 * @c decaySec poisoned every feedback gain in the tank permanently.
 *
 * RT-safe, @c noexcept, no allocation.
 */
[[nodiscard]] inline ReverbParams clampReverbParams(const ReverbParams& params,
                                                    SampleRate          sampleRate) noexcept
{
    // Ordered so that NaN (which compares false against everything) lands on lo.
    const auto lim = [](float v, float lo, float hi) noexcept
    {
        return v > lo ? (v < hi ? v : hi) : lo;
    };
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;

    ReverbParams p = params;
    p.mix        = lim(p.mix, 0.0f, 1.0f);
    p.decaySec   = lim(p.decaySec, 0.05f, 1.0e6f);
    p.preDelayMs = lim(p.preDelayMs, 0.0f, 1.0e6f);
    p.dampingHz  = lim(p.dampingHz, 200.0f, static_cast<float>(fs * 0.49));
    p.widthNorm  = lim(p.widthNorm, 0.0f, 1.0f);
    // Bloom only ever LENGTHENS the bass relative to the mids; a value below 1
    // would shorten it and, more importantly, break the attenuate-only stability
    // argument that keeps the FDN loop gain under unity.
    p.bassBloom  = lim(p.bassBloom, 1.0f, 3.0f);
    return p;
}

/**
 * @brief Would moving from @p current to @p next actually change any reverb
 *        coefficient audibly?
 *
 * Only the four fields that drive the decay gains, damper poles, width taps and
 * bass-bloom lift are considered; @c mix and @c preDelayMs are a multiply and a
 * ring-index move, so they are free and always applied.
 *
 * Both sides are clamped first — see @ref clampReverbParams for why that is not
 * optional. RT-safe, @c noexcept.
 *
 * @param current    The parameter set currently in force.
 * @param next       The candidate set.
 * @param sampleRate The rate both sets will be interpreted at.
 */
[[nodiscard]] inline bool reverbNeedsRecompute(const ReverbParams& current,
                                               const ReverbParams& next,
                                               SampleRate          sampleRate) noexcept
{
    const ReverbParams a = clampReverbParams(current, sampleRate);
    const ReverbParams b = clampReverbParams(next, sampleRate);

    // Negated so an unordered (NaN) comparison reads as "moved" and forces the
    // recompute, where clampReverbParams has already replaced the NaN.
    const auto moved = [](float x, float y, float eps) noexcept
    {
        const float d = x - y;
        return !(d < eps && d > -eps);
    };

    return moved(a.decaySec,  b.decaySec,  kReverbDecayEps)
        || moved(a.dampingHz, b.dampingHz, kReverbDampingEps)
        || moved(a.widthNorm, b.widthNorm, kReverbWidthEps)
        || moved(a.bassBloom, b.bassBloom, kReverbBloomEps);
}

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
