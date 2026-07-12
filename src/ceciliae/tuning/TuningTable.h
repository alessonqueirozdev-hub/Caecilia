/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <array>
#include <cstddef>

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/tuning/Temperament.h"

namespace ceciliae::tuning
{

/**
 * @brief A fully resolved 8' unison frequency table for all 128 MIDI notes.
 *
 * A @c Temperament describes only relative cent deviations; a @c TuningTable is
 * the concrete result of pinning that temperament to a reference pitch, so each
 * entry is an absolute sounding frequency in Hz for the corresponding key played
 * on an 8' unison stop.
 *
 * The table is @ref build "built" off the audio thread (in prepare(), or when a
 * temperament-change command is handled off-thread) and thereafter read with
 * cheap, RT-safe index lookups.
 *
 * Anchoring: A4 (MIDI note 69) is pinned @e exactly to the requested reference
 * pitch by subtracting the temperament's own A deviation from every degree. This
 * preserves the temperament's interval character while guaranteeing the ITuning
 * contract that A4 sounds at @c referenceA4Hz.
 */
struct TuningTable
{
    /// Number of MIDI notes covered (0..127).
    static constexpr std::size_t kNoteCount = 128;

    /// Sounding frequency in Hz per MIDI note, 8' unison, temperament applied.
    std::array<double, kNoteCount> hz{};

    /**
     * @brief Compile a temperament and reference pitch into an absolute table.
     * @param temperament   The temperament character to realise.
     * @param referenceA4Hz Pitch of A4 in Hz (e.g. 440.0, 415.0); must be > 0.
     * @return The resolved frequency table.
     *
     * Not RT-safe: computes 128 @c exp2 values. Call from prepare() / off-thread
     * temperament-change handling, never from the audio callback.
     */
    [[nodiscard]] static TuningTable build(const Temperament& temperament,
                                           double referenceA4Hz) noexcept;

    /**
     * @brief Sounding frequency of a key as an 8' unison, in Hz.
     * @param note MIDI note; out-of-range notes return 0.
     *
     * RT-safe: a single bounds-checked array read.
     */
    [[nodiscard]] double frequencyForNote(core::MidiNote note) const noexcept
    {
        return static_cast<std::size_t>(note) < kNoteCount ? hz[note] : 0.0;
    }
};

} // namespace ceciliae::tuning
