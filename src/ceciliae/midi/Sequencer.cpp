/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/midi/Sequencer.h"

namespace ceciliae::midi
{

void Sequencer::clear() noexcept
{
    count_    = 0;
    position_ = kNoPosition;
    // Leave storage as-is; count_ gates all reads.
}

bool Sequencer::append(const SequencerStep& step) noexcept
{
    if (count_ >= kMaxSteps)
        return false;
    steps_[count_] = step;
    ++count_;
    return true;
}

bool Sequencer::replace(std::size_t index, const SequencerStep& step) noexcept
{
    if (index >= count_)
        return false;
    steps_[index] = step;
    return true;
}

SequencerStep Sequencer::next() noexcept
{
    if (count_ == 0)
        return SequencerStep{};

    if (position_ == kNoPosition)
        position_ = 0;
    else if (position_ + 1 < count_)
        ++position_; // Clamp at the last step.

    return steps_[position_];
}

SequencerStep Sequencer::previous() noexcept
{
    if (count_ == 0)
        return SequencerStep{};

    if (position_ == kNoPosition)
        position_ = 0;
    else if (position_ > 0)
        --position_; // Clamp at the first step.

    return steps_[position_];
}

SequencerStep Sequencer::first() noexcept
{
    if (count_ == 0)
        return SequencerStep{};
    position_ = 0;
    return steps_[position_];
}

SequencerStep Sequencer::last() noexcept
{
    if (count_ == 0)
        return SequencerStep{};
    position_ = count_ - 1;
    return steps_[position_];
}

SequencerStep Sequencer::goTo(std::size_t index) noexcept
{
    if (count_ == 0)
        return SequencerStep{};
    position_ = index < count_ ? index : count_ - 1;
    return steps_[position_];
}

SequencerStep Sequencer::current() const noexcept
{
    if (count_ == 0 || position_ == kNoPosition || position_ >= count_)
        return SequencerStep{};
    return steps_[position_];
}

} // namespace ceciliae::midi
