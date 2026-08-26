// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/midi/MidiRouter.h"

namespace caecilia::midi
{

MidiRouteResult MidiRouter::route(const MidiEvent& ev) const noexcept
{
    if (map_ == nullptr)
        return MidiRouteResult::ignored(ev.sampleOffset);

    switch (ev.type)
    {
        case MidiMessageType::NoteOn:
        case MidiMessageType::NoteOff:
            return routeNote(ev);

        case MidiMessageType::ControlChange:
            return routeControlChange(ev);

        case MidiMessageType::ProgramChange:
            return routeProgramChange(ev);

        case MidiMessageType::PolyAftertouch:
        case MidiMessageType::ChannelAftertouch:
        case MidiMessageType::PitchBend:
        case MidiMessageType::Other:
        default:
            // TODO(phase8): route pitch bend / aftertouch to expression + swell.
            return MidiRouteResult::ignored(ev.sampleOffset);
    }
}

MidiRouteResult MidiRouter::routeNote(const MidiEvent& ev) const noexcept
{
    // 1) Sequencer navigation keys (e.g. si5/do6) fire on the on-edge and their
    //    note-off is swallowed so the page-turn key never sounds a pipe.
    const SequencerDirection dir = map_->sequencerNav().lookup(ev.channel, ev.data1);
    if (dir != SequencerDirection::None)
    {
        if (ev.isNoteOn())
            return MidiRouteResult::makeRegistration(
                RegistrationCommandTemplate::sequencer(dir), ev.sampleOffset);
        return MidiRouteResult::ignored(ev.sampleOffset);
    }

    // 2) A note learned as a registration control (piston-on-a-key). Fire on the
    //    note-on edge; swallow the paired note-off so it never sounds a pipe.
    if (const MidiLearnBinding* b = map_->findBinding(ev))
    {
        if (ev.isNoteOn())
            return MidiRouteResult::makeRegistration(b->command, ev.sampleOffset);
        return MidiRouteResult::ignored(ev.sampleOffset);
    }

    // 3) Ordinary keyboard note -> the channel's division.
    const auto& channels = map_->channels();
    if (!channels.isMapped(ev.channel) || !channels.inRange(ev.channel, ev.data1))
        return MidiRouteResult::ignored(ev.sampleOffset);

    NoteRoute n;
    n.division = channels.division(ev.channel);
    n.note     = channels.transposed(ev.channel, ev.data1);
    n.on       = ev.isNoteOn();
    n.velocity = n.on ? map_->velocity().map(ev.data2) : core::Velocity{0};
    return MidiRouteResult::makeNote(n, ev.sampleOffset);
}

MidiRouteResult MidiRouter::routeControlChange(const MidiEvent& ev) const noexcept
{
    // All-sound-off / all-notes-off take precedence over any learned mapping.
    if (ev.data1 == cc::kAllSoundOff || ev.data1 == cc::kAllNotesOff)
        return MidiRouteResult::makePanic(ev.sampleOffset);

    // A learned continuous controller (drawstop toggle, plenum, undo, ...). It
    // fires when the value crosses its threshold; a sub-threshold value is
    // swallowed rather than falling through to the sustain / ignore paths.
    if (const MidiLearnBinding* b = map_->findBinding(ev))
    {
        if (b->shouldFire(ev))
            return MidiRouteResult::makeRegistration(b->command, ev.sampleOffset);
        return MidiRouteResult::ignored(ev.sampleOffset);
    }

    // Swell shoe, scoped to the channel's division. Unlike the sustain pedal this
    // is CONTINUOUS: every value is meaningful, so there is no threshold.
    if (ev.data1 == cc::kExpression && map_->channels().isMapped(ev.channel))
    {
        ExpressionRoute e;
        e.division = map_->channels().division(ev.channel);
        e.position = static_cast<float>(ev.data2) * (1.0f / 127.0f);
        return MidiRouteResult::makeExpression(e, ev.sampleOffset);
    }

    // Sustain / damper pedal, scoped to the channel's division.
    if (ev.data1 == cc::kSustainPedal && map_->channels().isMapped(ev.channel))
    {
        SustainRoute s;
        s.division = map_->channels().division(ev.channel);
        s.down     = ev.data2 >= 64;
        return MidiRouteResult::makeSustain(s, ev.sampleOffset);
    }

    return MidiRouteResult::ignored(ev.sampleOffset);
}

MidiRouteResult MidiRouter::routeProgramChange(const MidiEvent& ev) const noexcept
{
    // A learned program-change binding wins over the generic PC->generals map.
    if (const MidiLearnBinding* b = map_->findBinding(ev))
        return MidiRouteResult::makeRegistration(b->command, ev.sampleOffset);

    const RegistrationCommandTemplate cmd = map_->programChange().resolve(ev.channel, ev.data1);
    if (cmd.isNoOp())
        return MidiRouteResult::ignored(ev.sampleOffset);
    return MidiRouteResult::makeRegistration(cmd, ev.sampleOffset);
}

bool MidiRouter::toEngineCommand(const MidiRouteResult& result,
                                 core::engine::EngineCommand& out) noexcept
{
    switch (result.kind)
    {
        case MidiRouteKind::Panic:
            // Carry the offset through. Dropping it put a panic at the top of the
            // block regardless of where in it the host asked for one, which cuts
            // off up to a block of audio that should still have been sounding.
            out = core::engine::EngineCommand::makePanic(result.sampleOffset);
            return true;

        // Both of these ARE direct encodings and always have been. Sustain
        // returning false was a plain bug: makeSustain has existed since the pedal
        // was implemented and maps one to one, so anything driving the engine
        // through the router rather than through CommandBridge silently lost the
        // damper pedal.
        case MidiRouteKind::Sustain:
            out = core::engine::EngineCommand::makeSustain(result.sustain.division,
                                                           result.sustain.down,
                                                           result.sampleOffset);
            return true;

        case MidiRouteKind::Expression:
            out = core::engine::EngineCommand::makeExpression(result.expression.division,
                                                              result.expression.position,
                                                              result.sampleOffset);
            return true;

        case MidiRouteKind::Note:
        case MidiRouteKind::Registration:
        case MidiRouteKind::Ignored:
        default:
            // Note events require registration-aware pipe expansion (engine side);
            // registration intents are resolved off-thread into a StateDelta that
            // becomes an ApplyStateDelta command. Neither is a direct encoding.
            return false;
    }
}

} // namespace caecilia::midi
