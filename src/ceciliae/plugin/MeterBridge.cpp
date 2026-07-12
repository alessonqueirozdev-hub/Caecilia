/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/plugin/MeterBridge.h"

#include <cmath>

namespace ceciliae::plugin
{

float MeterBridge::linearToDb(float linear, float floorDb) noexcept
{
    if (linear <= 0.0f)
        return floorDb;
    const float db = 20.0f * std::log10(linear);
    return db < floorDb ? floorDb : db;
}

} // namespace ceciliae::plugin
