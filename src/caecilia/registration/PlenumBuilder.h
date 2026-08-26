// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/registration/RegistrationState.h"
#include "caecilia/registration/StopSet.h"

#include <cstdint>
#include <optional>

namespace caecilia::model { class Organ; }

namespace caecilia::registration
{

/**
 * @brief Declarative description of the plenum ("full chorus") to build.
 *
 * Encodes the organ-building conventions the baseline only gestured at: a plenum
 * is the principal-family, unison-pitched pyramid 16'-8'-4'-2' crowned by
 * mixtures, with mutations (quints, tierces) excluded by default so a diapason
 * chorus is never polluted, and chorus reeds added only on request.
 */
struct PlenumSpec
{
    /// Restrict to one division, or nullopt for the whole organ.
    std::optional<core::DivisionId> division;

    bool includeMixtures    = true;   ///< Crown the chorus with mixtures / cornets.
    bool includeChorusReeds = false;  ///< Add unison/chorus reeds (not solo colour).
    bool excludeMutations   = true;   ///< Keep quints/tierces out of the diapason chorus.

    /// Lowest octave class (relative to 8') to admit: -1 == 16', -2 == 32'.
    std::int32_t lowestOctaveClass = -1;
    /// Highest octave class to admit: +2 == 2', +3 == 1'.
    std::int32_t highestOctaveClass = 2;

    /// If true, add the plenum to the current registration; else it replaces the
    /// engaged stops within @ref division (handled by the engine).
    bool additive = false;
};

/**
 * @brief Strategy interface deriving the stop set for a @c PlenumSpec.
 *
 * A first-class interface so the heuristic can be swapped (e.g. a style-aware
 * builder, or one driven by imported registrations) without touching the engine.
 */
class IPlenumBuilder
{
public:
    virtual ~IPlenumBuilder() = default;

    /**
     * @brief Derive the stops that make up the requested plenum.
     * @param spec    The loaded organ.
     * @param request The plenum to build.
     * @param current The live registration (consulted for @c additive builds).
     * @return The stops the plenum engages.
     *
     * Off the audio thread only.
     */
    [[nodiscard]] virtual StopSet build(const model::Organ& spec,
                                        const PlenumSpec& request,
                                        const RegistrationState& current) const = 0;
};

/**
 * @brief The default, taxonomy-driven plenum builder.
 *
 * Selects principal-family octave pitches within the requested octave-class
 * window, crowns with mixtures, optionally adds chorus reeds, and drops
 * mutations unless asked to keep them — using the exact-rational @c Footage so
 * pitch reasoning is correct.
 */
class DefaultPlenumBuilder final : public IPlenumBuilder
{
public:
    [[nodiscard]] StopSet build(const model::Organ& spec,
                                const PlenumSpec& request,
                                const RegistrationState& current) const override;
};

} // namespace caecilia::registration
