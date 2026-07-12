/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/registration/RegistrationCommand.h"
#include "ceciliae/registration/RegistrationIntent.h"
#include "ceciliae/registration/RegistrationState.h"

#include <string>
#include <utility>
#include <variant>

namespace ceciliae::registration
{

/**
 * @brief A combination piston / general: a recallable registration that stores
 *        EITHER a literal snapshot OR a portable intent.
 *
 * A @c Mode::Snapshot piston reproduces an exact set of stops on the organ it was
 * captured on (the classic console behaviour). A @c Mode::Intent piston stores a
 * @c RegistrationIntent instead, so recalling it on a *different* organ resolves
 * the semantically-nearest registration — the same portability the engine's
 * @c port() exposes, surfaced as a first-class console control.
 *
 * A piston lowers to a single @c RegistrationCommand, so recall flows through the
 * exact same apply/undo path as every other operation.
 */
struct Piston
{
    /// Which flavour of registration this piston stores.
    enum class Mode { Snapshot, Intent };

    Mode                                             mode = Mode::Snapshot;
    std::variant<RegistrationState, RegistrationIntent> payload{RegistrationState{}};
    std::string                                      label; ///< Console label, e.g. "Gen 1".

    /// Capture a literal snapshot piston.
    [[nodiscard]] static Piston fromSnapshot(RegistrationState state, std::string label = {})
    {
        Piston p;
        p.mode    = Mode::Snapshot;
        p.payload = std::move(state);
        p.label   = std::move(label);
        return p;
    }

    /// Capture a portable intent piston.
    [[nodiscard]] static Piston fromIntent(RegistrationIntent intent, std::string label = {})
    {
        Piston p;
        p.mode    = Mode::Intent;
        p.payload = std::move(intent);
        p.label   = std::move(label);
        return p;
    }

    /// @return the command that recalls this piston through the normal apply path.
    [[nodiscard]] RegistrationCommand toCommand() const
    {
        if (mode == Mode::Snapshot)
            return RegistrationCommand::recallSnapshot(std::get<RegistrationState>(payload));
        return RegistrationCommand::applyIntent(std::get<RegistrationIntent>(payload));
    }
};

} // namespace ceciliae::registration
