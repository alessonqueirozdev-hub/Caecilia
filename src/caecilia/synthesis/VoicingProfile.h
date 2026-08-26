// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstdint>

namespace caecilia::synth
{

/**
 * @brief The resolved, per-pipe hand-voicing variance a voice renders with.
 *
 * A real organ is voiced pipe by pipe: no two pipes of a rank are identical. A
 * VoicingProfile captures that deterministic, repeatable variance for ONE pipe.
 * @ref PerPipeVoicer produces one off the audio thread from a stable
 * @ref caecilia::core::PipeId seed, so a given pipe would always receive the
 * same voicing across runs and sessions, and a mixture would shimmer because its
 * ranks scatter independently rather than sounding cloned.
 *
 * @todo Nothing in the project constructs a @ref PerPipeVoicer, so no voice is
 *       ever handed a profile: they all run on the default-zero one. Even then
 *       the voices read only @ref detuneCents and @ref levelTrimDb — the
 *       remaining fields have no consumer at all.
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
 * These are meant to be authored per rank (or imported from a real-instrument
 * voicing curve) and to bound the deterministic scatter, so a gently-voiced
 * Principal stays tight while a keen String is allowed more life. All fields are
 * symmetric maxima unless noted. The @c model module authors the parallel
 * @c model::RankVoicingSpec per rank, but nothing converts one into a
 * VoicingParams, so the defaults below are all any caller would ever see.
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

} // namespace caecilia::synth
