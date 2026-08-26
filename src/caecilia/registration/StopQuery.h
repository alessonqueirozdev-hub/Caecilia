// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/registration/RegistrationState.h"
#include "caecilia/registration/StopSet.h"

#include <optional>
#include <string>

namespace caecilia::model { class Organ; class Stop; }

namespace caecilia::registration
{

/**
 * @brief A single declarative predicate over a stop's semantic metadata — the
 *        conjunctive "atom" of the selector algebra.
 *
 * A StopQuery is intentionally tiny and data-only: it is the queryable,
 * portable representation the brief calls for *instead* of an embedded logic
 * engine. Every populated field is ANDed together, so
 * `family:reed & pitch:8 & div:swell` compiles to a single StopQuery with three
 * constraints set. A default-constructed StopQuery matches every stop.
 *
 * Matching is evaluated lazily against a live @c model::OrganSpec plus a
 * @c RegistrationState (the latter only for the @ref engaged constraint), so the
 * same query re-resolves correctly as the registration changes.
 *
 * Division may be constrained either by a resolved @c DivisionId or, when parsed
 * from text (`div:swell`), by a case-insensitive division @ref divisionName that
 * is resolved against the spec at query time.
 */
struct StopQuery
{
    /// Tri-state filter on whether the stop is currently drawn.
    enum class Engaged
    {
        Any,      ///< Do not constrain on engaged state.
        Only,     ///< Match only currently-engaged stops.
        Exclude   ///< Match only currently-disengaged stops.
    };

    std::optional<core::TonalFamily> family;      ///< e.g. Reed.
    std::optional<core::ChorusRole>  role;        ///< e.g. Foundation.
    std::optional<core::PitchClass>  pitchClass;  ///< e.g. Unison.
    std::optional<core::Footage>     footage;     ///< Exact footage (8', 2 2/3', ...).
    std::optional<core::DivisionId>  division;    ///< Resolved division constraint.
    std::string                      divisionName;///< Unresolved division name (`div:swell`).
    std::string                      nameContains;///< Case-insensitive substring of the stop name.
    Engaged                          engaged = Engaged::Any;
    bool                             mutationsOnly = false;   ///< Keep only non-octave mutations.
    bool                             excludeMutations = false;///< Drop non-octave mutations.

    /// @return true if this query constrains nothing (matches every stop).
    [[nodiscard]] bool isUniversal() const noexcept;

    /**
     * @brief Test a single stop against this query.
     * @param stop        The candidate stop's spec.
     * @param isEngaged   Whether @p stop is currently drawn (for @ref engaged).
     * @return true if @p stop satisfies every populated constraint.
     *
     * @note @ref divisionName is NOT consulted here — resolve it to a
     *       @c DivisionId via @ref resolve first. Off-thread; may compare
     *       strings but performs no allocation.
     */
    [[nodiscard]] bool matches(const model::Stop& stop, bool isEngaged) const;

    /**
     * @brief Resolve this query into the set of matching stops.
     * @param spec  The loaded organ to scan.
     * @param state The live registration (only read for @ref engaged filters).
     * @return The matching stops as an ordered, deduplicated @c StopSet.
     *
     * Off the audio thread only (allocates). Resolves @ref divisionName against
     * the spec's divisions before scanning.
     */
    [[nodiscard]] StopSet resolve(const model::Organ& spec, const RegistrationState& state) const;

    // --- Fluent builders (off-thread convenience) --------------------------
    StopQuery& withFamily(core::TonalFamily f) { family = f; return *this; }
    StopQuery& withRole(core::ChorusRole r) { role = r; return *this; }
    StopQuery& withFootage(core::Footage ft) { footage = ft; return *this; }
    StopQuery& inDivision(core::DivisionId d) { division = d; return *this; }
    StopQuery& onlyEngaged() { engaged = Engaged::Only; return *this; }
};

} // namespace caecilia::registration
