// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// MidiRouter: raw MIDI in, typed intent out. A hundred and sixty lines of the
// shipping library with no test at all -- and with the same defect twice:
//
//   The note-off of a page-turn key, and of a note learned as a registration
//   control, were swallowed on the strength of "is this note bound NOW". That
//   eats the release of a key whose press already sounded, which is what happens
//   whenever the mapping arrives while the key is down. The pipe then speaks
//   until the next panic.
//
// Nothing in the plugin drives the router yet -- the note path is still decoded on
// juce types inside CommandBridge -- so these tests are also what would make that
// switch a small step rather than a leap.
//

#include "caecilia/engine/EngineCommand.h"
#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiMap.h"
#include "caecilia/midi/MidiRouter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
namespace core = caecilia::core;
namespace midi = caecilia::midi;

namespace
{
constexpr core::DivisionId kGreat{ 1 };
constexpr core::DivisionId kSwell{ 2 };

/// A two-manual console: channel 1 is the Great, channel 2 the Swell, and the
/// Swell is transposed and compass-limited so the routing has something to say.
midi::MidiMap console()
{
    midi::MidiMap map;
    map.channels().mapChannel(/*channel*/ 0, kGreat, /*transpose*/ 0);
    map.channels().setKeyRange(0, 36, 96);
    map.channels().mapChannel(/*channel*/ 1, kSwell, /*transpose*/ 12);
    map.channels().setKeyRange(1, 48, 84);
    return map;
}

midi::MidiLearnBinding boundTo(midi::MidiSource src,
                               const midi::RegistrationCommandTemplate& cmd)
{
    midi::MidiLearnBinding b;
    b.source  = src;
    b.command = cmd;
    return b;
}
} // namespace

TEST_CASE("A key routes to its channel's division, transposed and shaped",
          "[midi][router]")
{
    midi::MidiMap  map = console();
    midi::MidiRouter router;
    router.connect(map);

    const midi::MidiRouteResult great = router.route(midi::MidiEvent::noteOn(0, 60, 100));
    REQUIRE(great.kind == midi::MidiRouteKind::Note);
    CHECK(great.note.division.value == kGreat.value);
    CHECK(great.note.note == 60);
    CHECK(great.note.on);

    // The Swell is transposed up an octave: the same key sounds a different pipe.
    const midi::MidiRouteResult swell = router.route(midi::MidiEvent::noteOn(1, 60, 100));
    REQUIRE(swell.kind == midi::MidiRouteKind::Note);
    CHECK(swell.note.division.value == kSwell.value);
    CHECK(swell.note.note == 72);

    // A release carries no velocity and says so.
    const midi::MidiRouteResult off = router.route(midi::MidiEvent::noteOff(0, 60));
    REQUIRE(off.kind == midi::MidiRouteKind::Note);
    CHECK_FALSE(off.note.on);
    CHECK(off.note.velocity == 0);
}

TEST_CASE("A key outside the compass, or on no manual, sounds nothing",
          "[midi][router]")
{
    // A division has a compass -- the demo pedalboard is thirty notes, the manuals
    // sixty-one -- and a key past it is not a pipe. Without this the instrument
    // sounds notes it does not have and spends a voice on each.
    midi::MidiMap  map = console();
    midi::MidiRouter router;
    router.connect(map);

    CHECK(router.route(midi::MidiEvent::noteOn(1, 47, 100)).kind
          == midi::MidiRouteKind::Ignored); // below the Swell's compass
    CHECK(router.route(midi::MidiEvent::noteOn(1, 85, 100)).kind
          == midi::MidiRouteKind::Ignored); // above it
    CHECK(router.route(midi::MidiEvent::noteOn(9, 60, 100)).kind
          == midi::MidiRouteKind::Ignored); // a channel no manual claims
}

TEST_CASE("A router with no map routes nothing", "[midi][router]")
{
    // The state it is in before connect(), and a null map must be a silence rather
    // than a dereference.
    midi::MidiRouter router;
    CHECK_FALSE(router.isConnected());
    CHECK(router.route(midi::MidiEvent::noteOn(0, 60, 100)).kind
          == midi::MidiRouteKind::Ignored);
}

TEST_CASE("The swell shoe is continuous and the damper pedal is not",
          "[midi][router]")
{
    midi::MidiMap  map = console();
    midi::MidiRouter router;
    router.connect(map);

    // CC 11: every value is meaningful, and it arrives as a POSITION, not a gain.
    // How much a shut box attenuates is the instrument's business.
    const midi::MidiRouteResult shut = router.route(midi::MidiEvent::controlChange(1, 11, 0));
    REQUIRE(shut.kind == midi::MidiRouteKind::Expression);
    CHECK(shut.expression.division.value == kSwell.value);
    CHECK(shut.expression.position == Approx(0.0f));

    const midi::MidiRouteResult open = router.route(midi::MidiEvent::controlChange(1, 11, 127));
    REQUIRE(open.kind == midi::MidiRouteKind::Expression);
    CHECK(open.expression.position == Approx(1.0f));

    // CC 64: a pedal is down or up, and the line is at 64.
    const midi::MidiRouteResult down = router.route(midi::MidiEvent::controlChange(0, 64, 127));
    REQUIRE(down.kind == midi::MidiRouteKind::Sustain);
    CHECK(down.sustain.division.value == kGreat.value);
    CHECK(down.sustain.down);

    const midi::MidiRouteResult up = router.route(midi::MidiEvent::controlChange(0, 64, 63));
    REQUIRE(up.kind == midi::MidiRouteKind::Sustain);
    CHECK_FALSE(up.sustain.down);
}

TEST_CASE("All-notes-off outranks anything learned on it", "[midi][router]")
{
    // Binding a stop to CC 123 is a mistake an organist can make with a badly
    // configured controller. Losing the panic to it is not a mistake they can
    // recover from without unplugging something.
    midi::MidiMap map = console();
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::ControlChange, 0, 123 },
        midi::RegistrationCommandTemplate::toggle("id:1")));

    midi::MidiRouter router;
    router.connect(map);

    CHECK(router.route(midi::MidiEvent::controlChange(0, 123, 127)).kind
          == midi::MidiRouteKind::Panic);
    CHECK(router.route(midi::MidiEvent::controlChange(0, 120, 127)).kind
          == midi::MidiRouteKind::Panic);
}

TEST_CASE("A learned controller fires above its threshold and is eaten below",
          "[midi][router]")
{
    midi::MidiMap map = console();
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::ControlChange, 0, 20 },
        midi::RegistrationCommandTemplate::toggle("id:7")));

    midi::MidiRouter router;
    router.connect(map);

    const midi::MidiRouteResult on = router.route(midi::MidiEvent::controlChange(0, 20, 127));
    REQUIRE(on.kind == midi::MidiRouteKind::Registration);
    CHECK(on.registration.selector.view() == "id:7");

    // Below the threshold it is swallowed, not passed on.
    CHECK(router.route(midi::MidiEvent::controlChange(0, 20, 10)).kind
          == midi::MidiRouteKind::Ignored);
}

TEST_CASE("A control learned over one that already meant something keeps it",
          "[midi][router]")
{
    // Binding a stop to CC 64 takes the damper pedal away from that channel, which
    // is the organist's business. What must NOT happen is the control meaning both
    // things: a sub-threshold value falling through to the sustain path would draw
    // the stop on the way down and lift the pedal on the way up.
    //
    // The same holds for CC 11 and the swell shoe, and CC 11 is the likelier
    // accident: a controller with a spare slider often sends it.
    midi::MidiMap map = console();
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::ControlChange, 0, 64 },
        midi::RegistrationCommandTemplate::toggle("id:3")));
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::ControlChange, 1, 11 },
        midi::RegistrationCommandTemplate::recallGeneral(2)));

    midi::MidiRouter router;
    router.connect(map);

    CHECK(router.route(midi::MidiEvent::controlChange(0, 64, 127)).kind
          == midi::MidiRouteKind::Registration);
    CHECK(router.route(midi::MidiEvent::controlChange(0, 64, 0)).kind
          == midi::MidiRouteKind::Ignored);   // NOT Sustain

    CHECK(router.route(midi::MidiEvent::controlChange(1, 11, 127)).kind
          == midi::MidiRouteKind::Registration);
    CHECK(router.route(midi::MidiEvent::controlChange(1, 11, 20)).kind
          == midi::MidiRouteKind::Ignored);   // NOT Expression

    // And the same controllers on a channel with no binding go on working.
    CHECK(router.route(midi::MidiEvent::controlChange(1, 64, 127)).kind
          == midi::MidiRouteKind::Sustain);
    CHECK(router.route(midi::MidiEvent::controlChange(0, 11, 64)).kind
          == midi::MidiRouteKind::Expression);
}

TEST_CASE("A learned key fires once and never sounds a pipe", "[midi][router]")
{
    midi::MidiMap map = console();
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::Note, 0, 40 },
        midi::RegistrationCommandTemplate::recallGeneral(3)));

    midi::MidiRouter router;
    router.connect(map);

    const midi::MidiRouteResult press = router.route(midi::MidiEvent::noteOn(0, 40, 100));
    REQUIRE(press.kind == midi::MidiRouteKind::Registration);
    CHECK(press.registration.verb == midi::RegistrationVerb::RecallGeneral);
    CHECK(press.registration.index == 3);

    // The release goes with it, and only once.
    CHECK(router.route(midi::MidiEvent::noteOff(0, 40)).kind == midi::MidiRouteKind::Ignored);
    CHECK(router.pendingReleases() == 0);
}

TEST_CASE("A key already down keeps its release when its note becomes a control",
          "[midi][router][regression]")
{
    // THE defect, and it was here twice. The router decided on the note-OFF
    // whether to swallow it, by asking whether the note was bound -- so a key held
    // when the binding arrived lost its release and the pipe spoke until the next
    // panic.
    midi::MidiMap  map = console();
    midi::MidiRouter router;
    router.connect(map);

    // The key goes down with nothing bound: it sounds.
    REQUIRE(router.route(midi::MidiEvent::noteOn(0, 40, 100)).kind
            == midi::MidiRouteKind::Note);

    // Now it becomes a piston, while the key is still down.
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::Note, 0, 40 },
        midi::RegistrationCommandTemplate::recallGeneral(3)));

    // The release must still reach the engine, or that pipe never stops.
    const midi::MidiRouteResult release = router.route(midi::MidiEvent::noteOff(0, 40));
    REQUIRE(release.kind == midi::MidiRouteKind::Note);
    CHECK_FALSE(release.note.on);
}

TEST_CASE("A page-turn key is the same story", "[midi][router][regression]")
{
    // The sequencer navigation path had the identical bug, twenty lines above the
    // learned one.
    midi::MidiMap  map = console();
    // A fresh MidiMap already carries the user's si5/do6 page-turn keys, which is
    // the right default and the wrong starting point for this test: it needs the
    // key to be an ordinary one first.
    map.sequencerNav().clear();

    midi::MidiRouter router;
    router.connect(map);

    // si5 with nothing mapped to it: it plays.
    REQUIRE(router.route(midi::MidiEvent::noteOn(0, 83, 100)).kind
            == midi::MidiRouteKind::Note);

    map.sequencerNav().bind(83, midi::SequencerDirection::Previous, /*channel*/ 0);

    CHECK(router.route(midi::MidiEvent::noteOff(0, 83)).kind == midi::MidiRouteKind::Note);

    // And from the next press onward it is a page turn, release and all.
    const midi::MidiRouteResult turn = router.route(midi::MidiEvent::noteOn(0, 83, 100));
    REQUIRE(turn.kind == midi::MidiRouteKind::Registration);
    CHECK(turn.registration.verb == midi::RegistrationVerb::SequencerPrevious);
    CHECK(router.route(midi::MidiEvent::noteOff(0, 83)).kind == midi::MidiRouteKind::Ignored);
}

TEST_CASE("A reset drops the releases that are never coming", "[midi][router]")
{
    midi::MidiMap map = console();
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::Note, 0, 40 },
        midi::RegistrationCommandTemplate::recallGeneral(1)));

    midi::MidiRouter router;
    router.connect(map);
    REQUIRE(router.route(midi::MidiEvent::noteOn(0, 40, 100)).kind
            == midi::MidiRouteKind::Registration);
    REQUIRE(router.pendingReleases() == 1);

    router.reset();
    CHECK(router.pendingReleases() == 0);
}

TEST_CASE("A program change recalls a general, and a learned one wins",
          "[midi][router]")
{
    midi::MidiMap map = console();
    // The default mode is program -> general, which is the workflow every organ
    // console with a program-change stud expects.
    map.programChange().setDefaultMode(midi::ProgramChangeMap::Mode::RecallGeneral);

    midi::MidiRouter router;
    router.connect(map);

    const midi::MidiEvent pc{ midi::MidiMessageType::ProgramChange, 0, 5, 0 };
    const midi::MidiRouteResult generic = router.route(pc);
    REQUIRE(generic.kind == midi::MidiRouteKind::Registration);
    CHECK(generic.registration.index == 5);

    // A learned binding on the same program change is the organist being explicit.
    (void) map.installBinding(boundTo(
        midi::MidiSource{ midi::MidiSource::Kind::ProgramChange, 0, 5 },
        midi::RegistrationCommandTemplate::toggle("id:2")));

    const midi::MidiRouteResult learned = router.route(pc);
    REQUIRE(learned.kind == midi::MidiRouteKind::Registration);
    CHECK(learned.registration.verb == midi::RegistrationVerb::Toggle);
    CHECK(learned.registration.selector.view() == "id:2");

    // Every OTHER program still recalls its own general -- the default mode is not
    // a table of exceptions, it is "program n is general n", which is what a
    // console with a rank of program-change studs expects.
    const midi::MidiRouteResult other =
        router.route(midi::MidiEvent{ midi::MidiMessageType::ProgramChange, 0, 6, 0 });
    REQUIRE(other.kind == midi::MidiRouteKind::Registration);
    CHECK(other.registration.index == 6);

    // And a channel told to ignore program changes ignores them.
    map.programChange().setDefaultMode(midi::ProgramChangeMap::Mode::Ignore);
    CHECK(router.route(midi::MidiEvent{ midi::MidiMessageType::ProgramChange, 0, 6, 0 }).kind
          == midi::MidiRouteKind::Ignored);
}

TEST_CASE("What the engine can be told directly, and what it cannot",
          "[midi][router]")
{
    // Sustain, expression and panic map one to one onto engine commands. Notes
    // need registration-aware pipe expansion and registration intents have to be
    // resolved off the audio thread, so neither is a direct encoding -- and
    // toEngineCommand saying so is what stops a caller shipping a half-formed
    // command.
    core::engine::EngineCommand cmd;

    midi::SustainRoute s;
    s.division = kGreat;
    s.down     = true;
    CHECK(midi::MidiRouter::toEngineCommand(midi::MidiRouteResult::makeSustain(s, 17), cmd));
    CHECK(cmd.type == core::engine::EngineCommandType::SetSustain);
    CHECK(cmd.sampleOffset == 17);

    midi::ExpressionRoute e;
    e.division = kSwell;
    e.position = 0.25f;
    CHECK(midi::MidiRouter::toEngineCommand(midi::MidiRouteResult::makeExpression(e, 5), cmd));
    CHECK(cmd.type == core::engine::EngineCommandType::SetExpression);

    // The offset travels. Dropping it put a panic at the top of the block whatever
    // the host asked for, cutting off up to a block of audio still sounding.
    CHECK(midi::MidiRouter::toEngineCommand(midi::MidiRouteResult::makePanic(123), cmd));
    CHECK(cmd.type == core::engine::EngineCommandType::Panic);
    CHECK(cmd.sampleOffset == 123);

    midi::NoteRoute n;
    n.division = kGreat;
    n.note     = 60;
    n.on       = true;
    CHECK_FALSE(midi::MidiRouter::toEngineCommand(midi::MidiRouteResult::makeNote(n, 0), cmd));
    CHECK_FALSE(midi::MidiRouter::toEngineCommand(
        midi::MidiRouteResult::makeRegistration(
            midi::RegistrationCommandTemplate::recallGeneral(1), 0), cmd));
    CHECK_FALSE(midi::MidiRouter::toEngineCommand(midi::MidiRouteResult::ignored(0), cmd));
}
