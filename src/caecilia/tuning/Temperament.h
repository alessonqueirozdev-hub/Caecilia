/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <array>
#include <cstddef>

#include "caecilia/core/ITuning.h"

namespace caecilia::tuning
{

/**
 * @brief A historical / micro temperament as a per-pitch-class deviation table.
 *
 * A temperament is fully described, up to a global transposition, by the twelve
 * cent deviations of its scale degrees from 12-tone equal temperament. The
 * deviations are authored with C (pitch class 0) as the zero reference and are
 * octave-periodic: pitch class @c pc applies to every MIDI note @c n with
 * <tt>n % 12 == pc</tt>.
 *
 * The concrete sounding-frequency anchoring (pinning A4 to the reference pitch)
 * is performed later by @ref TuningTable::build, so this type stays a pure,
 * transposition-free description of the temperament's @e character.
 *
 * This is a small, allocation-free value type (a @c std::array plus a
 * non-owning name pointer); it is cheap to copy and safe to embed anywhere.
 */
struct Temperament
{
    /// Number of chromatic pitch classes in an octave.
    static constexpr std::size_t kPitchClasses = 12;

    /// The catalogue id this temperament corresponds to (Custom for user tables).
    core::TemperamentId id = core::TemperamentId::Equal;

    /**
     * @brief Cent deviations from equal temperament, indexed by pitch class.
     *
     * Index 0 == C, 1 == C#/Db, ... 9 == A, ... 11 == B. Reference is C = 0.
     * Positive sharpens, negative flattens relative to the equal-tempered degree.
     */
    std::array<double, kPitchClasses> centsFromEqual{};

    /**
     * @brief Human-readable display name.
     *
     * Non-owning: the pointed-to string must outlive this value (built-ins point
     * at static string literals; custom temperaments borrow the caller's name).
     */
    const char* name = "Equal";

    /**
     * @brief Deviation, in cents, of a pitch class from equal temperament.
     * @param pitchClass Any integer; reduced modulo 12 (negative values handled).
     * @return Cent deviation for that pitch class.
     *
     * RT-safe: allocation-free, lock-free, @c constexpr.
     */
    [[nodiscard]] constexpr double centsForPitchClass(int pitchClass) const noexcept
    {
        const int pc = ((pitchClass % 12) + 12) % 12;
        return centsFromEqual[static_cast<std::size_t>(pc)];
    }

    /**
     * @brief Build a data-driven custom temperament from an explicit cent table.
     * @param centsFromEqualTable Twelve cent deviations (C = index 0).
     * @param displayName         Non-owning name pointer; must outlive the value.
     * @return A Temperament tagged @c TemperamentId::Custom.
     *
     * The single hook that makes the module data-driven: any well temperament,
     * meantone variant or measured historical tuning can be expressed as a
     * twelve-entry cent table without touching engine code.
     */
    [[nodiscard]] static constexpr Temperament custom(
        const std::array<double, kPitchClasses>& centsFromEqualTable,
        const char* displayName = "Custom") noexcept
    {
        Temperament t{};
        t.id = core::TemperamentId::Custom;
        t.centsFromEqual = centsFromEqualTable;
        t.name = displayName;
        return t;
    }

    friend constexpr bool operator==(const Temperament& a, const Temperament& b) noexcept
    {
        return a.id == b.id && a.centsFromEqual == b.centsFromEqual;
    }
};

} // namespace caecilia::tuning
