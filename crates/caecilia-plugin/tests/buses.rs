// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Each division of the organ on an output bus of its own, as a host
//! routes them: the buses by name, a bus switched off not rendered, the
//! buses' sum the main output, and organs of different sizes.
//!
//! Not under Miri, which is too slow for the wrappers.
#![cfg(not(miri))]
#![allow(unsafe_code)]

use std::sync::Mutex;

use caecilia_model::{built_in_organ, caecilia_organ};
use caecilia_plugin::{Caecilia, DIVISION_BUSES, set_organ_for_new_instances, stop_param_id};
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{Event, EventBuffer, EventKind, NoteRef};
use manguebyte_dsp::{ProcessContext, Transport};
use manguebyte_plugin::{Harness, Plugin};

/// The organ a new instance plays is a process-wide slot
/// ([`set_organ_for_new_instances`]): the tests take turns on it, each
/// choosing its organ before it builds.
static ORGAN_LOCK: Mutex<()> = Mutex::new(());

const SR: f64 = 48000.0;

/// "Division 01" for the organ's first division, and so on to 16.
const NAMES: [&str; 16] = [
    "Division 01",
    "Division 02",
    "Division 03",
    "Division 04",
    "Division 05",
    "Division 06",
    "Division 07",
    "Division 08",
    "Division 09",
    "Division 10",
    "Division 11",
    "Division 12",
    "Division 13",
    "Division 14",
    "Division 15",
    "Division 16",
];

fn harness() -> Harness<Caecilia> {
    Harness::<Caecilia>::new(SR, 256)
}

fn retire_all(h: &mut Harness<Caecilia>) {
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 0.0);
    }
}

fn note_on(h: &mut Harness<Caecilia>, channel: u8, key: u8) {
    h.schedule(
        h.position(),
        EventKind::NoteOn {
            note: NoteRef::new(channel, key),
            velocity: 0.8,
        },
    );
}

fn peak(b: &AudioBuffer<f32>) -> f32 {
    (0..b.num_channels())
        .flat_map(|c| b.channel(c).iter())
        .fold(0.0f32, |p, s| p.max(s.abs()))
}

/// The demonstration organ: Pédale on bus 0 (channel 2), Grand-Orgue on
/// bus 1 (channel 0), Récit on bus 2 (channel 1).
#[test]
fn each_division_is_heard_on_a_bus_of_its_own() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    for (channel, stop, bus, name) in [
        (2u8, 0usize, 0usize, "Pédale"),
        (0, 9, 1, "Grand-Orgue"),
        (1, 20, 2, "Récit"),
    ] {
        let mut h = harness();
        retire_all(&mut h);
        h.automate(0, &stop_param_id(stop), 1.0);
        note_on(&mut h, channel, 48);
        h.render(2400);
        for d in 0..16 {
            let p = peak(h.aux_output(d).expect("every bus is on"));
            if d == bus {
                assert!(p > 0.0, "the {name} is heard on bus {d}");
            } else {
                assert_eq!(p, 0.0, "the {name} is alone: bus {d} silent");
            }
        }
    }
}

#[test]
fn a_bus_switched_off_is_not_given_and_the_division_sounds_on_main() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut h = harness();
    retire_all(&mut h);
    h.automate(0, &stop_param_id(9), 1.0); // Grand-Orgue Bourdon 8
    h.set_aux_active(1, false);
    note_on(&mut h, 0, 60);
    let main = h.render(2400);
    assert!(h.aux_output(1).is_none(), "switched off, not given");
    assert!(h.aux_output(0).is_some());
    assert!(
        main.peak() > 0.0,
        "the Grand-Orgue sounds on the main output"
    );
}

#[test]
fn the_buses_sum_to_the_main_output() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut h = harness();
    retire_all(&mut h);
    for stop in [0usize, 9, 20] {
        h.automate(0, &stop_param_id(stop), 1.0);
    }
    // The master chain made transparent: no room tail, the EQ off so its
    // voicing is not voiced, and the level at unity, so the main output
    // is the engine's mix the buses are drawn from. The limiter, under
    // its ceiling, passes untouched.
    h.automate(0, "reverb", 0.0);
    h.automate(0, "eq", 0.0);
    note_on(&mut h, 0, 60);
    note_on(&mut h, 1, 64);
    note_on(&mut h, 2, 48);
    let main = h.render(4800);
    let (mut sum_l, mut sum_r) = (vec![0.0f32; 4800], vec![0.0f32; 4800]);
    for d in 0..16 {
        let bus = h.aux_output(d).unwrap();
        for (o, x) in sum_l.iter_mut().zip(bus.channel(0)) {
            *o += x;
        }
        for (o, x) in sum_r.iter_mut().zip(bus.channel(1)) {
            *o += x;
        }
    }
    let gap = |a: &[f32], b: &[f32]| {
        a.iter()
            .zip(b)
            .map(|(x, y)| (x - y).abs())
            .fold(0.0f32, f32::max)
    };
    let (gl, gr) = (gap(&sum_l, main.channel(0)), gap(&sum_r, main.channel(1)));
    // Three chests, a division each: the sum is the same arithmetic the
    // main output does. The gap stays at what was measured, 0 here.
    assert!(
        gl <= 1e-6 && gr <= 1e-6,
        "the buses' sum misses by {gl}/{gr}"
    );
}

#[test]
fn the_main_output_is_the_same_with_the_buses_off_or_on() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut on = harness();
    let mut off = harness();
    for d in 0..16 {
        off.set_aux_active(d, false);
    }
    for h in [&mut on, &mut off] {
        note_on(h, 0, 60);
        note_on(h, 1, 64);
    }
    let a = on.render(4800);
    let b = off.render(4800);
    assert_eq!(a.channel(0), b.channel(0));
    assert_eq!(a.channel(1), b.channel(1));
}

#[test]
fn organs_with_different_division_counts_work() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // The Chapel Organ: a manual and a pedal, channels 0 and 1.
    set_organ_for_new_instances(built_in_organ("Chapel Organ").unwrap());
    let mut h = harness();
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 1.0);
    }
    note_on(&mut h, 0, 60);
    note_on(&mut h, 1, 48);
    h.render(2400);
    for d in 0..2 {
        assert!(
            peak(h.aux_output(d).unwrap()) > 0.0,
            "the Chapel Organ's division {d} is heard"
        );
    }
    for d in 2..16 {
        assert_eq!(peak(h.aux_output(d).unwrap()), 0.0, "bus {d}");
    }

    // The French Romantic organ: four manuals and a pedal, channels 0..4.
    set_organ_for_new_instances(built_in_organ("French Romantic Organ").unwrap());
    let mut h = harness();
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 1.0);
    }
    for channel in 0..4u8 {
        note_on(&mut h, channel, 48);
    }
    h.render(2400);
    for d in 0..4 {
        assert!(
            peak(h.aux_output(d).unwrap()) > 0.0,
            "the French organ's division {d} is heard"
        );
    }
    for d in 4..16 {
        assert_eq!(peak(h.aux_output(d).unwrap()), 0.0, "bus {d}");
    }
}

/// How the hosted checks drive a processor: one block, main output and
/// the sixteen buses.
type Run<'a> =
    &'a mut dyn FnMut(&mut AudioBuffer<f32>, &ProcessContext<'_, f32>, &mut [AudioBuffer<f32>]);

/// A Grand-Orgue key through `run`: the demonstration organ's division 1
/// sounds on bus 1, and nowhere else.
fn check_a_division_on_its_bus(run: Run<'_>, muted: usize) {
    let mut events = EventBuffer::with_capacity(1);
    events
        .push(Event {
            offset: 0,
            kind: EventKind::NoteOn {
                note: NoteRef::new(0, 60),
                velocity: 0.8,
            },
        })
        .unwrap();
    let transport = Transport::STOPPED;
    let context = ProcessContext::new(&events, &transport);
    let mut main = AudioBuffer::<f32>::new(2, 2400);
    let mut aux: Vec<AudioBuffer<f32>> =
        (0..16).map(|_| AudioBuffer::<f32>::new(2, 2400)).collect();
    for _ in 0..10 {
        run(&mut main, &context, &mut aux);
    }
    assert!(main.peak() > 0.0, "the Grand-Orgue on the main output");
    for (d, bus) in aux.iter().enumerate() {
        let p = peak(bus);
        if d == 1 {
            assert_eq!(p > 0.0, muted != 1, "the Grand-Orgue's bus");
        } else {
            assert_eq!(p, 0.0, "bus {d}");
        }
    }
}

// --- CLAP ------------------------------------------------------------------------

#[test]
fn a_clap_host_sees_each_division_on_a_port_of_its_own() {
    use manguebyte_host::clap::ClapLibrary;

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    static ENTRY: manguebyte_clap::clap_sys::entry::clap_plugin_entry =
        manguebyte_clap::plugin_entry::<Caecilia>();
    // SAFETY: the entry point is the framework's own CLAP wrapper around
    // Caecilia, callable for as long as this test runs.
    let library = unsafe { ClapLibrary::from_entry(&raw const ENTRY, "caecilia") }.unwrap();
    let mut plugin = library.create(Caecilia::INFO.id).unwrap();
    let names: Vec<String> = plugin
        .audio_outputs()
        .iter()
        .map(|port| port.name.clone())
        .collect();
    let mut expected = vec!["Main".to_string()];
    expected.extend(NAMES.iter().map(|n| n.to_string()));
    assert_eq!(names, expected);
    for (i, port) in plugin.audio_outputs().iter().enumerate() {
        assert_eq!(port.channels, 2, "port {i} is stereo");
        assert_eq!(port.is_main, i == 0);
    }

    // The Grand-Orgue's bus switched off before activation, as a host does.
    plugin.set_audio_port_active(false, 2, false).unwrap();
    let mut processor = plugin.activate(SR, 256).unwrap();
    check_a_division_on_its_bus(
        &mut |main, context, aux| processor.process_with_aux(main, context, aux),
        1,
    );
    drop(processor);
    plugin.deactivate().unwrap();
    assert_eq!(plugin.failed_blocks(), 0);
}

// --- VST 3 ------------------------------------------------------------------------

#[test]
fn a_vst3_host_sees_each_division_on_a_bus_of_its_own() {
    use manguebyte_host::vst3::Vst3Module;

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    // SAFETY: the factory is the framework's own VST 3 wrapper around
    // Caecilia, callable for as long as this test runs.
    let module = unsafe {
        Vst3Module::from_factory(manguebyte_vst3::plugin_factory::<Caecilia>(), "caecilia")
    }
    .unwrap();
    let class = module.classes()[0].id;
    let mut plugin = module.create(class).unwrap();
    let names: Vec<String> = plugin
        .audio_outputs()
        .iter()
        .map(|bus| bus.name.clone())
        .collect();
    let mut expected = vec!["Output".to_string()];
    expected.extend(NAMES.iter().map(|n| n.to_string()));
    assert_eq!(names, expected);
    for (i, bus) in plugin.audio_outputs().iter().enumerate() {
        assert_eq!(bus.channels, 2, "bus {i} is stereo");
        assert_eq!(bus.is_main, i == 0);
    }

    // The Grand-Orgue's bus switched off before activation, as a host does.
    plugin.set_audio_output_active(2, false).unwrap();
    let mut processor = plugin.activate(SR, 256).unwrap();
    check_a_division_on_its_bus(
        &mut |main, context, aux| processor.process_with_aux(main, context, aux),
        1,
    );
    drop(processor);
    plugin.deactivate().unwrap();
    assert_eq!(plugin.failed_blocks(), 0);
}

#[test]
fn the_buses_are_named_in_division_order() {
    assert_eq!(DIVISION_BUSES.len(), NAMES.len());
    for (bus, name) in DIVISION_BUSES.iter().zip(NAMES) {
        assert_eq!(bus.name, name);
        assert_eq!(bus.channels, 2);
        assert!(bus.active_by_default);
    }
}
