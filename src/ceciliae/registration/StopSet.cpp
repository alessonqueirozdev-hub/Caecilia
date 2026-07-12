/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/registration/StopSet.h"

#include <algorithm>
#include <iterator>

namespace ceciliae::registration
{

StopSet::StopSet(std::span<const core::StopId> ids)
    : ids_(ids.begin(), ids.end())
{
    std::sort(ids_.begin(), ids_.end(), less);
    ids_.erase(std::unique(ids_.begin(), ids_.end(),
                           [](core::StopId a, core::StopId b) noexcept
                           { return a.value == b.value; }),
               ids_.end());
}

bool StopSet::contains(core::StopId id) const noexcept
{
    return std::binary_search(ids_.begin(), ids_.end(), id, less);
}

bool StopSet::insert(core::StopId id)
{
    const auto it = std::lower_bound(ids_.begin(), ids_.end(), id, less);
    if (it != ids_.end() && it->value == id.value)
        return false;
    ids_.insert(it, id);
    return true;
}

bool StopSet::erase(core::StopId id)
{
    const auto it = std::lower_bound(ids_.begin(), ids_.end(), id, less);
    if (it == ids_.end() || it->value != id.value)
        return false;
    ids_.erase(it);
    return true;
}

bool StopSet::toggle(core::StopId id)
{
    if (erase(id))
        return false;
    insert(id);
    return true;
}

StopSet& StopSet::uniteWith(const StopSet& other)
{
    std::vector<core::StopId> merged;
    merged.reserve(ids_.size() + other.ids_.size());
    std::set_union(ids_.begin(), ids_.end(),
                   other.ids_.begin(), other.ids_.end(),
                   std::back_inserter(merged), less);
    ids_.swap(merged);
    return *this;
}

StopSet& StopSet::intersectWith(const StopSet& other)
{
    std::vector<core::StopId> merged;
    merged.reserve(std::min(ids_.size(), other.ids_.size()));
    std::set_intersection(ids_.begin(), ids_.end(),
                          other.ids_.begin(), other.ids_.end(),
                          std::back_inserter(merged), less);
    ids_.swap(merged);
    return *this;
}

StopSet& StopSet::subtract(const StopSet& other)
{
    std::vector<core::StopId> merged;
    merged.reserve(ids_.size());
    std::set_difference(ids_.begin(), ids_.end(),
                        other.ids_.begin(), other.ids_.end(),
                        std::back_inserter(merged), less);
    ids_.swap(merged);
    return *this;
}

StopSet operator|(StopSet a, const StopSet& b) { return a.uniteWith(b); }
StopSet operator&(StopSet a, const StopSet& b) { return a.intersectWith(b); }
StopSet operator-(StopSet a, const StopSet& b) { return a.subtract(b); }

} // namespace ceciliae::registration
