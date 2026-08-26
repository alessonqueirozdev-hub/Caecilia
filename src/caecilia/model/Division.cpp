// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
