/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

namespace ceciliae::tuning
{

/**
 * @brief A Railsback-style octave-stretch curve applied on top of a temperament.
 *
 * Stretch tuning bends the extremes of the compass outward — the bass a little
 * flat, the treble a little sharp — so that octaves beat with the (slightly
 * inharmonic) partials the ear expects rather than with theoretical equal-
 * tempered ratios. Here the deviation is modelled as a simple piecewise-linear
 * function of distance, in octaves, from a pivot note: zero at the pivot, sloping
 * one way in the bass and another in the treble.
 *
 * The default (both slopes 0) is a no-op, i.e. no stretch, so an unconfigured
 * curve leaves the temperament untouched. This is a trivially-copyable value
 * type with only @c constexpr / @c noexcept accessors and is safe to read on the
 * audio thread.
 */
struct StretchTuning
{
    /// Cents-per-octave slope below the pivot (positive => a flatter bass).
    double bassCentsPerOctave = 0.0;

    /// Cents-per-octave slope above the pivot (positive => a sharper treble).
    double trebleCentsPerOctave = 0.0;

    /// The note at which the stretch deviation is exactly zero (default A4).
    core::MidiNote pivotNote = 69;

    /// @return A neutral (identity) stretch curve that adds nothing.
    [[nodiscard]] static constexpr StretchTuning none() noexcept { return {}; }

    /**
     * @brief Stretch deviation, in cents, applied to a key's unison pitch.
     * @param note MIDI note of the key being played.
     * @return Signed cent offset (flat below pivot, sharp above, for positive
     *         slopes).
     *
     * RT-safe: allocation-free, lock-free, @c constexpr.
     */
    [[nodiscard]] constexpr double centsForNote(core::MidiNote note) const noexcept
    {
        const double octaves =
            (static_cast<double>(static_cast<int>(note)) - static_cast<double>(static_cast<int>(pivotNote))) / 12.0;
        // Below the pivot octaves is negative, so a positive bassCentsPerOctave
        // yields a negative (flat) deviation, matching the Railsback shape.
        return octaves >= 0.0 ? octaves * trebleCentsPerOctave
                              : octaves * bassCentsPerOctave;
    }
};

} // namespace ceciliae::tuning
