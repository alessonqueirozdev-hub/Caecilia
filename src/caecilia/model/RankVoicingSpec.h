// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

namespace caecilia::model
{

/**
 * @brief Rank-level voicing character + the per-pipe scatter amounts meant for
 *        the synthesis @c PerPipeVoicer to expand into deterministic
 *        hand-voicing variance keyed to each @c PipeId.
 *
 * This is the *source data* for voicing; it deliberately does NOT compute a
 * concrete per-pipe @c VoicingProfile (that belongs to the synthesis module,
 * which seeds a PRNG from the stable @c PipeId plus these curves). Keeping the
 * spec here means one set of authored numbers can drive audio, tooltips and UI.
 *
 * @todo Nothing consumes this yet: @c Rank::voicing() has no callers and there
 * is no conversion to @c synth::VoicingParams, so @c buildCaeciliaDemoOrgan
 * authors these values per tonal family and the render path then ignores them.
 *
 * All amounts are normalised character controls unless a unit is named; a rank
 * with all-zero scatter would sound "cloned", which the scatter fields exist to
 * avoid so mixtures shimmer.
 */
struct RankVoicingSpec
{
    float chiffAmount          = 0.0f; ///< Strength of the attack chiff/speech in [0, 1].
    float harmonicDevelopment  = 0.5f; ///< Steady-state upper-harmonic richness in [0, 1].
    float brightness           = 0.5f; ///< Overall spectral tilt / brightness in [0, 1].
    float windSensitivity      = 0.5f; ///< How strongly this rank responds to wind sag in [0, 1].

    // --- Per-pipe deterministic scatter (expanded by PerPipeVoicer via PipeId) ---
    float detuneScatterCents   = 1.0f; ///< Max per-pipe random detune, in cents.
    float levelScatterDb       = 0.5f; ///< Max per-pipe random level trim, in dB.
    float brightnessScatter    = 0.05f;///< Max per-pipe brightness deviation in [0, 1].
    float attackScatterMs      = 2.0f; ///< Max per-pipe attack-onset jitter, in ms.

    friend constexpr bool operator==(const RankVoicingSpec&, const RankVoicingSpec&) noexcept = default;
};

} // namespace caecilia::model
