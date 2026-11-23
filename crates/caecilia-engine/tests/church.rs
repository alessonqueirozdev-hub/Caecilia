// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organ through a church's desk: dry, mono, even, and what a mono
//! cable that takes the left side alone hears.

mod common;

use caecilia_engine::{Command, OrganEngine, PoolConfig};
use caecilia_model::{Calibration, Organ, StopId, caecilia_organ};
use common::{BLOCK, Render, SR, blocks_for, db, run};

fn engine(organ: &Organ, stop_names: &[&str]) -> OrganEngine {
    let mut e = OrganEngine::with_space(
        organ,
        SR,
        BLOCK,
        512,
        Calibration::V2,
        PoolConfig::offline(),
    );
    draw(&mut e, organ, stop_names);
    e
}

fn draw(e: &mut OrganEngine, organ: &Organ, stop_names: &[&str]) {
    let great = organ.primary_manual();
    let stops: Vec<StopId> = organ
        .stops()
        .iter()
        .filter(|s| s.division == great && stop_names.contains(&s.name.as_str()))
        .map(|s| s.id)
        .collect();
    assert_eq!(
        stops.len(),
        stop_names.len(),
        "the great has {:?}",
        organ
            .stops()
            .iter()
            .filter(|s| s.division == great)
            .map(|s| s.name.as_str())
            .collect::<Vec<_>>()
    );
    let t = e.table_for(organ, &stops, &[]);
    e.set_engaged(t);
}

const MONTRE: &[&str] = &["Montre 8"];
const PLENUM: &[&str] = &[
    "Montre 16",
    "Montre 8",
    "Prestant 4",
    "Doublette 2",
    "Fourniture IV",
    "Trompette 8",
    "Clairon 4",
];

/// `notes` held together for `seconds`.
fn chord(e: &mut OrganEngine, organ: &Organ, notes: &[u8], seconds: f64) -> Render {
    let division = organ.primary_manual();
    run(
        e,
        blocks_for(seconds),
        |b| {
            if b == 0 {
                notes
                    .iter()
                    .map(|&note| Command::NoteOn { division, note })
                    .collect()
            } else {
                Vec::new()
            }
        },
        |_, _| {},
    )
}

fn power(x: &[f32]) -> f64 {
    x.iter().map(|v| f64::from(*v).powi(2)).sum::<f64>() / x.len().max(1) as f64
}

/// From 0.3 s on: past the speech, the glides and the first reflections.
fn settled(x: &[f32]) -> &[f32] {
    &x[(0.3 * SR) as usize..]
}

#[test]
fn dry_puts_every_pipe_in_the_middle_with_no_room() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, PLENUM);
    e.set_dry(true);
    e.reset();
    let x = chord(&mut e, &organ, &[60, 64, 67, 72], 0.8);
    let (l, r) = (settled(&x.l), settled(&x.r));
    let apart = l
        .iter()
        .zip(r)
        .map(|(a, b)| f64::from(a - b).powi(2))
        .sum::<f64>();
    let whole = l.iter().map(|a| f64::from(*a).powi(2)).sum::<f64>();
    assert!(whole > 0.0);
    assert!(
        apart / whole < 1e-9,
        "the sides differ by {:.1} dB",
        10.0 * (apart / whole).log10()
    );
}

/// The left side alone, as a stereo jack into a mono plug passes it: how far
/// apart the loudest and the quietest of twelve notes are, in dB.
fn left_alone_spread(dry: bool) -> f64 {
    let organ = caecilia_organ();
    let levels: Vec<f64> = (60u8..72)
        .map(|note| {
            let mut e = engine(&organ, MONTRE);
            e.set_dry(dry);
            e.reset();
            let x = chord(&mut e, &organ, &[note], 0.6);
            10.0 * power(settled(&x.l)).log10()
        })
        .collect();
    let most = levels.iter().copied().fold(f64::MIN, f64::max);
    let least = levels.iter().copied().fold(f64::MAX, f64::min);
    most - least
}

#[test]
fn through_a_mono_plug_dry_keeps_a_melody_level() {
    let full = left_alone_spread(false);
    let dry = left_alone_spread(true);
    eprintln!(
        "left side alone, C4 to B4 on the Montre 8: full {full:.1} dB apart, dry {dry:.1} dB"
    );
    assert!(dry < full, "dry {dry:.1} dB, full {full:.1} dB");
    assert!(dry < 3.0, "dry notes are {dry:.1} dB apart");
}

/// What a desk that sums the two sides keeps of the stereo, in dB.
fn summed_keeps(mono: bool) -> f64 {
    let organ = caecilia_organ();
    let mut e = engine(&organ, PLENUM);
    e.set_mono(mono);
    e.reset();
    let x = chord(&mut e, &organ, &[60, 64, 67, 72], 0.8);
    let (l, r) = (settled(&x.l), settled(&x.r));
    let sum: Vec<f32> = l.iter().zip(r).map(|(a, b)| 0.5 * (a + b)).collect();
    10.0 * (power(&sum) / (0.5 * (power(l) + power(r)))).log10()
}

#[test]
fn a_desk_that_sums_the_sides_loses_nothing_in_mono() {
    let stereo = summed_keeps(false);
    let mono = summed_keeps(true);
    eprintln!("summed to mono, the plenum keeps: stereo {stereo:.1} dB, mono {mono:.1} dB");
    // The room's reflections stay, and they are not the same on both sides.
    assert!(mono > -1.0, "mono keeps {mono:.1} dB");
    assert!(mono > stereo);
}

/// How much louder the plenum is than the Montre 8 alone, in dB.
fn plenum_over_montre(even: bool) -> f64 {
    let organ = caecilia_organ();
    let level = |stops: &[&str]| {
        let mut e = engine(&organ, stops);
        e.set_even(even);
        e.reset();
        let x = chord(&mut e, &organ, &[60, 64, 67, 72], 0.8);
        db((power(settled(&x.l)) + power(settled(&x.r))).sqrt())
    };
    level(PLENUM) - level(MONTRE)
}

#[test]
fn even_brings_the_plenum_closer_to_a_single_stop() {
    let natural = plenum_over_montre(false);
    let even = plenum_over_montre(true);
    eprintln!("plenum over the Montre 8: natural {natural:+.1} dB, even {even:+.1} dB");
    assert!(natural > 3.0, "natural {natural:.1} dB");
    assert!(
        even < natural - 2.0,
        "even {even:.1} dB, natural {natural:.1} dB"
    );
    assert!(even > 0.0, "the plenum is still the fuller: {even:.1} dB");
}

#[test]
fn a_registration_drawn_under_the_even_level_glides_without_a_click() {
    let organ = caecilia_organ();
    let mut e = engine(&organ, MONTRE);
    e.set_even(true);
    e.reset();
    let division = organ.primary_manual();
    let plenum: Vec<StopId> = organ
        .stops()
        .iter()
        .filter(|s| s.division == division && PLENUM.contains(&s.name.as_str()))
        .map(|s| s.id)
        .collect();
    let mut x = Render {
        l: Vec::new(),
        r: Vec::new(),
    };
    for b in 0..blocks_for(0.8) {
        let events = if b == 0 {
            vec![caecilia_engine::Timed {
                offset: 0,
                command: Command::NoteOn { division, note: 60 },
            }]
        } else {
            Vec::new()
        };
        if b == blocks_for(0.4) {
            let t = e.table_for(&organ, &plenum, &[]);
            e.set_engaged(t);
        }
        let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
        e.process(&mut l, &mut r, &events);
        x.l.extend(l);
        x.r.extend(r);
    }
    // The Montre's own pipe, already sounding, must not jump down by the
    // even level's whole step in one sample.
    let at = blocks_for(0.4) * BLOCK;
    let step = x.l[at - 4..at + 4 * BLOCK]
        .windows(2)
        .map(|w| (w[1] - w[0]).abs())
        .fold(0.0f32, f32::max);
    let swing = x.l[at - BLOCK..at]
        .iter()
        .map(|v| v.abs())
        .fold(0.0f32, f32::max);
    assert!(step < swing, "a step of {step} against a swing of {swing}");
}

/// Keys pressed and released at given seconds, with the melody lifted by
/// `lift` dB (or not); the left and right sides as they came out.
fn played(lift: Option<f32>, steps: &[(f64, Command)], seconds: f64) -> Render {
    let organ = caecilia_organ();
    let mut e = engine(&organ, MONTRE);
    e.set_dry(true);
    e.set_melody(lift);
    e.reset();
    run(
        &mut e,
        blocks_for(seconds),
        |b| {
            steps
                .iter()
                .filter(|(at, _)| blocks_for(*at) == b)
                .map(|(_, c)| *c)
                .collect()
        },
        |_, _| {},
    )
}

fn level_between(x: &Render, from: f64, to: f64) -> f64 {
    let span = (from * SR) as usize..(to * SR) as usize;
    10.0 * (power(&x.l[span.clone()]) + power(&x.r[span])).log10()
}

fn down(note: u8) -> Command {
    Command::NoteOn {
        division: caecilia_organ().primary_manual(),
        note,
    }
}

fn up(note: u8) -> Command {
    Command::NoteOff {
        division: caecilia_organ().primary_manual(),
        note,
    }
}

#[test]
fn the_top_key_as_it_goes_down_carries_the_melody() {
    let steps = [(0.0, down(72))];
    let lifted = level_between(&played(Some(6.0), &steps, 0.8), 0.3, 0.8);
    let plain = level_between(&played(None, &steps, 0.8), 0.3, 0.8);
    let gained = lifted - plain;
    assert!((gained - 6.0).abs() < 0.5, "lifted by {gained:.2} dB");
}

#[test]
fn letting_the_soprano_go_does_not_lift_the_alto() {
    // The soprano down first, the alto under it, then the soprano up: the
    // alto, held all along, sounds on as it did.
    let steps = [(0.0, down(72)), (0.05, down(64)), (0.5, up(72))];
    let lifted = level_between(&played(Some(6.0), &steps, 1.2), 0.8, 1.2);
    let plain = level_between(&played(None, &steps, 1.2), 0.8, 1.2);
    let gained = lifted - plain;
    assert!(gained.abs() < 0.3, "the alto rose by {gained:.2} dB");
}

#[test]
fn a_higher_key_does_not_quieten_a_pipe_already_speaking() {
    // The soprano held, a higher key down above it: the soprano keeps its
    // lift; the new key, the top as it went down, takes one too.
    let one = [(0.0, down(64))];
    let both = [(0.0, down(64)), (0.5, down(72))];
    let alone = level_between(&played(Some(6.0), &one, 1.2), 0.8, 1.2);
    let plain_alone = level_between(&played(None, &one, 1.2), 0.8, 1.2);
    let together = level_between(&played(Some(6.0), &both, 1.2), 0.8, 1.2);
    let plain_together = level_between(&played(None, &both, 1.2), 0.8, 1.2);
    assert!((alone - plain_alone - 6.0).abs() < 0.5);
    let gained = together - plain_together;
    assert!(
        (gained - 6.0).abs() < 0.5,
        "the pair rose by {gained:.2} dB"
    );
}
