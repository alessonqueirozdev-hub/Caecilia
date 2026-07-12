/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/registration/RegistrationIntent.h"

#include "ceciliae/model/Organ.h"
#include "ceciliae/model/Stop.h"

namespace ceciliae::registration
{

bool PortResult::allEssentialsMatched() const noexcept
{
    for (const auto& o : outcomes)
        if (o.essential && o.matchedCount == 0)
            return false;
    return true;
}

RegistrationIntent RegistrationIntent::captureFrom(const model::Organ& spec,
                                                   const RegistrationState& state)
{
    RegistrationIntent intent;
    intent.sourceOrganName = spec.name();

    for (const core::StopId id : state.engagedStops())
    {
        const model::Stop* stop = spec.stop(id);
        if (stop == nullptr)
            continue;

        // Build a `family & pitch & div` clause describing this stop semantically.
        StopQuery q;
        q.family  = stop->family();
        q.footage = stop->footage();
        if (const model::Division* div = spec.division(stop->division()))
            q.divisionName = div->name();

        Item item;
        item.selector   = Selector::atom(std::move(q));
        item.label      = stop->displayName();
        item.importance = 1.0f;
        // Foundations are treated as essential to the character of the sound.
        item.essential  = stop->role() == core::ChorusRole::Foundation;
        intent.items.push_back(std::move(item));
    }

    return intent;
}

PortResult port(const RegistrationIntent& intent, const model::Organ& targetSpec)
{
    PortResult result;
    const RegistrationState emptyState; // intent is absolute, not relative

    float weightedScore = 0.0f;
    float totalWeight   = 0.0f;

    for (const auto& item : intent.items)
    {
        const StopSet matched = item.selector.resolve(targetSpec, emptyState);

        PortResult::ItemOutcome outcome;
        outcome.label        = item.label;
        outcome.matchedCount = matched.size();
        outcome.essential    = item.essential;
        // A clause scores 1.0 if it found any stop, 0.0 otherwise. Essential
        // misses drag the weighted average harder via a bigger effective weight.
        outcome.score        = matched.empty() ? 0.0f : 1.0f;

        const float weight = item.importance * (item.essential ? 2.0f : 1.0f);
        weightedScore += outcome.score * weight;
        totalWeight   += weight;

        result.resolved.uniteWith(matched);
        result.outcomes.push_back(std::move(outcome));
    }

    result.confidence = totalWeight > 0.0f ? (weightedScore / totalWeight) : 1.0f;

    // TODO(phase8): graded per-clause scoring — reward an exact footage+division
    // hit over a same-family fallback, and factor a division-remapping table
    // (Swell->Récit, Choir->Positif) into the confidence rather than 0/1 matching.
    return result;
}

} // namespace ceciliae::registration
