// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/RegistrationState.h"

#include <algorithm>

namespace caecilia::registration
{
namespace
{
constexpr bool couplerLess(model::CouplerId a, model::CouplerId b) noexcept
{
    return a.value < b.value;
}
} // namespace

bool RegistrationState::isCouplerEngaged(model::CouplerId id) const noexcept
{
    return std::binary_search(couplers.begin(), couplers.end(), id, couplerLess);
}

bool RegistrationState::engageCoupler(model::CouplerId id)
{
    const auto it = std::lower_bound(couplers.begin(), couplers.end(), id, couplerLess);
    if (it != couplers.end() && it->value == id.value)
        return false;
    couplers.insert(it, id);
    return true;
}

bool RegistrationState::disengageCoupler(model::CouplerId id)
{
    const auto it = std::lower_bound(couplers.begin(), couplers.end(), id, couplerLess);
    if (it == couplers.end() || it->value != id.value)
        return false;
    couplers.erase(it);
    return true;
}

bool RegistrationState::toggleCoupler(model::CouplerId id)
{
    if (disengageCoupler(id))
        return false;
    engageCoupler(id);
    return true;
}

void RegistrationState::clearAll() noexcept
{
    stops.clear();
    couplers.clear();
}

} // namespace caecilia::registration
