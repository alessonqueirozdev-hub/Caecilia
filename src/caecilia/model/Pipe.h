/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/model/PipeSpatial.h"

namespace caecilia::model
{

/**
 * @brief One physical pipe: the smallest addressable sounding element.
 *
 * A Pipe carries frequency and voicing — the two things the brief asks of it:
 * - **Frequency**: a nominal sounding frequency (equal-temperament A440,
 *   scaled by the owning rank's exact @c Footage) computed at load time as a
 *   sensible default; the authoritative, temperament-aware frequency is read
 *   at render time from @ref frequencyUnder using the engine's @c ITuning.
 * - **Voicing**: the stable @c PipeId that seeds the synthesis @c PerPipeVoicer,
 *   plus optional per-pipe *overrides* imported from a real-instrument voicing
 *   measurement (all zero => derive everything deterministically from the seed).
 *
 * A Pipe is an immutable value once the organ is compiled; every accessor used
 * on the audio thread is @c noexcept and allocation-free.
 */
struct Pipe
{
    core::PipeId id{};                 ///< Stable (rank, note) identity + voicing seed.
    PipeSpatial  spatial{};            ///< Physical placement feeding dsp early reflections.
    float        nominalFrequencyHz = 0.0f; ///< Load-time default (equal temperament, footage-scaled).

    // --- Optional per-pipe imported voicing overrides (0 => derive from seed) ---
    float        detuneCents     = 0.0f; ///< Measured per-pipe detune override, in cents.
    float        levelTrimDb     = 0.0f; ///< Measured per-pipe level trim, in dB.
    float        brightnessTrim  = 0.0f; ///< Measured per-pipe brightness deviation in [-1, 1].

    /// @return This pipe's nominal MIDI note. RT-safe.
    [[nodiscard]] core::MidiNote note() const noexcept { return id.midiNote; }

    /**
     * @brief Authoritative sounding frequency under the active temperament.
     * @param tuning  The engine's tuning model (per-pipe table precomputed off-thread).
     * @param footage The owning rank's exact footage (drives the octave/mutation scaling).
     * @return Frequency in Hz.
     *
     * Delegates to @c ITuning::frequencyForPipe so the exact rational footage and
     * any per-pipe stretch/detune scatter are applied consistently. RT-safe.
     */
    [[nodiscard]] double frequencyUnder(const core::ITuning& tuning,
                                        core::Footage footage) const noexcept
    {
        return tuning.frequencyForPipe(id, footage);
    }

    friend bool operator==(const Pipe&, const Pipe&) noexcept = default;
};

} // namespace caecilia::model
