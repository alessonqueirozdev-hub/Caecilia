// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/wind/OrganWind.h"

#include "caecilia/model/Organ.h"
#include "caecilia/model/Windchest.h"

namespace caecilia::wind
{

WindModelConfig configFromOrgan(const model::Organ& organ)
{
    WindModelConfig cfg;

    const std::vector<model::Windchest>& chests = organ.windchests();
    if (chests.empty())
        return makeSingleChestConfig();

    // One reservoir for the instrument. Its nominal is the highest pressure any
    // chest asks for, because a regulator can drop pressure to a chest but cannot
    // raise it above what the blower supplies.
    BellowsConfig bellows;
    bellows.nominalPressurePa = 0.0f;
    for (const model::Windchest& c : chests)
        bellows.nominalPressurePa = c.nominalPressurePa > bellows.nominalPressurePa
                                  ? c.nominalPressurePa
                                  : bellows.nominalPressurePa;
    // The plate has mass. A chord landing on this reservoir drops it, past the
    // new pressure and back, a few times before it settles -- and every pipe on it
    // goes flat, quiet and dull in time with the bounce, because pitch, level,
    // brightness and speech all track the deviation.
    //
    // Four and a half hertz for a reservoir of this size, and a damping that lets
    // it ring for about two cycles: enough that a full chord is heard to land on
    // the wind rather than merely to lower it, and not so much that the instrument
    // sounds out of regulation. A real organ's number is a property of the
    // reservoir, so this belongs to the instrument and not to the class default --
    // which stays at zero, i.e. the first-order behaviour, for every caller that
    // has not thought about it.
    bellows.plateResonanceHz = 4.5f;
    bellows.plateDamping     = 0.45f;

    cfg.bellows.push_back(bellows);

    for (const model::Windchest& c : chests)
    {
        WindchestConfig wc;
        wc.id                = c.id;
        wc.bellowsIndex      = 0;
        wc.nominalPressurePa = c.nominalPressurePa;

        // A tremulant belongs to the chest that has one. Indices run in emission
        // order, which is what the engine addresses them by.
        if (c.hasTremulant)
        {
            wc.tremulantIndex = static_cast<std::int32_t>(cfg.tremulants.size());
            cfg.tremulants.push_back(TremulantConfig{});
        }
        else
        {
            wc.tremulantIndex = -1;
        }

        cfg.chests.push_back(wc);
    }

    // Rank routing. Without it every rank falls back to chest 0, which would put
    // the whole instrument on one tremulant and one trunk -- the Récit's tremulant
    // would shake the Grand Orgue.
    for (const model::Windchest& c : chests)
        for (const core::RankId rank : c.ranks)
            cfg.pipeBindings.push_back(PipeChestBinding{ rank, c.id });

    return cfg;
}

} // namespace caecilia::wind
