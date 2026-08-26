// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/registration/PlenumBuilder.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"

#include <vector>

namespace caecilia::registration
{

StopSet DefaultPlenumBuilder::build(const model::Organ& spec,
                                    const PlenumSpec& request,
                                    const RegistrationState& current) const
{
    std::vector<core::StopId> chosen;
    chosen.reserve(spec.stops().size());

    for (const auto& stop : spec.stops())
    {
        if (request.division && !(stop.division() == *request.division))
            continue;

        const bool isMixture = stop.family() == core::TonalFamily::Mixture || stop.isCompound();
        const bool isReed    = stop.family() == core::TonalFamily::Reed;
        const bool isPrincipal = stop.family() == core::TonalFamily::Principal;

        if (isMixture)
        {
            // The mixture crown: admitted whole (its internal ranks include the
            // quints/tierces that belong there by design).
            if (request.includeMixtures)
                chosen.push_back(stop.id());
            continue;
        }

        if (isReed)
        {
            // Only chorus/foundation reeds join the plenum; solo/colour reeds
            // (Vox Humana, Oboe used as a solo) stay out.
            const bool chorusReed = stop.role() == core::ChorusRole::Foundation
                                    || stop.role() == core::ChorusRole::Chorus;
            if (request.includeChorusReeds && chorusReed)
                chosen.push_back(stop.id());
            continue;
        }

        if (!isPrincipal)
            continue; // flutes/strings/hybrids are not part of a diapason chorus

        if (request.excludeMutations && stop.footage().isMutation())
            continue;

        const std::int32_t oc = stop.footage().octaveClassFrom8();
        if (oc < request.lowestOctaveClass || oc > request.highestOctaveClass)
            continue;

        chosen.push_back(stop.id());
    }

    StopSet plenum{std::span<const core::StopId>{chosen}};

    // An additive build folds in whatever is already drawn (within scope). A
    // replacing build is realised by the engine clearing the scope first, so we
    // only need to union the current engaged stops here when additive.
    if (request.additive)
        plenum.uniteWith(current.engagedStops());

    return plenum;
}

} // namespace caecilia::registration
