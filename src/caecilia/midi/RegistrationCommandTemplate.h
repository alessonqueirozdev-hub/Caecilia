/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/midi/MidiTypes.h"

#include <cstdint>
#include <string_view>

namespace caecilia::midi
{

/// The semantic action a MIDI control requests of the registration brain.
enum class RegistrationVerb : std::uint8_t
{
    None,                ///< No-op.
    Engage,              ///< Draw the stops matched by @c selector.
    Disengage,           ///< Retire the stops matched by @c selector.
    Toggle,              ///< Flip the stops matched by @c selector.
    RecallGeneral,       ///< Recall general combination @c index.
    RecallDivisional,    ///< Recall divisional @c index within @c division.
    SequencerNext,       ///< Advance the sequencer one step.
    SequencerPrevious,   ///< Step the sequencer back one.
    SequencerFirst,      ///< Jump the sequencer to its first step.
    SequencerLast,       ///< Jump the sequencer to its last step.
    BuildPlenum,         ///< Ask the plenum builder to construct a chorus.
    ClearAll,            ///< Retire every drawn stop.
    Undo,                ///< Undo the last registration command.
    Redo,                ///< Redo the last undone registration command.
    SetCrescendoStep     ///< Move the crescendo/Walze to @c index.
};

/**
 * @brief A resolved, portable description of a registration action a MIDI
 *        control fires — but NOT yet applied.
 *
 * The MIDI module deliberately does not run registration logic (that is
 * off-thread work owned by the @c registration module). Instead every MIDI
 * control resolves to one of these templates, which the off-thread registration
 * engine consumes: for selector-based verbs it parses @ref selector with the
 * shared grammar; for indexed verbs it uses @ref index / @ref division.
 *
 * Because it embeds a fixed-capacity @ref SelectorExpr it is trivially copyable
 * and can be handed across the command ring or stored in the RT-read learn table
 * without allocation.
 */
struct RegistrationCommandTemplate
{
    RegistrationVerb verb     = RegistrationVerb::None; ///< What to do.
    SelectorExpr     selector{};                        ///< Target set (selector-based verbs).
    std::uint16_t    index    = 0;                      ///< General/divisional/crescendo index.
    core::DivisionId division{};                        ///< Scope for divisional verbs.

    /// @return true if this template does nothing.
    [[nodiscard]] constexpr bool isNoOp() const noexcept
    {
        return verb == RegistrationVerb::None;
    }

    // --- factory helpers ----------------------------------------------------

    [[nodiscard]] static RegistrationCommandTemplate engage(std::string_view selectorText) noexcept
    {
        return {RegistrationVerb::Engage, SelectorExpr::from(selectorText), 0, {}};
    }

    [[nodiscard]] static RegistrationCommandTemplate disengage(std::string_view selectorText) noexcept
    {
        return {RegistrationVerb::Disengage, SelectorExpr::from(selectorText), 0, {}};
    }

    [[nodiscard]] static RegistrationCommandTemplate toggle(std::string_view selectorText) noexcept
    {
        return {RegistrationVerb::Toggle, SelectorExpr::from(selectorText), 0, {}};
    }

    [[nodiscard]] static RegistrationCommandTemplate recallGeneral(std::uint16_t generalIndex) noexcept
    {
        return {RegistrationVerb::RecallGeneral, {}, generalIndex, {}};
    }

    [[nodiscard]] static RegistrationCommandTemplate recallDivisional(core::DivisionId div,
                                                                      std::uint16_t divisionalIndex) noexcept
    {
        return {RegistrationVerb::RecallDivisional, {}, divisionalIndex, div};
    }

    [[nodiscard]] static RegistrationCommandTemplate sequencer(SequencerDirection dir) noexcept
    {
        RegistrationCommandTemplate t;
        switch (dir)
        {
            case SequencerDirection::Previous: t.verb = RegistrationVerb::SequencerPrevious; break;
            case SequencerDirection::Next:     t.verb = RegistrationVerb::SequencerNext;     break;
            case SequencerDirection::First:    t.verb = RegistrationVerb::SequencerFirst;    break;
            case SequencerDirection::Last:     t.verb = RegistrationVerb::SequencerLast;     break;
            case SequencerDirection::None:
            default:                           t.verb = RegistrationVerb::None;              break;
        }
        return t;
    }

    friend bool operator==(const RegistrationCommandTemplate&,
                           const RegistrationCommandTemplate&) noexcept = default;
};

} // namespace caecilia::midi
