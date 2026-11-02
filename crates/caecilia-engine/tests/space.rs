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
