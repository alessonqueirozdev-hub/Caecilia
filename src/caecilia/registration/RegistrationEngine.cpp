/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/registration/RegistrationEngine.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <utility>

namespace caecilia::registration
{

RegistrationEngine::RegistrationEngine()
    : plenumBuilder_(&defaultPlenumBuilder_)
{
}

void RegistrationEngine::setOrgan(const model::Organ* spec)
{
    spec_ = spec;
    state_ = RegistrationState{};
    history_.reset(state_);
    provenance_.clear();
}

bool RegistrationEngine::stopInDivision(core::StopId stop, core::DivisionId division) const
{
    if (spec_ == nullptr)
        return false;
    const model::Stop* s = spec_->stop(stop);
    return s != nullptr && s->division() == division;
}

RegistrationState RegistrationEngine::computeResult(const RegistrationState& base,
                                                    const RegistrationCommand& command) const
{
    using Kind = RegistrationCommand::Kind;
    RegistrationState next = base;

    const auto resolve = [&](const Selector& sel) -> StopSet
    {
        return spec_ != nullptr ? sel.resolve(*spec_, base) : StopSet{};
    };

    switch (command.kind)
    {
        case Kind::EngageStop:    next.engage(command.stop);    break;
        case Kind::DisengageStop: next.disengage(command.stop); break;
        case Kind::ToggleStop:    next.toggle(command.stop);    break;

        case Kind::EngageSelector:
            for (const core::StopId id : resolve(command.selector))
                next.engage(id);
            break;

        case Kind::DisengageSelector:
            for (const core::StopId id : resolve(command.selector))
                next.disengage(id);
            break;

        case Kind::ToggleSelector:
            for (const core::StopId id : resolve(command.selector))
                next.toggle(id);
            break;

        case Kind::Solo:
        {
            const StopSet matched = resolve(command.selector);
            if (command.scope)
            {
                for (const core::StopId id : base.engagedStops())
                    if (stopInDivision(id, *command.scope))
                        next.disengage(id);
                for (const core::StopId id : matched)
                    if (stopInDivision(id, *command.scope))
                        next.engage(id);
            }
            else
            {
                next.stops = matched; // couplers untouched
            }
            break;
        }

        case Kind::Clear:
            if (command.scope)
            {
                for (const core::StopId id : base.engagedStops())
                    if (stopInDivision(id, *command.scope))
                        next.disengage(id);
            }
            else
            {
                next.clearAll();
            }
            break;

        case Kind::BuildPlenum:
        {
            const StopSet built = (spec_ != nullptr && plenumBuilder_ != nullptr)
                                      ? plenumBuilder_->build(*spec_, command.plenum, base)
                                      : StopSet{};
            if (command.plenum.additive)
            {
                next.stops = built; // builder already folded in the current stops
            }
            else if (command.plenum.division)
            {
                for (const core::StopId id : base.engagedStops())
                    if (stopInDivision(id, *command.plenum.division))
                        next.disengage(id);
                for (const core::StopId id : built)
                    next.engage(id);
            }
            else
            {
                next.stops = built;
            }
            break;
        }

        case Kind::EngageCoupler:    next.engageCoupler(command.coupler);    break;
        case Kind::DisengageCoupler: next.disengageCoupler(command.coupler); break;
        case Kind::ToggleCoupler:    next.toggleCoupler(command.coupler);    break;

        case Kind::RecallSnapshot:
            next = command.snapshot;
            break;

        case Kind::ApplyIntent:
            if (spec_ != nullptr)
            {
                const PortResult pr = ::caecilia::registration::port(command.intent, *spec_);
                next.stops = pr.resolved; // couplers untouched
            }
            break;
    }

    return next;
}

void RegistrationEngine::updateProvenance(const RegistrationState& previous,
                                          const RegistrationState& newState,
                                          const RegistrationCommand& command)
{
    using Kind = RegistrationCommand::Kind;

    // Drop provenance for stops that are no longer engaged.
    for (const core::StopId id : previous.engagedStops())
        if (!newState.isEngaged(id))
            provenance_.erase(id.value);

    const std::size_t commandIndex = history_.size(); // index the new node will take

    for (const core::StopId id : newState.engagedStops())
    {
        // Only (re)stamp stops that just changed or lack a record.
        if (previous.isEngaged(id) && provenance_.find(id.value) != provenance_.end())
            continue;

        Explanation ex;
        ex.id           = id;
        ex.commandIndex = commandIndex;

        switch (command.kind)
        {
            case Kind::BuildPlenum:
            {
                ex.reason = Explanation::Reason::PlenumPrincipal;
                ex.detail = "plenum";
                if (spec_ != nullptr)
                {
                    if (const model::Stop* s = spec_->stop(id))
                    {
                        if (s->family() == core::TonalFamily::Mixture || s->isCompound())
                            ex.reason = Explanation::Reason::PlenumMixture;
                        else if (s->family() == core::TonalFamily::Reed)
                            ex.reason = Explanation::Reason::PlenumReed;
                    }
                }
                break;
            }
            case Kind::EngageSelector:
            case Kind::ToggleSelector:
            case Kind::Solo:
                ex.reason = Explanation::Reason::SelectorMatch;
                ex.detail = "matched a selector";
                break;
            case Kind::ApplyIntent:
                ex.reason = Explanation::Reason::IntentPort;
                ex.detail = "ported intent";
                break;
            case Kind::RecallSnapshot:
                ex.reason = Explanation::Reason::SnapshotRecall;
                ex.detail = "recalled snapshot";
                break;
            default:
                ex.reason = Explanation::Reason::DirectCommand;
                ex.detail = "drawn directly";
                break;
        }
        provenance_[id.value] = std::move(ex);
    }
}

StateDelta RegistrationEngine::commitState(RegistrationState newState,
                                           const RegistrationCommand& command)
{
    StateDelta delta = computeDelta(state_, newState);
    updateProvenance(state_, newState, command);
    state_ = std::move(newState);
    history_.record(command, state_);
    return delta;
}

StateDelta RegistrationEngine::apply(const RegistrationCommand& command)
{
    RegistrationState next = computeResult(state_, command);
    return commitState(std::move(next), command);
}

StateDelta RegistrationEngine::undo()
{
    RegistrationState target = history_.undo(); // copy; cursor moved
    StateDelta        delta  = computeDelta(state_, target);
    state_                   = std::move(target);
    // TODO(phase7): restore per-stop provenance captured at each history node so
    // explain() reflects the node's original cause after an undo/redo, rather
    // than being conservatively rebuilt.
    return delta;
}

StateDelta RegistrationEngine::redo()
{
    RegistrationState target = history_.redo(); // copy; cursor moved
    StateDelta        delta  = computeDelta(state_, target);
    state_                   = std::move(target);
    return delta;
}

StopSet RegistrationEngine::query(const Selector& selector) const
{
    return spec_ != nullptr ? selector.resolve(*spec_, state_) : StopSet{};
}

Explanation RegistrationEngine::explain(core::StopId stop) const
{
    if (!state_.isEngaged(stop))
        return Explanation{stop, Explanation::Reason::NotEngaged, "not drawn", 0};

    const auto it = provenance_.find(stop.value);
    if (it != provenance_.end())
        return it->second;

    return Explanation{stop, Explanation::Reason::DirectCommand, "engaged", 0};
}

PortResult RegistrationEngine::port(const RegistrationIntent& intent) const
{
    if (spec_ == nullptr)
        return PortResult{};
    return ::caecilia::registration::port(intent, *spec_);
}

RegistrationIntent RegistrationEngine::captureIntent() const
{
    if (spec_ == nullptr)
        return RegistrationIntent{};
    return RegistrationIntent::captureFrom(*spec_, state_);
}

} // namespace caecilia::registration
