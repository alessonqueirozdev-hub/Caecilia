/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/model/Rank.h"

#include <cmath>

namespace ceciliae::model
{

void Rank::setCompass(core::MidiNote low, core::MidiNote high) noexcept
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

const Pipe* Rank::pipeFor(core::MidiNote note) const noexcept
{
    if (!contains(note))
        return nullptr;

    const std::size_t index = static_cast<std::size_t>(note - lowNote_);
    return index < pipes_.size() ? &pipes_[index] : nullptr;
}

void Rank::generatePipes(double referenceA4Hz)
{
    pipes_.clear();
    if (highNote_ < lowNote_)
        return;

    const std::size_t count = static_cast<std::size_t>(highNote_ - lowNote_) + 1;
    pipes_.reserve(count);

    // Sounding pitch multiplier from the exact footage: an 8' rank sounds the key
    // pitch (x1), a 4' rank an octave higher (x2), a 2 2/3' quint a twelfth (x3).
    // multiplier = 8 / feet, kept exact via Footage::feet().
    const double feet = footage_.feet();
    const double footageMultiplier = feet > 0.0 ? (8.0 / feet) : 1.0;

    for (core::MidiNote note = lowNote_; ; ++note)
    {
        Pipe pipe;
        pipe.id      = core::PipeId{id_.value, note};
        pipe.spatial = baseSpatial_;

        // Equal-temperament default relative to A4; the ITuning table overrides
        // this at render time to honour the selected historical temperament.
        const double keyHz = referenceA4Hz * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
        pipe.nominalFrequencyHz = static_cast<float>(keyHz * footageMultiplier);

        pipes_.push_back(pipe);

        if (note == highNote_)
            break; // avoid MidiNote (uint8) overflow when highNote_ == 127
    }
}

} // namespace ceciliae::model
