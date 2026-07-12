/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/ITuning.h"
#include "ceciliae/core/IWindSupply.h"

#include <cmath>

namespace ceciliae::synth
{

/**
 * @brief The shared, non-owning environment a voice renders inside.
 *
 * The core @ref ceciliae::core::IVoice contract keeps @c renderAdd(AudioBlock&)
 * deliberately narrow, so the wind supply, tuning table and per-rank identity a
 * voice needs are injected once, off the audio thread, through this context
 * before the first @c noteOn. The pointers are non-owning and outlive the voice
 * (they belong to the engine); the value fields are cheap copies.
 *
 * Real-time contract: only READ on the audio thread; never mutated there.
 */
struct VoiceContext
{
    const core::IWindSupply* wind    = nullptr;         ///< Per-block wind snapshot source (may be null in tests).
    const core::ITuning*     tuning  = nullptr;         ///< Frequency table (may be null => tempered fallback).
    core::WindchestId        chest{};                    ///< Windchest feeding this voice's pipe.
    core::Footage            footage = core::footage::kEight; ///< Sounding footage of the rank.
    core::TonalFamily        family  = core::TonalFamily::Principal; ///< Tonal family (selects wind response).
    core::RankId             rank{};                     ///< Owning rank identity.
};

/**
 * @brief Equal-tempered fallback sounding frequency for a pipe.
 *
 * Used only when no @ref ceciliae::core::ITuning is wired into the
 * @ref VoiceContext (e.g. headless unit tests). Production voices read the
 * tuning table, which honours the exact rational footage and historical
 * temperaments. This fallback applies the footage as a plain frequency scale.
 *
 * @param note    MIDI note number.
 * @param footage Exact rational footage of the rank.
 * @return Sounding frequency in Hz (A4 = 440).
 */
[[nodiscard]] inline double defaultSoundingFrequencyHz(core::MidiNote note,
                                                       core::Footage footage) noexcept
{
    const double unison = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
    // 8' unison sounds at the key pitch; an N' rank scales by (8 / feet).
    const double feet = footage.feet();
    const double scale = feet > 0.0 ? (8.0 / feet) : 1.0;
    return unison * scale;
}

/**
 * @brief Resolve a pipe's sounding frequency from a context, preferring the
 *        tuning table and falling back to equal temperament.
 */
[[nodiscard]] inline double soundingFrequencyHz(const VoiceContext& ctx,
                                                core::PipeId pipe) noexcept
{
    if (ctx.tuning != nullptr)
        return ctx.tuning->frequencyForPipe(pipe, ctx.footage);
    return defaultSoundingFrequencyHz(pipe.midiNote, ctx.footage);
}

} // namespace ceciliae::synth
