/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/model/Division.h"

namespace caecilia::model
{

void Division::setCompass(core::MidiNote low, core::MidiNote high) noexcept
{
    if (low <= high)
    {
        lowNote_  = low;
        highNote_ = high;
    }
    else
    {
        lowNote_  = high;
        highNote_ = low;
    }
}

} // namespace caecilia::model
