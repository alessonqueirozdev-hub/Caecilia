// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/tuning/TuningTable.h"

#include <cmath>

namespace caecilia::tuning
{

TuningTable TuningTable::build(const Temperament& temperament, double referenceA4Hz) noexcept
{
    TuningTable table{};

    // Guard against a nonsensical reference; fall back to concert pitch.
    const double a4Hz = referenceA4Hz > 0.0 ? referenceA4Hz : 440.0;

    // Pin A4 exactly: zero out A's own deviation so the temperament's character
    // is preserved while note 69 lands precisely on the reference pitch.
    constexpr int kMidiA4 = 69;
    constexpr int kPitchClassA = 9;
    const double anchorCents = temperament.centsForPitchClass(kPitchClassA);

    for (std::size_t n = 0; n < kNoteCount; ++n)
    {
        const int pitchClass = static_cast<int>(n % 12);
        const double netCents = temperament.centsForPitchClass(pitchClass) - anchorCents;

        // f(n) = A4 * 2^((n - 69)/12 + netCents/1200)
        const double semitoneExp = (static_cast<double>(n) - static_cast<double>(kMidiA4)) / 12.0;
        const double centExp = netCents / 1200.0;
        table.hz[n] = a4Hz * std::exp2(semitoneExp + centExp);
    }

    return table;
}

} // namespace caecilia::tuning
