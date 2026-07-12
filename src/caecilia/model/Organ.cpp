/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/model/Organ.h"

namespace caecilia::model
{

const Windchest* Organ::windchest(core::WindchestId id) const noexcept
{
    return id.value < windchests_.size() ? &windchests_[id.value] : nullptr;
}

const Rank* Organ::rank(core::RankId id) const noexcept
{
    return id.value < ranks_.size() ? &ranks_[id.value] : nullptr;
}

const Stop* Organ::stop(core::StopId id) const noexcept
{
    return id.value < stops_.size() ? &stops_[id.value] : nullptr;
}

const Division* Organ::division(core::DivisionId id) const noexcept
{
    return id.value < divisions_.size() ? &divisions_[id.value] : nullptr;
}

const Coupler* Organ::coupler(CouplerId id) const noexcept
{
    return id.value < couplers_.size() ? &couplers_[id.value] : nullptr;
}

const Pipe* Organ::pipeForKey(core::StopId stopId, core::MidiNote note) const noexcept
{
    const Stop* s = stop(stopId);
    if (s == nullptr)
        return nullptr;

    const Rank* r = rank(s->rank());
    if (r == nullptr)
        return nullptr;

    return r->pipeFor(note);
}

std::size_t Organ::collectPipesForKey(core::DivisionId division,
                                      core::MidiNote note,
                                      std::span<const core::StopId> engaged,
                                      std::span<core::PipeId> out) const noexcept
{
    std::size_t written = 0;

    for (const core::StopId stopId : engaged)
    {
        if (written >= out.size())
            break;

        const Stop* s = stop(stopId);
        if (s == nullptr || !(s->division() == division))
            continue;

        const Pipe* p = pipeForKey(stopId, note);
        if (p == nullptr)
            continue;

        out[written] = p->id;
        ++written;
    }

    return written;
}

} // namespace caecilia::model
