/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ceciliae::core::engine
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
 * The audio thread writes one of a double-buffered pair each block and flips an
 * atomic index; the UI always reads a fully-consistent frame with no lock and no
 * torn state. Everything the console needs to animate — key levels, wind sag,
 * tremulant phase, live voice count — travels through here, so the audio thread
 * never touches a UI object.
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

    std::uint16_t divisionCount = 0;                        ///< Valid entries in @ref divisions.
    std::array<MeterFrame, kMaxMeteredDivisions> divisions{}; ///< Per-division meters.

    float windPressurePa = 0.0f;    ///< Representative chest pressure (Pa).
    float windSagNorm    = 0.0f;    ///< Normalised sag (0 = nominal, negative = drooping).
    float tremulantPhase = 0.0f;    ///< Tremulant phase in [0, 1) for gauge animation.
};

} // namespace ceciliae::core::engine
