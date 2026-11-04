// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Every pipe in its own place, and the room's first reflections.

mod common;

use caecilia_engine::{Command, OrganEngine, PoolConfig};
use caecilia_model::{Calibration, DivisionId, Organ, StopId, caecilia_organ};
use common::{BLOCK, Render, SR, blocks_for, run};

const PLATE: usize = 4;
const HALL: usize = 2;
const CATHEDRAL: usize = 3;

fn engine(organ: &Organ, placed: bool, stop_names: &[(&str, &str)]) -> OrganEngine {
    let build = if placed {
        OrganEngine::with_space
    } else {
        OrganEngine::with_config
    };
    let mut e = build(
        organ,
        SR,
        BLOCK,
        512,
        Calibration::V2,
        PoolConfig::offline(),
    );
    let stops: Vec<StopId> = organ
        .stops()
        .iter()
        .filter(|s| {
            let division = organ.division(s.division).map(|d| d.name.as_str());
            stop_names
                .iter()
                .any(|(d, n)| Some(*d) == division && *n == s.name)
        })
        .map(|s| s.id)
        .collect();
    assert_eq!(stops.len(), stop_names.len());
    let t = e.table_for(organ, &stops, &[]);
    e.set_engaged(t);
    e
}

fn great(organ: &Organ) -> DivisionId {
    organ.primary_manual()
}

/// A key held for `held` seconds, then `after` seconds of what's left.
fn key(e: &mut OrganEngine, division: DivisionId, note: u8, held: f64, after: f64) -> Render {
    let off = blocks_for(held);
    run(
        e,
        off + blocks_for(after),
        |b| {
            if b == 0 {
                vec![Command::NoteOn { division, note }]
            } else if b == off {
                vec![Command::NoteOff { division, note }]
            } else {
                Vec::new()
            }
        },
        |_, _| {},
    )
}

fn energy(x: &[f32]) -> f64 {
    x.iter().map(|v| f64::from(*v) * f64::from(*v)).sum()
}

const MONTRE: &[(&str, &str)] = &[("Grand-Orgue", "Montre 8")];

#[test]
fn the_c_and_c_sharp_sides_come_from_opposite_sides() {
    let organ = caecilia_organ();
    let lean = |note: u8| {
        let mut e = engine(&organ, true, MONTRE);
        e.set_room(PLATE);
        let d = great(&organ);
        let x = key(&mut e, d, note, 0.5, 0.0);
        energy(&x.l) / energy(&x.r)
    };
    // The largest pipes stand outside, C on the left and C# on the right.
    let (c, c_sharp) = (lean(36), lean(37));
    assert!(c > 1.5, "low C leans left: {c}");
    assert!(c_sharp < 1.0 / 1.5, "low C# leans right: {c_sharp}");
    // Pipes further up stand nearer the middle, still on their side.
    let high = lean(72);
    assert!(1.0 < high && high < c, "high C, nearer the middle: {high}");
}

#[test]
fn placing_the_pipes_keeps_the_organ_about_as_loud() {
    let organ = caecilia_organ();
    let loud = |placed: bool| {
        let mut e = engine(&organ, placed, MONTRE);
        let d = great(&organ);
        let x = run(
            &mut e,
            blocks_for(1.0),
            |b| {
                if b == 0 {
                    [48u8, 55, 60, 64, 67]
                        .map(|note| Command::NoteOn { division: d, note })
                        .to_vec()
                } else {
                    Vec::new()
                }
            },
            |_, _| {},
        );
        energy(&x.l) + energy(&x.r)
    };
    let db = 10.0 * (loud(true) / loud(false)).log10();
    assert!(db.abs() < 4.0, "{db:.2} dB");
}

#[test]
fn the_player_does_not_wait_for_the_sound_to_walk_down_the_nave() {
    let organ = caecilia_organ();
    let onset = |placed: bool| {
        let mut e = engine(&organ, placed, MONTRE);
        let d = great(&organ);
        let x = key(&mut e, d, 60, 0.2, 0.0);
        x.l.iter()
            .zip(&x.r)
            .position(|(l, r)| l.abs().max(r.abs()) > 1e-4)
            .expect("it sounds")
    };
    let late = onset(true) as f64 - onset(false) as f64;
    // A real listener 9 m away would wait 26 ms.
    assert!(late / SR < 0.008, "{:.1} ms later", late / SR * 1000.0);
}

#[test]
fn the_room_answers_and_each_room_differently() {
    let organ = caecilia_organ();
    let tail = |room: usize| {
        let mut e = engine(&organ, true, MONTRE);
        e.set_room(room);
        let d = great(&organ);
        let x = key(&mut e, d, 60, 0.5, 0.5);
        let from = blocks_for(0.5) * BLOCK + (0.08 * SR) as usize;
        energy(&x.l[from..]) + energy(&x.r[from..])
    };
    let (plate, hall, cathedral) = (tail(PLATE), tail(HALL), tail(CATHEDRAL));
    assert!(hall > plate * 2.0, "the hall answers: {hall} vs {plate}");
    let apart = (cathedral / hall).log10().abs();
    assert!(apart > 0.05, "{cathedral} vs {hall}");
}

#[test]
fn after_the_reflections_it_falls_silent() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, true, MONTRE);
    let d = great(&organ);
    let x = key(&mut e, d, 60, 0.3, 2.0);
    let last = x.l.len() - BLOCK;
    assert!(energy(&x.l[last..]) + energy(&x.r[last..]) < 1e-9);
}

#[test]
fn every_organ_can_be_placed() {
    let organ = caecilia_model::demo_organ();
    let mut e = OrganEngine::with_space(
        &organ,
        SR,
        BLOCK,
        512,
        Calibration::V2,
        PoolConfig::offline(),
    );
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let t = e.table_for(&organ, &all, &[]);
    e.set_engaged(t);
    for room in 0..caecilia_engine::ROOMS {
        e.set_room(room);
        let x = key(&mut e, DivisionId(1), 60, 0.1, 0.0);
        assert!(energy(&x.l) > 0.0, "room {room}");
        assert!(x.l.iter().chain(&x.r).all(|v| v.is_finite()));
    }
}

/// The energy of `x` at `hz`.
fn at_frequency(x: &[f32], hz: f64) -> f64 {
    let w = std::f64::consts::TAU * hz / SR;
    let (mut re, mut im) = (0.0, 0.0);
    for (n, v) in x.iter().enumerate() {
        let window = 0.5 - 0.5 * (std::f64::consts::TAU * n as f64 / x.len() as f64).cos();
        let v = f64::from(*v) * window;
        re += v * (w * n as f64).cos();
        im += v * (w * n as f64).sin();
    }
    (re * re + im * im) / x.len() as f64
}

#[test]
fn a_pipe_has_air_between_its_harmonics() {
    let organ = caecilia_organ();
    let between = |placed: bool| {
        let mut e = engine(&organ, placed, MONTRE);
        e.set_room(PLATE);
        let d = great(&organ);
        let x = key(&mut e, d, 69, 1.5, 0.0);
        let from = (0.5 * SR) as usize;
        // Halfway between A 440 and its octave.
        at_frequency(&x.l[from..], 660.0) + at_frequency(&x.r[from..], 660.0)
    };
    let (dry, voiced) = (between(false), between(true));
    assert!(voiced > dry * 4.0, "{voiced} vs {dry}");
}

#[test]
fn a_repeated_key_is_not_a_repeated_sample() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, true, MONTRE);
    e.set_room(PLATE);
    let d = great(&organ);
    let first = key(&mut e, d, 60, 0.3, 1.0);
    let second = key(&mut e, d, 60, 0.3, 1.0);
    let span = 0..(0.3 * SR) as usize;
    let difference: f64 = first.l[span.clone()]
        .iter()
        .zip(&second.l[span.clone()])
        .map(|(a, b)| f64::from(a - b) * f64::from(a - b))
        .sum();
    assert!(difference > energy(&first.l[span]) * 0.1, "{difference}");
}

#[test]
fn a_pipe_comes_up_to_speech_rather_than_starting_at_full_level() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, true, MONTRE);
    e.set_room(PLATE);
    let d = great(&organ);
    let x = key(&mut e, d, 60, 0.5, 0.0);
    let ms = |t: f64| (t * SR / 1000.0) as usize;
    let early = energy(&x.l[..ms(8.0)]) / ms(8.0) as f64;
    let steady = energy(&x.l[ms(200.0)..ms(400.0)]) / ms(200.0) as f64;
    assert!(early < steady * 0.5, "{early} vs {steady}");
    assert!(early > 0.0, "it has begun to speak");
}

/// The sharpest corner in the sound, either channel: what a click makes and a
/// partial below a few kilohertz doesn't.
fn largest_step(x: &Render, from: usize, to: usize) -> f32 {
    [&x.l, &x.r]
        .iter()
        .flat_map(|c| (from..to).map(move |i| (c[i] - 2.0 * c[i - 1] + c[i - 2]).abs()))
        .fold(0.0, f32::max)
}

#[test]
fn a_key_repeated_while_its_pipe_dies_away_does_not_click() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, true, MONTRE);
    e.set_room(PLATE);
    let d = great(&organ);
    // Down, up, and down again while the first note's release still sounds,
    // the same pipe twice at once.
    let (up, again) = (blocks_for(0.4), blocks_for(0.45));
    let total = blocks_for(1.2);
    let x = run(
        &mut e,
        total,
        |b| {
            if b == 0 {
                // One pipe in each of the pool's eight groups, so the repeat
                // lands in the group the dying note is in.
                [60u8, 62, 64, 65, 67, 69, 71, 72]
                    .map(|note| Command::NoteOn { division: d, note })
                    .to_vec()
            } else if b == again {
                vec![Command::NoteOn {
                    division: d,
                    note: 60,
                }]
            } else if b == up {
                vec![Command::NoteOff {
                    division: d,
                    note: 60,
                }]
            } else {
                Vec::new()
            }
        },
        |_, _| {},
    );
    // What a plain note moves by, against what the repeat does.
    let steady = largest_step(&x, BLOCK * blocks_for(0.2), BLOCK * up);
    let repeat = largest_step(&x, BLOCK * up, BLOCK * blocks_for(0.8));
    assert!(
        repeat < steady * 2.0,
        "the repeat turns by {repeat} where a held chord turns by {steady}"
    );
}

#[test]
fn a_passage_of_repeated_notes_and_chords_has_no_corners_standing_out() {
    let organ = caecilia_organ();
    let stops = &[
        ("Grand-Orgue", "Montre 8"),
        ("Grand-Orgue", "Prestant 4"),
        ("Grand-Orgue", "Bourdon 8"),
    ];
    let mut e = engine(&organ, true, stops);
    e.set_room(PLATE);
    let d = great(&organ);
    // A passage with repeated keys and overlapping chords, every 90 ms.
    let steps: [&[u8]; 8] = [
        &[60, 64, 67],
        &[60, 65, 69],
        &[60, 64, 67],
        &[59, 62, 67],
        &[60, 64, 67],
        &[62, 65, 69],
        &[60],
        &[60, 64, 67, 72],
    ];
    let every = blocks_for(0.09);
    let total = every * steps.len() * 4;
    let x = run(
        &mut e,
        total,
        |b| {
            if b % every != 0 {
                return Vec::new();
            }
            let i = b / every;
            let mut commands = Vec::new();
            if i > 0 {
                for note in steps[(i - 1) % steps.len()] {
                    commands.push(Command::NoteOff {
                        division: d,
                        note: *note,
                    });
                }
            }
            for note in steps[i % steps.len()] {
                commands.push(Command::NoteOn {
                    division: d,
                    note: *note,
                });
            }
            commands
        },
        |_, _| {},
    );
    let mut corners: Vec<f32> = (1..total)
        .map(|b| largest_step(&x, b * BLOCK, (b + 1) * BLOCK))
        .collect();
    corners.sort_by(f32::total_cmp);
    let (typical, worst) = (corners[corners.len() / 2], corners[corners.len() - 1]);
    assert!(worst < typical * 3.0, "{worst} vs typically {typical}");
}
