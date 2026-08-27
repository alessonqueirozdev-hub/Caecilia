// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/midi/LearnedControls.h"

#include "caecilia/midi/MidiMap.h"

namespace caecilia::midi
{

void LearnedControls::clear() noexcept
{
    notes_.fill(0);
    ccs_.fill(0);
    // swallowed_ deliberately survives: a key that is DOWN when the organist
    // rebinds something still owes a note-off, and forgetting it here is what
    // leaves that pipe sounding until the next panic.
}

void LearnedControls::adopt(const MidiMap& map) noexcept
{
    clear();

    for (std::size_t i = 0; i < map.bindingCount(); ++i)
    {
        const MidiSource& src = map.bindingAt(i).source;

        const int first = src.channel == kAnyChannel ? 0  : static_cast<int>(src.channel);
        const int last  = src.channel == kAnyChannel ? 15 : static_cast<int>(src.channel);

        for (int ch = first; ch <= last; ++ch)
        {
            const auto channel = static_cast<MidiChannel>(ch);
            if (src.kind == MidiSource::Kind::Note)
                set(notes_, channel, src.data1);
            else if (src.kind == MidiSource::Kind::ControlChange)
                set(ccs_, channel, src.data1);
        }
    }
}

LearnedControls::Verdict LearnedControls::inspect(const MidiEvent& ev, bool armed) noexcept
{
    // A key coming up is decided ENTIRELY by whether its key going down was
    // swallowed, and never by whether the note is bound now. See SwallowedNotes
    // for why the two are not the same question.
    if (ev.isNoteOff())
        return swallowed_.take(ev) ? Verdict::Swallow : Verdict::Play;

    // Capturing takes precedence over firing: the control the organist is about to
    // move may well be one that is already bound, and re-binding it is exactly what
    // they asked for.
    if (armed && isActuation(ev))
    {
        swallowed_.remember(ev);
        return Verdict::Capture;
    }

    const bool bound = (ev.type == MidiMessageType::NoteOn && test(notes_, ev.channel, ev.data1))
                    || (ev.type == MidiMessageType::ControlChange
                        && test(ccs_, ev.channel, ev.data1));
    if (! bound)
        return Verdict::Play;

    if (! isActuation(ev))
        return Verdict::Swallow; // the "off" edge of a bound tab: eaten, not reported

    swallowed_.remember(ev);
    return Verdict::Fire;
}

} // namespace caecilia::midi
