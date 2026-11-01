// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The plugin, played the way a host plays it.

use caecilia_plugin::{Caecilia, stop_param_id};
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_midi::MidiMessage;
use manguebyte_plugin::{Harness, Plugin, load_state, save_state};

const SR: f64 = 48000.0;

fn harness() -> Harness<Caecilia> {
    Harness::<Caecilia>::new(SR, 256)
}

fn rms(b: &AudioBuffer<f32>, from: usize) -> f64 {
    let n = b.num_frames() - from;
    let s: f64 = (0..2)
        .map(|c| {
            b.channel(c)[from..]
                .iter()
                .map(|x| f64::from(*x).powi(2))
                .sum::<f64>()
        })
        .sum();
    (s / (2.0 * n as f64)).sqrt()
}

fn db(x: f64) -> f64 {
    20.0 * x.max(1e-12).log10()
}

fn note_on(h: &mut Harness<Caecilia>, frame: u64, channel: u8, key: u8) {
    h.schedule(
        frame,
        EventKind::NoteOn {
            note: NoteRef::new(channel, key),
            velocity: 0.8,
        },
    );
}

fn note_off(h: &mut Harness<Caecilia>, frame: u64, channel: u8, key: u8) {
    h.schedule(
        frame,
        EventKind::NoteOff {
            note: NoteRef::new(channel, key),
            velocity: 0.0,
        },
    );
}

#[test]
fn it_opens_on_a_plenum_and_plays_a_chord() {
    let mut h = harness();
    for k in [60, 64, 67] {
        note_on(&mut h, 0, 0, k);
    }
    let out = h.render(48000);
    let level = db(rms(&out, 24000));
    assert!(
        (-40.0..-3.0).contains(&level),
        "a plenum chord at {level:.1} dBFS"
    );
    assert!(
        out.peak() <= 10f32.powf(-1.0 / 20.0) + 1e-4,
        "under the ceiling"
    );
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 3 * 6);
}

#[test]
fn drawing_a_stop_through_the_host_changes_what_sounds() {
    let mut h = harness();
    // Retire the whole opening plenum, draw only the Bourdon 8 of the Grand-Orgue.
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 0.0);
    }
    h.automate(0, &stop_param_id(9), 1.0);
    note_on(&mut h, 0, 0, 60);
    h.render(4800);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 1);
    // Draw the Montre 8 under the held key: it joins it.
    h.automate(4800, &stop_param_id(7), 1.0);
    h.render(4800);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 2);
}

#[test]
fn midi_channels_play_divisions() {
    let mut h = harness();
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 0.0);
    }
    // Only the Récit Gambe is drawn.
    h.automate(0, &stop_param_id(20), 1.0);
    note_on(&mut h, 0, 0, 60); // channel 1: the Grand-Orgue, which has nothing drawn
    h.render(2400);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 0);
    note_on(&mut h, 2400, 1, 60); // channel 2: the Récit
    h.render(2400);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 1);
}

#[test]
fn the_sustain_pedal_holds_and_cc11_closes_the_box() {
    let cc = |channel, controller, value| {
        EventKind::Midi(MidiMessage::ControlChange {
            channel,
            controller,
            value,
        })
    };
    let mut h = harness();
    h.schedule(0, cc(0, 64, 127));
    note_on(&mut h, 0, 0, 60);
    note_off(&mut h, 2400, 0, 60);
    h.render(48000);
    assert_eq!(
        h.plugin_mut().engine().unwrap().active_voices(),
        6,
        "held by the pedal"
    );
    h.schedule(48000, cc(0, 64, 0));
    h.render(48000);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 0);

    // The Récit's box, through its channel's CC 11.
    let recit_level = |shut: bool| {
        let mut h = harness();
        for n in 0..64 {
            h.automate(0, &stop_param_id(n), 0.0);
        }
        h.automate(0, &stop_param_id(18), 1.0);
        if shut {
            h.schedule(0, cc(1, 11, 0));
        }
        note_on(&mut h, 0, 1, 60);
        db(rms(&h.render(48000), 24000))
    };
    let drop = recit_level(true) - recit_level(false);
    assert!(drop < -10.0, "CC 11 shut the box by {drop:.1} dB");
}

#[test]
fn a_saved_session_reopens_on_its_registration() {
    let mut h = harness();
    h.automate(0, &stop_param_id(16), 1.0); // Grand-Orgue Trompette
    h.automate(0, &stop_param_id(7), 0.0); // retire the Montre 8
    h.render(256);
    let saved = save_state::<Caecilia>(h.params());

    let fresh = Caecilia::new();
    let params = fresh.params();
    load_state::<Caecilia>(&params, &saved).expect("state loads");
    assert_eq!(params.stop_mask(), h.params().stop_mask());
    assert!(params.stops[16].value() && !params.stops[7].value());
}
