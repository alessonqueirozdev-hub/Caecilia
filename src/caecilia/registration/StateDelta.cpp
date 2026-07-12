/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/registration/StateDelta.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace caecilia::registration
{
namespace
{
constexpr bool couplerLess(model::CouplerId a, model::CouplerId b) noexcept
{
    return a.value < b.value;
}
} // namespace

StateDelta computeDelta(const RegistrationState& from, const RegistrationState& to)
{
    StateDelta delta;

    // --- Stops: engage = to - from, disengage = from - to -------------------
    std::vector<core::StopId> engage;
    std::set_difference(to.stops.begin(), to.stops.end(),
                        from.stops.begin(), from.stops.end(),
                        std::back_inserter(engage), StopSet::less);

    std::vector<core::StopId> disengage;
    std::set_difference(from.stops.begin(), from.stops.end(),
                        to.stops.begin(), to.stops.end(),
                        std::back_inserter(disengage), StopSet::less);

    // --- Couplers -----------------------------------------------------------
    std::vector<model::CouplerId> couplerEngage;
    std::set_difference(to.couplers.begin(), to.couplers.end(),
                        from.couplers.begin(), from.couplers.end(),
                        std::back_inserter(couplerEngage), couplerLess);

    std::vector<model::CouplerId> couplerDisengage;
    std::set_difference(from.couplers.begin(), from.couplers.end(),
                        to.couplers.begin(), to.couplers.end(),
                        std::back_inserter(couplerDisengage), couplerLess);

    // --- Pack into the fixed-capacity POD, flagging any overflow ------------
    const auto pack = [&delta](const auto& src, auto& dst, std::size_t& count,
                               std::size_t cap)
    {
        const std::size_t n = std::min(src.size(), cap);
        for (std::size_t i = 0; i < n; ++i)
            dst[i] = src[i];
        count = n;
        if (src.size() > cap)
            delta.truncated = true;
    };

    pack(engage, delta.stopsToEngage, delta.numEngage, StateDelta::kMaxStopChanges);
    pack(disengage, delta.stopsToDisengage, delta.numDisengage, StateDelta::kMaxStopChanges);
    pack(couplerEngage, delta.couplersToEngage, delta.numCouplerEngage,
         StateDelta::kMaxCouplerChanges);
    pack(couplerDisengage, delta.couplersToDisengage, delta.numCouplerDisengage,
         StateDelta::kMaxCouplerChanges);

    return delta;
}

} // namespace caecilia::registration
