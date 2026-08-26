// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The MIDI learn table: binding a physical control to a drawstop or a piston.
//
// None of this module had a test. Its README said so -- "headless-unit-testable,
// though no test suite covers it yet" -- and it stayed true for as long as nothing
// outside the directory instantiated any of it. Now the plugin does, so the
// promises the headers make have to be promises something checks.
//
// What is checked here is the whole chain a learned drawstop travels: an actuation
// edge captures a binding, the binding matches its own controller and no other,
// the edge rules decide when it fires, and the selector it carries resolves to
// exactly one stop on a real organ.
//

#include "caecilia/midi/MidiEvent.h"
#include "caecilia/midi/MidiLearn.h"
#include "caecilia/midi/MidiMap.h"
#include "caecilia/model/Organ.h"
#include "caecilia/registration/FactoryGenerals.h"

#include "support/TestOrgan.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace midi = caecilia::midi;
namespace reg  = caecilia::registration;
namespace tests = caecilia::tests;

namespace
{
midi::RegistrationCommandTemplate toggleStop(int id)
{
    return midi::RegistrationCommandTemplate::toggle("id:" + std::to_string(id));
}
} // namespace

TEST_CASE("A learn captures on an actuation, and only on an actuation",
          "[midi][learn]")
{
    midi::MidiLearn learn;
    REQUIRE_FALSE(learn.isArmed());

    learn.arm(toggleStop(3));
    CHECK(learn.isArmed());

    // The things an organist did NOT mean as "bind this": a key coming up, a
    // continuous controller passing through zero on its way somewhere, a pressure
    // message the pedal board sends on its own.
    CHECK_FALSE(learn.observe(midi::MidiEvent::noteOff(0, 60)));
    CHECK_FALSE(learn.observe(midi::MidiEvent::controlChange(0, 11, 0)));
    CHECK_FALSE(learn.observe(midi::MidiEvent{ midi::MidiMessageType::ChannelAftertouch, 0, 64, 0 }));
    CHECK(learn.isArmed()); // still waiting

    // And the thing they did.
    CHECK(learn.observe(midi::MidiEvent::noteOn(2, 41, 100)));
    REQUIRE(learn.hasCaptured());

    const midi::MidiLearnBinding b = learn.takeCaptured();
    CHECK(b.isValid());
    CHECK(b.source.kind == midi::MidiSource::Kind::Note);
    CHECK(b.source.channel == 2);
    CHECK(b.source.data1 == 41);
    CHECK(b.command.verb == midi::RegistrationVerb::Toggle);
    CHECK(b.command.selector.view() == "id:3");

    // Taking it disarms; a console left armed would swallow the next key played.
    CHECK_FALSE(learn.isArmed());
    CHECK_FALSE(learn.hasCaptured());
}

TEST_CASE("Re-arming replaces the target rather than queueing it", "[midi][learn]")
{
    // An organist who right-clicks the wrong stop right-clicks the right one next.
    // The first arm must not survive to bind the control they then move.
    midi::MidiLearn learn;
    learn.arm(toggleStop(1));
    learn.arm(toggleStop(7));

    REQUIRE(learn.observe(midi::MidiEvent::controlChange(0, 20, 127)));
    CHECK(learn.takeCaptured().command.selector.view() == "id:7");
}

TEST_CASE("A binding answers to its own controller and to no other", "[midi][learn]")
{
    midi::MidiMap map;
    midi::MidiLearn learn;

    learn.arm(toggleStop(5));
    REQUIRE(learn.observe(midi::MidiEvent::controlChange(3, 22, 127)));
    (void) map.installBinding(learn.takeCaptured());
    REQUIRE(map.bindingCount() == 1);

    CHECK(map.findBinding(midi::MidiEvent::controlChange(3, 22, 90)) != nullptr);
    CHECK(map.findBinding(midi::MidiEvent::controlChange(3, 23, 90)) == nullptr); // other CC
    CHECK(map.findBinding(midi::MidiEvent::controlChange(4, 22, 90)) == nullptr); // other channel
    CHECK(map.findBinding(midi::MidiEvent::noteOn(3, 22, 90))        == nullptr); // other kind
}

TEST_CASE("A continuous controller fires above its threshold and not below",
          "[midi][learn]")
{
    // A stop tab wired to a CC sends 127 and 0; a shoe wired to one sweeps. The
    // threshold is what stops a sweep toggling a stop forty times on the way up.
    midi::MidiMap   map;
    midi::MidiLearn learn;
    learn.arm(toggleStop(2));
    REQUIRE(learn.observe(midi::MidiEvent::controlChange(0, 30, 127)));
    (void) map.installBinding(learn.takeCaptured());

    const midi::MidiLearnBinding& b = map.bindingAt(0);
    CHECK(b.triggerThreshold == 64);
    CHECK_FALSE(b.shouldFire(midi::MidiEvent::controlChange(0, 30, 0)));
    CHECK_FALSE(b.shouldFire(midi::MidiEvent::controlChange(0, 30, 63)));
    CHECK(b.shouldFire(midi::MidiEvent::controlChange(0, 30, 64)));
    CHECK(b.shouldFire(midi::MidiEvent::controlChange(0, 30, 127)));
}

TEST_CASE("A note binding fires on the way down and not on the way up",
          "[midi][learn]")
{
    // findBinding matches the note-off too -- deliberately, so the caller can
    // swallow it -- and shouldFire is what keeps the action from happening twice.
    midi::MidiMap   map;
    midi::MidiLearn learn;
    learn.arm(midi::RegistrationCommandTemplate::recallGeneral(4));
    REQUIRE(learn.observe(midi::MidiEvent::noteOn(0, 36, 90)));
    (void) map.installBinding(learn.takeCaptured());

    const midi::MidiLearnBinding& b = map.bindingAt(0);
    CHECK(b.shouldFire(midi::MidiEvent::noteOn(0, 36, 90)));
    CHECK_FALSE(b.shouldFire(midi::MidiEvent::noteOff(0, 36)));
    CHECK_FALSE(b.shouldFire(midi::MidiEvent::noteOn(0, 36, 0))); // velocity 0 is a note-off

    // The note-off still MATCHES, which is what lets the plugin swallow it rather
    // than let a bound stop tab also sound a pipe.
    CHECK(map.findBinding(midi::MidiEvent::noteOff(0, 36)) != nullptr);
}

TEST_CASE("Re-binding a controller replaces what it did", "[midi][learn]")
{
    // One control, one action. Installing over the same source must replace, not
    // append, or a tab would fire two stops and only one of them on purpose.
    midi::MidiMap   map;
    midi::MidiLearn learn;

    learn.arm(toggleStop(1));
    REQUIRE(learn.observe(midi::MidiEvent::noteOn(0, 48, 100)));
    (void) map.installBinding(learn.takeCaptured());

    learn.arm(toggleStop(9));
    REQUIRE(learn.observe(midi::MidiEvent::noteOn(0, 48, 100)));
    (void) map.installBinding(learn.takeCaptured());

    CHECK(map.bindingCount() == 1);
    CHECK(map.bindingAt(0).command.selector.view() == "id:9");
}

TEST_CASE("A learned drawstop resolves to exactly that drawstop", "[midi][learn]")
{
    // The join between this module and the registration one, and the reason the
    // selector grammar grew an `id:` term: a binding has to name ONE stop, and a
    // name substring cannot -- a real organ has the same Trompette 8 on two
    // divisions.
    const caecilia::model::Organ organ = tests::buildTestOrgan();

    midi::MidiMap   map;
    midi::MidiLearn learn;

    for (const caecilia::model::Stop& s : organ.stops())
    {
        learn.arm(toggleStop(static_cast<int>(s.id().value)));
        REQUIRE(learn.observe(midi::MidiEvent::noteOn(
            0, static_cast<caecilia::core::MidiNote>(20 + s.id().value), 100)));
        (void) map.installBinding(learn.takeCaptured());
    }
    REQUIRE(map.bindingCount() == organ.stops().size());

    for (std::size_t i = 0; i < map.bindingCount(); ++i)
    {
        const midi::MidiLearnBinding& b = map.bindingAt(i);
        const std::uint64_t mask =
            reg::resolveSelectorMask(organ, b.command.selector.view());

        INFO("binding " << i << " selector '" << b.command.selector.view() << "'");
        // Exactly one bit, and the right one.
        REQUIRE(mask != 0);
        CHECK((mask & (mask - 1)) == 0);
    }

    // And every stop of the organ is covered exactly once between them.
    std::uint64_t all = 0;
    for (std::size_t i = 0; i < map.bindingCount(); ++i)
        all |= reg::resolveSelectorMask(organ, map.bindingAt(i).command.selector.view());
    for (const caecilia::model::Stop& s : organ.stops())
        CHECK((all & (std::uint64_t{ 1 } << s.id().value)) != 0);
}

TEST_CASE("An event survives being squeezed into one word", "[midi][learn]")
{
    // How a learned control crosses from the audio thread to the message thread:
    // a lock-free queue of plain integers. Everything that decides an action has
    // to make the trip -- get the channel wrong and a binding on manual II fires
    // from manual I.
    const midi::MidiEvent cases[] = {
        midi::MidiEvent::noteOn(0, 0, 1),
        midi::MidiEvent::noteOn(15, 127, 127),
        midi::MidiEvent::noteOff(7, 60),
        midi::MidiEvent::controlChange(3, 64, 127),
        midi::MidiEvent::controlChange(9, 11, 0),
        midi::MidiEvent{ midi::MidiMessageType::ProgramChange, 12, 42, 0 },
    };

    for (const midi::MidiEvent& ev : cases)
    {
        const midi::MidiEvent back = midi::MidiEvent::unpack(ev.pack());
        INFO("kind " << static_cast<int>(ev.type) << " ch " << static_cast<int>(ev.channel)
                     << " d1 " << static_cast<int>(ev.data1)
                     << " d2 " << static_cast<int>(ev.data2));
        CHECK(back.type    == ev.type);
        CHECK(back.channel == ev.channel);
        CHECK(back.data1   == ev.data1);
        CHECK(back.data2   == ev.data2);

        // And it still binds and fires the same way afterwards, which is the only
        // thing the round trip is for.
        CHECK(back.isNoteOn()  == ev.isNoteOn());
        CHECK(back.isNoteOff() == ev.isNoteOff());
    }

    // The sample offset is deliberately not carried; saying so here stops someone
    // relying on it later.
    CHECK(midi::MidiEvent::unpack(midi::MidiEvent::noteOn(0, 60, 100, 321).pack())
              .sampleOffset == 0);
}

TEST_CASE("The table fills up rather than overrunning", "[midi][learn]")
{
    // Fixed capacity is the real-time contract; what it must not do is wrap around
    // and start overwriting bindings the organist made.
    midi::MidiMap map;
    for (std::size_t i = 0; i < midi::MidiMap::kMaxLearnBindings + 8; ++i)
    {
        midi::MidiLearnBinding b;
        b.source  = midi::MidiSource{ midi::MidiSource::Kind::Note,
                                      static_cast<midi::MidiChannel>(i / 128),
                                      static_cast<std::uint8_t>(i % 128) };
        b.command = toggleStop(1);
        (void) map.installBinding(b);
    }
    CHECK(map.bindingCount() == midi::MidiMap::kMaxLearnBindings);

    // And clearing really clears.
    map.clearBindings();
    CHECK(map.bindingCount() == 0);
    CHECK(map.findBinding(midi::MidiEvent::noteOn(0, 0, 100)) == nullptr);
}

TEST_CASE("A slot past the count reads as nothing, not as what used to be there",
          "[midi][learn]")
{
    // The table is what a console draws and what a document saves, and both walk
    // it by index. A vacated slot that still held its old binding would draw a
    // phantom -- a stop showing a brass pip for a tab that no longer exists.
    const midi::MidiMap empty;
    CHECK(empty.bindingCount() == 0);
    CHECK_FALSE(empty.bindingAt(0).isValid());
    CHECK_FALSE(empty.bindingAt(99999).isValid()); // and never past the storage

    midi::MidiMap   map;
    midi::MidiLearn learn;
    for (int n : { 40, 41, 42 })
    {
        learn.arm(toggleStop(n));
        REQUIRE(learn.observe(midi::MidiEvent::noteOn(
            0, static_cast<caecilia::core::MidiNote>(n), 100)));
        (void) map.installBinding(learn.takeCaptured());
    }
    REQUIRE(map.bindingCount() == 3);

    map.removeBindingAt(1);
    REQUIRE(map.bindingCount() == 2);
    CHECK(map.bindingAt(0).command.selector.view() == "id:40");
    CHECK(map.bindingAt(1).command.selector.view() == "id:42"); // compacted down
    CHECK_FALSE(map.bindingAt(2).isValid());                    // and the tail wiped

    map.clearBindings();
    for (std::size_t i = 0; i < 3; ++i)
        CHECK_FALSE(map.bindingAt(i).isValid());
}
