/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstdint>

namespace ceciliae::synth
{

/**
 * @brief The resolved, per-pipe hand-voicing variance a voice renders with.
 *
 * A real organ is voiced pipe by pipe: no two pipes of a rank are identical. A
 * VoicingProfile captures that deterministic, repeatable variance for ONE pipe.
 * It is produced off the audio thread by the @ref PerPipeVoicer from a stable
 * @ref ceciliae::core::PipeId seed, so a given pipe always receives the same
 * voicing across runs and sessions, and so a mixture shimmers because its ranks
 * scatter independently rather than sounding cloned.
 */
struct VoicingProfile
{
    float    detuneCents   = 0.0f; ///< Static per-pipe detune in cents.
    float    levelTrimDb   = 0.0f; ///< Per-pipe loudness trim in decibels.
    float    brightnessTrim = 0.0f;///< Per-pipe timbral tilt (+ brighter, - darker), in [-1, 1].
    float    attackJitter  = 0.0f; ///< Per-pipe attack-time scatter in [-1, 1].
    float    chiffAmount   = 0.0f; ///< Strength of the speech/chiff transient in [0, 1].
    float    speechAmount  = 0.0f; ///< Amount of pitch/level "speech" during onset in [0, 1].
    float    driftDepth    = 0.0f; ///< Slow independent random pitch drift depth in cents.
    std::uint32_t phaseSeed = 0u;  ///< Deterministic seed for the voice's initial phase set.
};

/**
 * @brief The voicing "recipe" for a whole rank: the ranges the @ref PerPipeVoicer
 *        scatters a per-pipe @ref VoicingProfile within.
 *
 * These are authored per rank (or imported from a real-instrument voicing curve)
 * and bound the deterministic scatter, so a gently-voiced Principal stays tight
 * while a keen String is allowed more life. All fields are symmetric maxima
 * unless noted.
 */
struct VoicingParams
{
    float maxDetuneCents   = 2.0f;  ///< Peak absolute per-pipe detune.
    float maxLevelTrimDb   = 0.75f; ///< Peak absolute per-pipe level trim.
    float maxBrightnessTrim = 0.15f;///< Peak absolute per-pipe brightness tilt.
    float maxAttackJitter  = 0.25f; ///< Peak absolute per-pipe attack scatter.
    float baseChiffAmount  = 0.2f;  ///< Nominal chiff strength before per-pipe scatter.
    float baseSpeechAmount = 0.15f; ///< Nominal speech amount before per-pipe scatter.
    float maxDriftDepth    = 1.0f;  ///< Peak slow-drift depth in cents.
};

} // namespace ceciliae::synth
