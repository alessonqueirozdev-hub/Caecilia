// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The audio thread's half of MIDI learn: what happens to an event before it ever
// reaches the organ.
//
// This logic used to live in the plugin, where nothing could test it -- the
// headless suite links caecilia::core and there is no plugin harness -- and it was
// the only part of a learned drawstop with no cover at all. Moving it here found
// the bug it had:
//
//   Binding a note WHILE HOLDING IT ate the note-off, because the "is this note
//   bound" test came before the "did I swallow its note-on" test. The pipe then
//   spoke until the next panic. The comment above the code explained why the two
//   had to be in the other order.
//

#include "caecilia/midi/LearnedControls.h"
#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiLearn.h"
#include "caecilia/midi/MidiMap.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace midi = caecilia::midi;

namespace
{
using Verdict = midi::LearnedControls::Verdict;

/// A map with one binding on the given source.
midi::MidiMap mapWith(midi::MidiSource::Kind kind, midi::MidiChannel channel,
                      std::uint8_t number)
{
    midi::MidiMap map;
    midi::MidiLearnBinding b;
    b.source  = midi::MidiSource{ kind, channel, number };
    b.command = midi::RegistrationCommandTemplate::toggle("id:1");
    (void) map.installBinding(b);
    return map;
}
} // namespace

TEST_CASE("An unbound control plays, and a bound one does not", "[midi][gate]")
{
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));

    CHECK(gate.isNoteBound(0, 36));
    CHECK_FALSE(gate.isNoteBound(0, 37));
    CHECK_FALSE(gate.isNoteBound(1, 36));
    CHECK_FALSE(gate.isControlBound(0, 36)); // a CC 36 is not a note 36

    CHECK(gate.inspect(midi::MidiEvent::noteOn(0, 60, 100), false) == Verdict::Play);
    CHECK(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Fire);
}

TEST_CASE("A bound key's release is swallowed with it", "[midi][gate]")
{
    // Otherwise binding a stop tab to a note sounds the pipe as well as drawing
    // the stop, and then leaves it sounding.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));

    REQUIRE(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Fire);
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), false) == Verdict::Swallow);

    // And only once: a second note-off with no note-on before it is somebody
    // else's, and must not be eaten.
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), false) == Verdict::Play);
}

TEST_CASE("A key already down keeps its release when its note is bound",
          "[midi][gate][regression]")
{
    // THE defect this move found. An organist holding a key and then binding that
    // note -- or binding it a moment before playing it, while the table is still
    // crossing to the audio thread -- has a note-off whose note-on already sounded.
    // Eat that and the pipe speaks until the next panic.
    midi::LearnedControls gate;

    // Nothing bound yet: the key sounds.
    REQUIRE(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Play);

    // Now the binding arrives, while the key is still down.
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));

    // The release must reach the organ, or that pipe never stops.
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), false) == Verdict::Play);
}

TEST_CASE("An armed learn captures before it fires", "[midi][gate]")
{
    // Re-binding a control that already has a binding is exactly what an organist
    // means when they right-click a second stop and move the same tab.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));

    CHECK(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), /*armed*/ true)
          == Verdict::Capture);
    // Its release still goes with it.
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), true) == Verdict::Swallow);
}

TEST_CASE("An armed learn swallows the key it binds", "[midi][gate]")
{
    // Binding a stop tab must not also sound the pipe it is being bound to.
    midi::LearnedControls gate;

    CHECK(gate.inspect(midi::MidiEvent::noteOn(0, 48, 100), true) == Verdict::Capture);
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 48), true) == Verdict::Swallow);

    // A key coming up is not an actuation, so an armed learn lets it pass rather
    // than capturing on it -- a released key is not what the organist pointed at.
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 49), true) == Verdict::Play);
}

TEST_CASE("The off edge of a bound tab is eaten and not reported", "[midi][gate]")
{
    // A stop tab wired to a CC sends 127 and then 0. The 0 must not reach the
    // organ -- it is the same control -- and must not cost a ring slot and an
    // async wake for a message the other thread would discard anyway.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::ControlChange, 2, 22));

    CHECK(gate.inspect(midi::MidiEvent::controlChange(2, 22, 127), false) == Verdict::Fire);
    CHECK(gate.inspect(midi::MidiEvent::controlChange(2, 22, 0),   false) == Verdict::Swallow);

    // An unbound CC on the same channel is untouched -- expression, sustain and
    // the rest go on working.
    CHECK(gate.inspect(midi::MidiEvent::controlChange(2, 11, 64), false) == Verdict::Play);
}

TEST_CASE("A wildcard channel binds every channel", "[midi][gate]")
{
    // MidiLearn records the channel it saw, so a wildcard only arrives from a
    // hand-authored or restored binding. Lighting one channel's bit for it would
    // pass the control through on the other fifteen.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::ControlChange, midi::kAnyChannel, 30));

    for (int ch = 0; ch < 16; ++ch)
    {
        INFO("channel " << ch);
        CHECK(gate.isControlBound(static_cast<midi::MidiChannel>(ch), 30));
    }
}

TEST_CASE("Program change is not the gate's business", "[midi][gate]")
{
    // It already has its own path to the general pistons. Swallowing it here would
    // take that away from every organ console that sends one.
    midi::LearnedControls gate;
    midi::MidiMap map;
    midi::MidiLearnBinding b;
    b.source  = midi::MidiSource{ midi::MidiSource::Kind::ProgramChange, 0, 4 };
    b.command = midi::RegistrationCommandTemplate::recallGeneral(4);
    (void) map.installBinding(b);
    gate.adopt(map);

    CHECK(gate.inspect(midi::MidiEvent{ midi::MidiMessageType::ProgramChange, 0, 4, 0 },
                       false) == Verdict::Play);
}

TEST_CASE("Re-adopting forgets old bindings but not keys still down",
          "[midi][gate][regression]")
{
    // Two things at once, and they pull in opposite directions: a control that is
    // no longer bound must play again, and a key that was swallowed on the way
    // down must still be swallowed on the way up -- even though the binding that
    // swallowed it has just been removed.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));
    REQUIRE(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Fire);

    gate.adopt(midi::MidiMap{}); // the organist cleared the bindings, key still down

    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), false) == Verdict::Swallow);
    CHECK(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Play);
}

TEST_CASE("A reset drops the note-offs that are never coming", "[midi][gate]")
{
    // A host reset or a panic ends every sounding note without the keys sending
    // anything. What the gate is still waiting for has to go with them, or the
    // next press of that key is swallowed for a release that already happened.
    midi::LearnedControls gate;
    gate.adopt(mapWith(midi::MidiSource::Kind::Note, 0, 36));
    REQUIRE(gate.inspect(midi::MidiEvent::noteOn(0, 36, 100), false) == Verdict::Fire);

    gate.reset();
    CHECK(gate.inspect(midi::MidiEvent::noteOff(0, 36), false) == Verdict::Play);
}

TEST_CASE("The gate and the capture machine agree on what an actuation is",
          "[midi][gate]")
{
    // They have to: the gate decides a block earlier whether the event is worth
    // waking the message thread for, and the capture machine decides there whether
    // to bind it. An event one accepts and the other rejects is a learn that
    // silently did not happen. They were two copies of this rule until
    // LearnedControls::isActuation was the one.
    const midi::MidiEvent cases[] = {
        midi::MidiEvent::noteOn(0, 60, 100),
        midi::MidiEvent::noteOn(0, 60, 0),      // velocity 0 is a release
        midi::MidiEvent::noteOff(0, 60),
        midi::MidiEvent::controlChange(0, 11, 127),
        midi::MidiEvent::controlChange(0, 11, 0),
        midi::MidiEvent{ midi::MidiMessageType::ProgramChange, 0, 3, 0 },
        midi::MidiEvent{ midi::MidiMessageType::ChannelAftertouch, 0, 90, 0 },
        midi::MidiEvent{ midi::MidiMessageType::PitchBend, 0, 0, 64 },
    };

    for (const midi::MidiEvent& ev : cases)
    {
        midi::MidiLearn learn;
        learn.arm(midi::RegistrationCommandTemplate::toggle("id:1"));
        const bool captured = learn.observe(ev);

        INFO("kind " << static_cast<int>(ev.type)
                     << " d1 " << static_cast<int>(ev.data1)
                     << " d2 " << static_cast<int>(ev.data2));
        CHECK(midi::LearnedControls::isActuation(ev) == captured);
    }
}

TEST_CASE("A velocity of zero is a key coming up, not a control being actuated",
          "[midi][gate][regression]")
{
    // Half the MIDI hardware in the world sends a note-on of velocity 0 instead of
    // a note-off, to stay in running status. Read it as an actuation and an
    // organist arming a learn binds the RELEASE of the key they pressed -- and the
    // gate hands the message thread an event it will bind and then never see again.
    const midi::MidiEvent release = midi::MidiEvent::noteOn(0, 60, 0);
    REQUIRE(release.isNoteOff());
    CHECK_FALSE(midi::LearnedControls::isActuation(release));

    midi::MidiLearn learn;
    learn.arm(midi::RegistrationCommandTemplate::toggle("id:1"));
    CHECK_FALSE(learn.observe(release));
    CHECK(learn.isArmed()); // still waiting for something the organist meant

    // And the gate treats it as the release it is: swallowed only if the key going
    // down was swallowed, never captured.
    midi::LearnedControls gate;
    CHECK(gate.inspect(release, /*armed*/ true) == Verdict::Play);

    REQUIRE(gate.inspect(midi::MidiEvent::noteOn(0, 60, 100), true) == Verdict::Capture);
    CHECK(gate.inspect(release, false) == Verdict::Swallow);
}

TEST_CASE("A gate with nothing bound never touches anything", "[midi][gate]")
{
    // The state every session starts in and most stay in. Nothing about MIDI learn
    // may cost an organist who has bound nothing.
    midi::LearnedControls gate;

    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; n += 7)
        {
            const auto c = static_cast<midi::MidiChannel>(ch);
            const auto d = static_cast<std::uint8_t>(n);
            CHECK(gate.inspect(midi::MidiEvent::noteOn(c, d, 100), false)  == Verdict::Play);
            CHECK(gate.inspect(midi::MidiEvent::noteOff(c, d), false)      == Verdict::Play);
            CHECK(gate.inspect(midi::MidiEvent::controlChange(c, d, 90), false) == Verdict::Play);
        }
}
