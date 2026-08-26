// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::core::engine
{

/// Peak and RMS level of one metered bus for a single block (linear, not dB).
struct MeterFrame
{
    float peak = 0.0f; ///< Linear peak magnitude this block.
    float rms  = 0.0f; ///< Linear RMS this block.
};

/**
 * @brief An immutable per-block metering snapshot the audio thread publishes for
 *        the UI to poll at frame rate.
 *
 * The audio thread publishes one frame per block through a TripleBuffer, so the
 * UI always reads a fully-consistent frame with no lock and no torn state (two
 * slots would not be enough — see core/TripleBuffer.h). Everything the console
 * needs to animate is meant to travel through here, so the audio thread never
 * touches a UI object.
 *
 * @todo Only @ref framePos, @ref activeVoices, @ref master and — when a wind
 *       supply is bound — the two wind fields are filled in today. See
 *       AudioEngine::captureMeters().
 *
 * POD and trivially copyable; sized for a fixed maximum number of metered
 * divisions so it never allocates.
 */
struct MeterSnapshot
{
    /// Upper bound on independently metered divisions (Great/Swell/Pedal/...).
    static constexpr std::size_t kMaxMeteredDivisions = 16;

    std::uint64_t framePos     = 0; ///< Absolute frame index at end of the block.
    std::uint32_t activeVoices = 0; ///< Voices sounding after this block.

    MeterFrame master{};            ///< Post-master-chain output meter.

    // @todo Never populated: AudioEngine::captureMeters() leaves the count at
    // zero and the frames silent, so the console draws no division meters.
    std::uint16_t divisionCount = 0;                        ///< Valid entries in @ref divisions.
    std::array<MeterFrame, kMaxMeteredDivisions> divisions{}; ///< Per-division meters.

    // --- CPU governor -------------------------------------------------------
    //
    // What the engine is costing and what it is giving up for it. On a meter this
    // is not vanity: the load is the only thing that tells an organist whether the
    // next stop they draw will be the one that starts dropping pipes.

    float cpuLoad     = 0.0f; ///< Engine time / block period, smoothed. 1 = xrun.
    float cpuPeakLoad = 0.0f; ///< Decaying peak of the same. An xrun is ONE block.
    float budgetUnits = 0.0f; ///< The governor's current allowance, in cost units.
    float demandUnits = 0.0f; ///< What the active set asked for, in the same units.

    /// Voices the governor gave up this block. Zero whenever demand fits, which
    /// is every block on a machine that is coping.
    std::uint32_t voicesShed = 0;

    float windPressurePa = 0.0f;    ///< Representative chest pressure (Pa).
    float windSagNorm    = 0.0f;    ///< Normalised sag (0 = nominal, negative = drooping).
    float tremulantPhase = 0.0f;    ///< Tremulant phase in [0, 1) for gauge
                                    ///< animation. @todo Never written; stays 0.
};

} // namespace caecilia::core::engine
