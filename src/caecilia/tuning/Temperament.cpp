// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/tuning/Temperament.h"

// Temperament is a header-only value type; this translation unit exists to give
// the type a real compilation anchor and to pin its constexpr contract with
// compile-time sanity checks that fail the build if the invariants ever drift.

namespace caecilia::tuning
{
namespace
{

// Equal temperament: every degree is exactly on pitch (zero deviation).
constexpr Temperament kEqualCheck{};
static_assert(kEqualCheck.centsForPitchClass(0) == 0.0, "equal temperament must be flat everywhere");
static_assert(kEqualCheck.centsForPitchClass(9) == 0.0, "equal temperament A must be 0 cents");

// Pitch-class indexing must wrap modulo 12, including negative inputs.
static_assert(kEqualCheck.centsForPitchClass(12) == kEqualCheck.centsForPitchClass(0),
              "pitch class must wrap at the octave");
static_assert(kEqualCheck.centsForPitchClass(-1) == kEqualCheck.centsForPitchClass(11),
              "pitch class must wrap for negative inputs");

// A custom table round-trips through centsForPitchClass and is tagged Custom.
constexpr std::array<double, 12> kProbe{
    0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
constexpr Temperament kCustomCheck = Temperament::custom(kProbe, "Probe");
static_assert(kCustomCheck.id == core::TemperamentId::Custom, "custom() must tag Custom");
static_assert(kCustomCheck.centsForPitchClass(5) == 5.0, "custom table must be preserved");
static_assert(kCustomCheck.centsForPitchClass(17) == 5.0, "custom table must wrap at the octave");

} // namespace
} // namespace caecilia::tuning
