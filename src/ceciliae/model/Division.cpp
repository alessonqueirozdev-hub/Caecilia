/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/model/Division.h"

namespace ceciliae::model
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

} // namespace ceciliae::model
