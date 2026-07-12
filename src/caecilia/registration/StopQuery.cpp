/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/registration/StopQuery.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <algorithm>
#include <cctype>

namespace caecilia::registration
{
namespace
{
/// Case-insensitive substring test. Off-thread helper (no allocation).
bool containsNoCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    const auto toLower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [&](char a, char b) { return toLower(static_cast<unsigned char>(a))
                                  == toLower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}
} // namespace

bool StopQuery::isUniversal() const noexcept
{
    return !family && !role && !pitchClass && !footage && !division
           && divisionName.empty() && nameContains.empty()
           && engaged == Engaged::Any && !mutationsOnly && !excludeMutations;
}

bool StopQuery::matches(const model::Stop& stop, bool isEngaged) const
{
    if (family && stop.family() != *family)
        return false;
    if (role && stop.role() != *role)
        return false;
    if (pitchClass && stop.pitchClass() != *pitchClass)
        return false;
    if (footage && !(stop.footage() == *footage))
        return false;
    if (division && !(stop.division() == *division))
        return false;
    if (mutationsOnly && !stop.footage().isMutation())
        return false;
    if (excludeMutations && stop.footage().isMutation())
        return false;
    if (engaged == Engaged::Only && !isEngaged)
        return false;
    if (engaged == Engaged::Exclude && isEngaged)
        return false;
    if (!nameContains.empty() && !containsNoCase(stop.name(), nameContains))
        return false;
    return true;
}

StopSet StopQuery::resolve(const model::Organ& spec, const RegistrationState& state) const
{
    // Resolve a textual division name to a concrete id once, up front.
    std::optional<core::DivisionId> resolvedDivision = division;
    if (!divisionName.empty() && !resolvedDivision)
    {
        for (const auto& div : spec.divisions())
        {
            if (containsNoCase(div.name(), divisionName))
            {
                resolvedDivision = div.id();
                break;
            }
        }
        // If a name was given but matched nothing, the query resolves to empty:
        // an unknown division cannot match any stop.
        if (!resolvedDivision)
            return StopSet{};
    }

    std::vector<core::StopId> hits;
    hits.reserve(spec.stops().size());
    for (const auto& stop : spec.stops())
    {
        if (resolvedDivision && !(stop.division() == *resolvedDivision))
            continue;
        if (matches(stop, state.isEngaged(stop.id())))
            hits.push_back(stop.id());
    }
    return StopSet{std::span<const core::StopId>{hits}};
}

} // namespace caecilia::registration
