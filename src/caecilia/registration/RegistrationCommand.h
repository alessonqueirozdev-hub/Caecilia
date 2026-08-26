// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/registration/PlenumBuilder.h"
#include "caecilia/registration/RegistrationIntent.h"
#include "caecilia/registration/RegistrationState.h"
#include "caecilia/registration/Selector.h"

#include <optional>
#include <string>

namespace caecilia::registration
{

/**
 * @brief A single declarative registration operation — the tagged variant the
 *        registration brain speaks and that the history folds into state.
 *
 * State is the fold of the command log, so every mutation is expressed as one of
 * these values (never by poking the state directly). A command carries a @ref kind
 * plus exactly the payload that kind needs; the others are ignored. Commands are
 * plain values, so they snapshot cleanly into the branching @c RegistrationHistory
 * and are meant to travel unchanged from OSC, JSON-RPC, MIDI-learn and the UI.
 * @todo none of those surfaces produce one yet; only the unit tests do.
 */
struct RegistrationCommand
{
    /// The operation this command performs.
    enum class Kind
    {
        EngageStop,        ///< Draw one stop (@ref stop).
        DisengageStop,     ///< Retire one stop (@ref stop).
        ToggleStop,        ///< Flip one stop (@ref stop).
        EngageSelector,    ///< Draw every stop matching @ref selector.
        DisengageSelector, ///< Retire every stop matching @ref selector.
        ToggleSelector,    ///< Flip every stop matching @ref selector.
        Solo,              ///< Engage only @ref selector; retire the rest (in @ref scope).
        Clear,             ///< Retire all stops (optionally only within @ref scope).
        BuildPlenum,       ///< Derive and engage a plenum (@ref plenum).
        EngageCoupler,     ///< Engage one coupler (@ref coupler).
        DisengageCoupler,  ///< Disengage one coupler (@ref coupler).
        ToggleCoupler,     ///< Flip one coupler (@ref coupler).
        RecallSnapshot,    ///< Replace the whole state with @ref snapshot.
        ApplyIntent        ///< Port @ref intent onto the loaded organ and apply it.
    };

    Kind kind = Kind::Clear;

    // --- Payloads (only the field matching @ref kind is meaningful) ---------
    core::StopId                    stop{};
    model::CouplerId                coupler{};
    Selector                        selector{};
    PlenumSpec                      plenum{};
    RegistrationState               snapshot{};
    RegistrationIntent              intent{};
    std::optional<core::DivisionId> scope; ///< Optional division scope for Clear/Solo.

    // --- Factory helpers ----------------------------------------------------
    [[nodiscard]] static RegistrationCommand engageStop(core::StopId s);
    [[nodiscard]] static RegistrationCommand disengageStop(core::StopId s);
    [[nodiscard]] static RegistrationCommand toggleStop(core::StopId s);
    [[nodiscard]] static RegistrationCommand engageSelector(Selector sel);
    [[nodiscard]] static RegistrationCommand disengageSelector(Selector sel);
    [[nodiscard]] static RegistrationCommand toggleSelector(Selector sel);
    [[nodiscard]] static RegistrationCommand solo(Selector sel,
                                                  std::optional<core::DivisionId> scope = std::nullopt);
    [[nodiscard]] static RegistrationCommand clear(std::optional<core::DivisionId> scope = std::nullopt);
    [[nodiscard]] static RegistrationCommand buildPlenum(PlenumSpec spec);
    [[nodiscard]] static RegistrationCommand engageCoupler(model::CouplerId c);
    [[nodiscard]] static RegistrationCommand disengageCoupler(model::CouplerId c);
    [[nodiscard]] static RegistrationCommand toggleCoupler(model::CouplerId c);
    [[nodiscard]] static RegistrationCommand recallSnapshot(RegistrationState state);
    [[nodiscard]] static RegistrationCommand applyIntent(RegistrationIntent i);

    /// @return a short human description, e.g. "engage selector". Off-thread.
    [[nodiscard]] std::string describe() const;
};

} // namespace caecilia::registration
