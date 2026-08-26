// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/RegistrationCommand.h"

#include <utility>

namespace caecilia::registration
{

RegistrationCommand RegistrationCommand::engageStop(core::StopId s)
{
    RegistrationCommand c;
    c.kind = Kind::EngageStop;
    c.stop = s;
    return c;
}

RegistrationCommand RegistrationCommand::disengageStop(core::StopId s)
{
    RegistrationCommand c;
    c.kind = Kind::DisengageStop;
    c.stop = s;
    return c;
}

RegistrationCommand RegistrationCommand::toggleStop(core::StopId s)
{
    RegistrationCommand c;
    c.kind = Kind::ToggleStop;
    c.stop = s;
    return c;
}

RegistrationCommand RegistrationCommand::engageSelector(Selector sel)
{
    RegistrationCommand c;
    c.kind     = Kind::EngageSelector;
    c.selector = std::move(sel);
    return c;
}

RegistrationCommand RegistrationCommand::disengageSelector(Selector sel)
{
    RegistrationCommand c;
    c.kind     = Kind::DisengageSelector;
    c.selector = std::move(sel);
    return c;
}

RegistrationCommand RegistrationCommand::toggleSelector(Selector sel)
{
    RegistrationCommand c;
    c.kind     = Kind::ToggleSelector;
    c.selector = std::move(sel);
    return c;
}

RegistrationCommand RegistrationCommand::solo(Selector sel, std::optional<core::DivisionId> scope)
{
    RegistrationCommand c;
    c.kind     = Kind::Solo;
    c.selector = std::move(sel);
    c.scope    = scope;
    return c;
}

RegistrationCommand RegistrationCommand::clear(std::optional<core::DivisionId> scope)
{
    RegistrationCommand c;
    c.kind  = Kind::Clear;
    c.scope = scope;
    return c;
}

RegistrationCommand RegistrationCommand::buildPlenum(PlenumSpec spec)
{
    RegistrationCommand c;
    c.kind   = Kind::BuildPlenum;
    c.plenum = std::move(spec);
    return c;
}

RegistrationCommand RegistrationCommand::engageCoupler(model::CouplerId cid)
{
    RegistrationCommand c;
    c.kind    = Kind::EngageCoupler;
    c.coupler = cid;
    return c;
}

RegistrationCommand RegistrationCommand::disengageCoupler(model::CouplerId cid)
{
    RegistrationCommand c;
    c.kind    = Kind::DisengageCoupler;
    c.coupler = cid;
    return c;
}

RegistrationCommand RegistrationCommand::toggleCoupler(model::CouplerId cid)
{
    RegistrationCommand c;
    c.kind    = Kind::ToggleCoupler;
    c.coupler = cid;
    return c;
}

RegistrationCommand RegistrationCommand::recallSnapshot(RegistrationState state)
{
    RegistrationCommand c;
    c.kind     = Kind::RecallSnapshot;
    c.snapshot = std::move(state);
    return c;
}

RegistrationCommand RegistrationCommand::applyIntent(RegistrationIntent i)
{
    RegistrationCommand c;
    c.kind   = Kind::ApplyIntent;
    c.intent = std::move(i);
    return c;
}

std::string RegistrationCommand::describe() const
{
    switch (kind)
    {
        case Kind::EngageStop:        return "engage stop";
        case Kind::DisengageStop:     return "disengage stop";
        case Kind::ToggleStop:        return "toggle stop";
        case Kind::EngageSelector:    return "engage selector";
        case Kind::DisengageSelector: return "disengage selector";
        case Kind::ToggleSelector:    return "toggle selector";
        case Kind::Solo:              return "solo selector";
        case Kind::Clear:             return "clear";
        case Kind::BuildPlenum:       return "build plenum";
        case Kind::EngageCoupler:     return "engage coupler";
        case Kind::DisengageCoupler:  return "disengage coupler";
        case Kind::ToggleCoupler:     return "toggle coupler";
        case Kind::RecallSnapshot:    return "recall snapshot";
        case Kind::ApplyIntent:       return "apply intent";
    }
    return "unknown command";
}

} // namespace caecilia::registration
