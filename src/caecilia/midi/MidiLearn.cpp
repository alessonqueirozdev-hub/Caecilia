// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/midi/MidiLearn.h"

#include "caecilia/midi/LearnedControls.h"

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

    // Only capture on a deliberate actuation edge, and from the ONE definition of
    // what that is. The audio thread has to make the same judgement a block
    // earlier, to decide whether to swallow the event or let it sound; when the two
    // were separate copies, an event one accepted and the other rejected was a
    // learn that silently did not happen.
    if (! LearnedControls::isActuation(ev))
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
