/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/midi/MidiMap.h"

namespace ceciliae::midi
{

MidiMap::MidiMap() noexcept
    : sequencerNav_(SequencerNavMap::withUserDefaults())
{
    // Velocity defaults to a Fixed curve (organ-classic): a keypress on any
    // manual sounds at a constant level regardless of how hard the key is struck.
    // TODO(phase8): seed channels_ from the loaded OrganSpec's manual layout so a
    // freshly loaded organ is immediately playable without manual configuration.
}

std::size_t MidiMap::installBinding(const MidiLearnBinding& binding) noexcept
{
    if (!binding.isValid())
        return kMaxLearnBindings;

    // Replace an existing binding on the same physical source.
    for (std::size_t i = 0; i < bindingCount_; ++i)
    {
        if (bindings_[i].source == binding.source)
        {
            bindings_[i] = binding;
            return i;
        }
    }

    if (bindingCount_ >= kMaxLearnBindings)
        return kMaxLearnBindings; // Table full.

    const std::size_t slot = bindingCount_;
    bindings_[slot]        = binding;
    ++bindingCount_;
    return slot;
}

void MidiMap::removeBindingAt(std::size_t index) noexcept
{
    if (index >= bindingCount_)
        return;

    // Compact the tail down so [0, bindingCount_) stays dense.
    for (std::size_t i = index + 1; i < bindingCount_; ++i)
        bindings_[i - 1] = bindings_[i];

    --bindingCount_;
    bindings_[bindingCount_] = MidiLearnBinding{};
}

void MidiMap::clearBindings() noexcept
{
    for (std::size_t i = 0; i < bindingCount_; ++i)
        bindings_[i] = MidiLearnBinding{};
    bindingCount_ = 0;
}

const MidiLearnBinding* MidiMap::findBinding(const MidiEvent& ev) const noexcept
{
    for (std::size_t i = 0; i < bindingCount_; ++i)
    {
        if (bindings_[i].source.matches(ev))
            return &bindings_[i];
    }
    return nullptr;
}

} // namespace ceciliae::midi
