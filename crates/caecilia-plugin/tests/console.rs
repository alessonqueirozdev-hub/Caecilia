// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Pistons, MIDI learn and the registration controller, through a host.

use caecilia_model::StopId;
use caecilia_plugin::{Action, Binding, Caecilia, Origin, Source, stop_param_id};
use caecilia_registration::{Command, PlenumSpec, Reason};
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_midi::MidiMessage;
use manguebyte_plugin::{Harness, Plugin, load_state, save_state};

fn harness() -> Harness<Caecilia> {
    Harness::<Caecilia>::new(48000.0, 256)
}

fn midi(h: &mut Harness<Caecilia>, frame: u64, m: MidiMessage) {
    h.schedule(frame, EventKind::Midi(m));
}

fn drawn(h: &Harness<Caecilia>) -> Vec<u16> {
    let mask = h.params().stop_mask();
    (0..64).filter(|n| mask & (1 << n) != 0).collect()
}

#[test]
fn program_change_recalls_the_factory_generals() {
    let mut h = harness();
    midi(
        &mut h,
        0,
        MidiMessage::ProgramChange {
            channel: 0,
            program: 0,
        },
    ); // pp
    h.render(512);
    let pp = drawn(&h);
    // The 8' flutes of the manuals: Grand-Orgue and Récit Bourdons.
    assert_eq!(pp, [9, 19]);
    midi(
        &mut h,
        512,
        MidiMessage::ProgramChange {
            channel: 0,
            program: 6,
        },
    ); // T
    h.render(512);
    assert_eq!(drawn(&h).len(), 32, "the tutti general draws everything");
}

#[test]
fn a_learned_control_draws_a_stop_and_a_learned_key_fires_a_piston() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();

    // Learn: the next control moved draws the Récit Trompette.
    ctl.lock().unwrap().learn(Action::ToggleStop(24));
    midi(
        &mut h,
        0,
        MidiMessage::ControlChange {
            channel: 0,
            controller: 80,
            value: 127,
        },
    );
    midi(
        &mut h,
        100,
        MidiMessage::ControlChange {
            channel: 0,
            controller: 80,
            value: 0,
        },
    );
    h.render(512);
    assert_eq!(
        ctl.lock().unwrap().bindings(),
        [Binding {
            source: Source::Controller {
                channel: 0,
                number: 80
            },
            action: Action::ToggleStop(24)
        }]
    );
    assert!(!drawn(&h).contains(&24), "learning only learns");

    // Pressing it draws, pressing again retires; the release does nothing.
    midi(
        &mut h,
        512,
        MidiMessage::ControlChange {
            channel: 0,
            controller: 80,
            value: 127,
        },
    );
    h.render(512);
    assert!(drawn(&h).contains(&24));
    midi(
        &mut h,
        1024,
        MidiMessage::ControlChange {
            channel: 0,
            controller: 80,
            value: 0,
        },
    );
    midi(
        &mut h,
        1100,
        MidiMessage::ControlChange {
            channel: 0,
            controller: 80,
            value: 127,
        },
    );
    h.render(512);
    assert!(!drawn(&h).contains(&24));

    // A spare pedal key as a toe stud for the tutti general: it fires the
    // piston and sounds no pipe, and neither does its release.
    ctl.lock().unwrap().learn(Action::General(6));
    h.schedule(
        1536,
        EventKind::NoteOn {
            note: NoteRef::new(2, 30),
            velocity: 1.0,
        },
    );
    h.render(512);
    h.schedule(
        2048,
        EventKind::NoteOn {
            note: NoteRef::new(2, 30),
            velocity: 1.0,
        },
    );
    h.schedule(
        2300,
        EventKind::NoteOff {
            note: NoteRef::new(2, 30),
            velocity: 0.0,
        },
    );
    h.render(512);
    assert_eq!(drawn(&h).len(), 32);
    assert_eq!(
        h.plugin_mut().engine().unwrap().active_voices(),
        0,
        "a stud is not a pipe"
    );
}

#[test]
fn undo_reaches_back_through_the_host_and_the_pistons() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    let opening = drawn(&h);
    h.render(256);

    // The host draws a stop; a program change recalls pp; the controller undoes both.
    h.automate(256, &stop_param_id(16), 1.0);
    h.render(256);
    midi(
        &mut h,
        512,
        MidiMessage::ProgramChange {
            channel: 0,
            program: 0,
        },
    );
    h.render(256);
    assert_eq!(drawn(&h), [9, 19]);

    ctl.lock().unwrap().undo();
    h.render(256);
    let mut with_trompette = opening.clone();
    with_trompette.push(16);
    with_trompette.sort();
    assert_eq!(drawn(&h), with_trompette, "undo the piston");
    ctl.lock().unwrap().undo();
    h.render(256);
    assert_eq!(drawn(&h), opening, "undo the host's draw");
    ctl.lock().unwrap().redo();
    h.render(256);
    assert_eq!(drawn(&h), with_trompette);
    let _ = Origin::Host;
}

#[test]
fn the_controller_speaks_the_registration_grammar_and_explains() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    {
        let mut c = ctl.lock().unwrap();
        c.apply(Command::Clear(None));
        c.engage("family:reed & div:récit").unwrap();
        c.apply(Command::BuildPlenum(PlenumSpec {
            division: Some(caecilia_model::DivisionId(1)),
            ..PlenumSpec::default()
        }));
        assert!(c.engage("family:kazoo").is_err());
        assert_eq!(c.explain(StopId(15)).reason, Reason::PlenumMixture);
        assert_eq!(c.explain(StopId(28)).reason, Reason::SelectorMatch);
    }
    h.render(256);
    assert_eq!(drawn(&h), [6, 7, 10, 13, 15, 28, 29, 30, 31]);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 0);
}

#[test]
fn captured_pistons_and_learned_controls_are_saved_with_the_session() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    {
        let mut c = ctl.lock().unwrap();
        c.apply(Command::Clear(None));
        c.engage("name:gambe | name:céleste").unwrap();
        c.capture_general(8);
    }
    let binding = Binding {
        source: Source::Key {
            channel: 2,
            note: 31,
        },
        action: Action::General(8),
    };
    assert!(h.params().console.bind(binding));
    let saved = save_state::<Caecilia>(h.params());

    let fresh = Caecilia::new();
    let params = fresh.params();
    load_state::<Caecilia>(&params, &saved).expect("loads");
    assert_eq!(params.console.general(8).unwrap().0, (1 << 20) | (1 << 21));
    assert_eq!(params.console.bindings(), [binding]);
}

fn key(h: &mut Harness<Caecilia>, frame: u64, note: u8, on: bool) {
    let note = NoteRef::new(0, note);
    h.schedule(
        frame,
        if on {
            EventKind::NoteOn {
                note,
                velocity: 1.0,
            }
        } else {
            EventKind::NoteOff {
                note,
                velocity: 0.0,
            }
        },
    );
}

#[test]
fn manual_switches_send_one_keyboard_to_either_manual_or_both() {
    let mut h = harness();
    h.render(256);
    let ctl = h.plugin_mut().controller();
    ctl.lock().unwrap().apply(Command::Clear(None));
    h.render(256);
    // Only the Récit's Bourdon 8 drawn.
    h.automate(512, &stop_param_id(19), 1.0);
    h.render(256);
    assert_eq!(drawn(&h), [19]);
    // The Pédale is division 0, the Grand-Orgue 1, the Récit 2.
    let console = &h.params().console;
    for (note, division) in [(95, 2), (96, 1)] {
        assert!(console.bind(Binding {
            source: Source::Key { channel: 0, note },
            action: Action::Manual(division),
        }));
    }
    let telemetry = h.plugin_mut().telemetry();
    let voices = |h: &mut Harness<Caecilia>| h.plugin_mut().engine().unwrap().active_voices();

    // The first channel plays the Grand-Orgue, where nothing is drawn.
    key(&mut h, 768, 60, true);
    h.render(256);
    assert_eq!(voices(&mut h), 0);
    key(&mut h, 1024, 60, false);
    h.render(256);

    // The top B switches it to the Récit, and sounds no pipe itself.
    key(&mut h, 1280, 95, true);
    key(&mut h, 1300, 95, false);
    h.render(256);
    assert_eq!(voices(&mut h), 0, "a switch is not a pipe");
    assert_eq!(telemetry.route(), Some((0, 1 << 2)));
    key(&mut h, 1536, 64, true);
    h.render(256);
    assert!(voices(&mut h) > 0, "the Récit speaks");

    // The top C, alone, back to the Grand-Orgue; the key held comes up on
    // the Récit, where it went down.
    key(&mut h, 1792, 96, true);
    key(&mut h, 1800, 96, false);
    h.render(256);
    assert_eq!(telemetry.route(), Some((0, 1 << 1)));
    key(&mut h, 2048, 64, false);
    h.render(256);

    // Both together: both manuals.
    key(&mut h, 2304, 96, true);
    key(&mut h, 2310, 95, true);
    h.render(256);
    assert_eq!(telemetry.route(), Some((0, (1 << 1) | (1 << 2))));
    key(&mut h, 2560, 96, false);
    key(&mut h, 2570, 95, false);
    h.render(256);
}
