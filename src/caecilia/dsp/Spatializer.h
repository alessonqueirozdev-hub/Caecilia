/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>
#include <vector>

/**
 * @file Spatializer.h
 * @brief Per-pipe stereo placement and pipe-position early reflection.
 *
 * Each pipe sits at a physical position across the case, at a depth that delays
 * and colours its direct sound. This object turns a pipe's mono voice bus into a
 * stereo pair using equal-power panning, a distance attenuation, and a short
 * position-dependent pre-delay that seeds the early-reflection field ahead of the
 * shared reverb tail. One instance placements one source, so it may be owned per
 * voice and reconfigured on note-on.
 */

namespace caecilia::dsp
{

/**
 * @brief Placement descriptor for a single pipe.
 *
 * A self-contained POD so the DSP module stays free of any dependency on the
 * model module. TODO(phase4): adapt/populate this from @c model::PipeSpatial
 * when a pipe is voiced, keeping the DSP seam model-agnostic.
 */
struct PipeSpatialParams
{
    float panNorm      = 0.0f; ///< -1 = hard left, 0 = centre, +1 = hard right.
    float distanceNorm = 0.0f; ///< 0 = close/front, 1 = far/back (attenuates).
    float depthMs      = 0.0f; ///< Case-depth pre-delay of the direct sound (ms).
    float earlyLevel   = 0.0f; ///< Level of the depth-delayed early reflection [0,1].
};

/**
 * @brief Stereo placement processor for one pipe/voice.
 *
 * Real-time contract: @ref prepare allocates the pre-delay buffer (not RT-safe);
 * @ref setPlacement, @ref processAdd, @ref processReplace and @ref reset are
 * @c noexcept and allocation-free.
 */
class Spatializer
{
public:
    Spatializer() = default;

    /**
     * @brief Allocate the pre-delay buffer for the given maximum depth.
     * @param sampleRate     Host sample rate in Hz.
     * @param maxDepthMs     Largest pre-delay this instance will need.
     *
     * Not RT-safe.
     */
    void prepare(core::SampleRate sampleRate, float maxDepthMs = 40.0f);

    /// Recompute pan gains, distance attenuation and pre-delay. RT-safe.
    void setPlacement(const PipeSpatialParams& params) noexcept;

    /**
     * @brief Place @p count mono samples and ADD them into the stereo pair.
     * @param mono  Source mono samples.
     * @param count Number of samples.
     * @param left  Left destination (accumulated into).
     * @param right Right destination (accumulated into).
     *
     * Accumulates, mirroring the voice renderAdd contract. RT-safe, @c noexcept.
     */
    void processAdd(const float* mono,
                    std::size_t  count,
                    float*       left,
                    float*       right) noexcept;

    /// As @ref processAdd but OVERWRITES the destination. RT-safe, @c noexcept.
    void processReplace(const float* mono,
                        std::size_t  count,
                        float*       left,
                        float*       right) noexcept;

    /// Clear the pre-delay buffer. RT-safe.
    void reset() noexcept;

private:
    core::SampleRate   sampleRate_ = 44100.0;
    float              gainLeft_   = 0.70710678f; ///< Equal-power centre default.
    float              gainRight_  = 0.70710678f;
    float              earlyLevel_ = 0.0f;
    std::vector<float> preDelay_;                 ///< Depth pre-delay ring.
    std::size_t        preDelayLen_ = 0;
    std::size_t        preDelayPos_ = 0;
};

} // namespace caecilia::dsp
