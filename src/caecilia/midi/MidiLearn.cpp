/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/midi/MidiLearn.h"

namespace caecilia::midi
{

void MidiLearn::arm(const RegistrationCommandTemplate& target) noexcept
{
    target_   = target;
    captured_ = MidiLearnBinding{};
    state_    = State::Armed;
}

void MidiLearn::cancel() noexcept
{
    target_   = RegistrationCommandTemplate{};
    captured_ = MidiLearnBinding{};
    state_    = State::Idle;
}

bool MidiLearn::observe(const MidiEvent& ev) noexcept
{
    if (state_ != State::Armed)
        return false;

    // Only capture on a deliberate actuation edge.
    const bool actuated =
        ev.isNoteOn()
        || ev.type == MidiMessageType::ProgramChange
        || (ev.type == MidiMessageType::ControlChange && ev.data2 > 0);
    if (!actuated)
        return false;

    MidiLearnBinding binding;
    binding.source  = sourceFromEvent(ev);
    binding.command = target_;
    if (binding.source.kind == MidiSource::Kind::None)
        return false;

    captured_ = binding;
    state_    = State::Captured;
    return true;
}

MidiLearnBinding MidiLearn::takeCaptured() noexcept
{
    const MidiLearnBinding result = captured_;
    target_   = RegistrationCommandTemplate{};
    captured_ = MidiLearnBinding{};
    state_    = State::Idle;
    return result;
}

MidiSource MidiLearn::sourceFromEvent(const MidiEvent& ev) noexcept
{
    MidiSource src;
    src.channel = ev.channel;
    switch (ev.type)
    {
        case MidiMessageType::NoteOn:
        case MidiMessageType::NoteOff:
            src.kind  = MidiSource::Kind::Note;
            src.data1 = ev.data1;
            break;
        case MidiMessageType::ControlChange:
            src.kind  = MidiSource::Kind::ControlChange;
            src.data1 = ev.data1;
            break;
        case MidiMessageType::ProgramChange:
            src.kind  = MidiSource::Kind::ProgramChange;
            src.data1 = ev.data1;
            break;
        default:
            src.kind = MidiSource::Kind::None;
            break;
    }
    return src;
}

} // namespace caecilia::midi
