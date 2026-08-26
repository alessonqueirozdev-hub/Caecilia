// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

namespace caecilia::core
{

/// Selectable historical / micro tuning systems.
enum class TemperamentId : std::uint8_t
{
    Equal,          ///< 12-tone equal temperament.
    QuarterMeantone,///< Quarter-comma meantone.
    Werckmeister3,  ///< Werckmeister III.
    Kirnberger3,    ///< Kirnberger III.
    Pythagorean,    ///< Pythagorean.
    Young,          ///< Thomas Young (1799).
    Custom          ///< User-supplied per-note offset table.
};

/**
 * @brief Maps a note to a sounding frequency under a chosen temperament,
 *        honouring the exact-rational Footage of the sounding rank.
 *
 * The concrete tuning model precomputes a per-pipe frequency table OFF the
 * audio thread (in prepare()), so the accessors below are cheap table lookups
 * and never mutate live engine state.
 *
 * @todo A temperament change does not reach this table at runtime: the engine
 *       drains EngineCommandType::SetTemperament and drops it, so the tuning
 *       stays exactly as prepare() built it. See AudioEngine::drainCommands().
 *
 * Real-time contract: all query methods are @c noexcept, allocation-free and
 * lock-free.
 */
class ITuning
{
public:
    virtual ~ITuning() = default;

    /// @return The temperament currently in effect. RT-safe.
    [[nodiscard]] virtual TemperamentId temperament() const noexcept = 0;

    /// @return Reference pitch of A4 in Hz (e.g. 440.0, 415.0). RT-safe.
    [[nodiscard]] virtual double referenceA4Hz() const noexcept = 0;

    /**
     * @brief Sounding frequency of a keyboard note as an 8' unison stop, under
     *        the active temperament and A-reference.
     * @param note MIDI note in [0, 127].
     * @return Frequency in Hz.
     *
     * RT-safe.
     */
    [[nodiscard]] virtual double frequencyForNote(MidiNote note) const noexcept = 0;

    /**
     * @brief Sounding frequency of a pipe: the note's unison frequency scaled by
     *        the rank's exact Footage, plus per-pipe stretch/detune scatter.
     * @param pipe    The pipe's stable identity.
     * @param footage The exact rational footage of the pipe's rank.
     * @return Frequency in Hz.
     *
     * Scaling by Footage is exact (a 4' rank sounds one octave above the 8'
     * unison; a 2 2/3' quint sounds a twelfth above), so a mutation never drifts
     * off its true harmonic. RT-safe.
     */
    [[nodiscard]] virtual double frequencyForPipe(PipeId pipe, Footage footage) const noexcept = 0;
};

} // namespace caecilia::core
