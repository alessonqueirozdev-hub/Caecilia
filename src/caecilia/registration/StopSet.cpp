// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/StopSet.h"

#include "caecilia/core/ParameterIds.h"

#include <algorithm>
#include <iterator>

namespace caecilia::registration
{

// The comment on kMaskCapacity says the host stop pool is exactly this wide. Said
// in a comment it was true by inspection and by nothing else; said here it is a
// build failure the day one of them moves. They are not two numbers that happen
// to agree -- a stop the mask cannot hold is a parameter that can never do
// anything, and a parameter that does not exist is a stop nobody can draw.
static_assert(StopSet::kMaskCapacity == core::params::kMaxStopParameters,
              "The registration mask and the host stop-parameter pool must be "
              "the same width.");

std::uint64_t StopSet::toMask() const noexcept
{
    std::uint64_t mask = 0;
    for (const core::StopId id : ids_)
        if (id.value < kMaskCapacity)
            mask |= (std::uint64_t{1} << id.value);
    return mask;
}

StopSet StopSet::fromMask(std::uint64_t mask)
{
    StopSet out;
    // Ascending bit order gives ascending ids, so the invariant the whole class
    // rests on holds without a sort.
    for (std::size_t bit = 0; bit < kMaskCapacity; ++bit)
        if ((mask & (std::uint64_t{1} << bit)) != 0)
            out.ids_.push_back(core::StopId{ static_cast<std::uint16_t>(bit) });
    return out;
}

bool StopSet::fitsInMask() const noexcept
{
    // Sorted ascending, so only the last id can be out of range.
    return ids_.empty() || ids_.back().value < kMaskCapacity;
}

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

} // namespace caecilia::registration
