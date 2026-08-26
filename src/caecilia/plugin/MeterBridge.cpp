// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/MeterBridge.h"

#include <cmath>

namespace caecilia::plugin
{

float MeterBridge::linearToDb(float linear, float floorDb) noexcept
{
    if (linear <= 0.0f)
        return floorDb;
    const float db = 20.0f * std::log10(linear);
    return db < floorDb ? floorDb : db;
}

} // namespace caecilia::plugin
