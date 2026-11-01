// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! What 2.0 does better than 1.x, and by how much.
//!
//! Every test renders the same case under `Calibration::OneX` (1.x, reproduced
//! to 0.0001 dB by `reference.rs`) and `Calibration::V2`, measures both the same
//! way, prints the pair, and asserts that 2.0 moved where it was meant to and
//! that 1.x still measures what it measured. `--nocapture` shows the table.

mod common;

use caecilia_engine::{Command, OrganEngine, Timed};
use caecilia_model::{Calibration, DivisionId, Organ, StopId, WindchestId, demo_organ};
use common::*;

const GO: DivisionId = DivisionId(1);
const RECIT: DivisionId = DivisionId(2);

fn engine(organ: &Organ, cal: Calibration, block: usize) -> OrganEngine {
    OrganEngine::with_calibration(organ, SR, block, 512, cal)
}

fn stop(organ: &Organ, division: DivisionId, name: &str) -> StopId {
    organ
        .stops()
        .iter()
        .find(|s| s.division == division && s.name == name)
        .unwrap()
        .id
}

/// Render `seconds` in blocks of `block`, with `at(frame)` giving the commands
/// for the block that starts at `frame`.
fn render(
    e: &mut OrganEngine,
    block: usize,
    seconds: f64,
    mut at: impl FnMut(usize) -> Vec<Command>,
) -> Render {
    let total = (seconds * SR) as usize / block * block;
    let mut x = Render {
        l: vec![0.0; total],
        r: vec![0.0; total],
    };
    for start in (0..total).step_by(block) {
        let events: Vec<Timed> = at(start)
            .into_iter()
            .map(|command| Timed { offset: 0, command })
            .collect();
        e.process(
            &mut x.l[start..start + block],
            &mut x.r[start..start + block],
            &events,
        );
    }
    x
}

fn report(what: &str, onex: f64, v2: f64, unit: &str) {
    eprintln!("{what:<52} 1.x {onex:>9.3} {unit:<6} 2.0 {v2:>9.3} {unit}");
}

/// A fine Goertzel scan for the strongest frequency near `around`.
fn pitch_near(x: &Render, around: f64, from: usize, to: usize) -> f64 {
    let (mut best, mut best_hz) = (0.0, around);
    let mut hz = around * 0.995;
    while hz < around * 1.005 {
        let m = goertzel(x, hz, from, to);
        if m > best {
            best = m;
            best_hz = hz;
        }
        hz += 0.005;
    }
    best_hz
}

#[test]
fn a_full_chord_is_heard_to_land_on_the_wind() {
    let organ = demo_organ();
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let more = [72u8, 76, 79, 83, 86, 89, 91, 93];
    let sag = |cal| {
        let mut e = engine(&organ, cal, BLOCK);
        let t = e.table_for(&organ, &all, &[]);
        e.set_engaged(t);
        let mut lowest = f64::MAX;
        for b in 0..blocks_for(3.0) {
            let mut c = Vec::new();
            if b == 0 {
                c.push(Command::NoteOn {
                    division: GO,
                    note: 60,
                });
            }
            if b == blocks_for(1.0) {
                c.extend(
                    more.iter()
                        .map(|&note| Command::NoteOn { division: GO, note }),
                );
            }
            let events: Vec<Timed> = c
                .into_iter()
                .map(|command| Timed { offset: 0, command })
                .collect();
            let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
            e.process(&mut l, &mut r, &events);
            let w = e.wind();
            lowest = lowest.min(f64::from(
                w.pressure_at(WindchestId(1), 0) / w.nominal_pressure_pa(WindchestId(1)),
            ));
        }
        100.0 * (1.0 - lowest)
    };
    let (onex, v2) = (sag(Calibration::OneX), sag(Calibration::V2));
    report("Grand-Orgue sag, all stops, nine keys", onex, v2, "%");
    assert!(onex < 0.4, "1.x measured {onex}%");
    assert!(
        (2.0..=4.0).contains(&v2),
        "2.0 sags {v2}%: audible, still well winded"
    );
}

#[test]
fn a_reed_goes_flat_when_the_chord_lands() {
    let organ = demo_organ();
    let mut stops: Vec<StopId> = organ
        .stops()
        .iter()
        .filter(|s| s.division == GO)
        .map(|s| s.id)
        .collect();
    let reed = stop(&organ, RECIT, "Trompette 8");
    stops.push(reed);
    // The load sits a semitone and more above the reed's C4 even through the
    // 16' ranks, so nothing it plays lands near the fundamental being measured.
    let load: Vec<u8> = (73..=90).collect();
    let dip = |cal| {
        let pitch = |loaded: bool| {
            let mut e = engine(&organ, cal, BLOCK);
            let t = e.table_for(&organ, &stops, &[]);
            e.set_engaged(t);
            let x = render(&mut e, BLOCK, 3.5, |f| {
                let mut c = Vec::new();
                if f == 0 {
                    c.push(Command::NoteOn {
                        division: RECIT,
                        note: 60,
                    });
                }
                if loaded && f == blocks_for(1.0) * BLOCK {
                    c.extend(
                        load.iter()
                            .map(|&note| Command::NoteOn { division: GO, note }),
                    );
                }
                c
            });
            pitch_near(&x, 261.6256, (1.5 * SR) as usize, x.l.len())
        };
        1200.0 * (pitch(true) / pitch(false)).log2()
    };
    let (onex, v2) = (dip(Calibration::OneX), dip(Calibration::V2));
    report(
        "Récit Trompette C4 under an 18-key chord",
        onex,
        v2,
        "cents",
    );
    assert!(onex > -1.0 && onex <= 0.0, "1.x moved {onex} cents");
    assert!(
        v2 < -1.5,
        "2.0 must dip audibly against the flues: {v2} cents"
    );
}

fn tremulant_swing(organ: &Organ, cal: Calibration, block: usize) -> f64 {
    let bourdon = stop(organ, RECIT, "Bourdon 8");
    let mut e = engine(organ, cal, block);
    let t = e.table_for(organ, &[bourdon], &[]);
    e.set_engaged(t);
    let trem = Command::Tremulant {
        chest: WindchestId(2),
        enabled: true,
        rate_hz: 5.5,
        depth_fraction: 0.15,
    };
    let x = render(&mut e, block, 4.0, |f| match f {
        0 => vec![
            Command::NoteOn {
                division: RECIT,
                note: 60,
            },
            trem,
        ],
        _ => vec![],
    });
    let win = (SR * 0.005) as usize;
    let env = envelope(&x, win);
    let (lo, hi) = ((1.5 * SR) as usize / win..(3.5 * SR) as usize / win)
        .fold((f64::MAX, 0.0f64), |(lo, hi), w| {
            (lo.min(env[w]), hi.max(env[w]))
        });
    db(hi) - db(lo)
}

#[test]
fn the_tremulant_reaches_the_sound() {
    let organ = demo_organ();
    let (onex, v2) = (
        tremulant_swing(&organ, Calibration::OneX, BLOCK),
        tremulant_swing(&organ, Calibration::V2, BLOCK),
    );
    report("Bourdon 8 envelope swing, tremulant at 0.6", onex, v2, "dB");
    assert!(onex < 1.6, "1.x measured {onex} dB");
    assert!(v2 > 2.0, "2.0 swing {v2} dB");
}

#[test]
fn the_tremulant_sounds_the_same_at_any_buffer_size() {
    let organ = demo_organ();
    let spread = |cal| {
        let s: Vec<f64> = [64, 256, 1024, 2048]
            .iter()
            .map(|&b| tremulant_swing(&organ, cal, b))
            .collect();
        s.iter().copied().fold(f64::MIN, f64::max) - s.iter().copied().fold(f64::MAX, f64::min)
    };
    let (onex, v2) = (spread(Calibration::OneX), spread(Calibration::V2));
    report(
        "tremulant swing, spread over 64..2048-frame buffers",
        onex,
        v2,
        "dB",
    );
    assert!(v2 < 0.3, "2.0 depends on the buffer by {v2} dB");
    assert!(onex > v2, "1.x read the wind once per buffer");
}

#[test]
fn distinct_reeds_have_distinct_voices() {
    let organ = demo_organ();
    let basson = stop(&organ, RECIT, "Basson-Hautbois 8");
    let trompette = stop(&organ, RECIT, "Trompette 8");
    let difference = |cal| {
        let profile = |s| {
            let mut e = engine(&organ, cal, BLOCK);
            let t = e.table_for(&organ, &[s], &[]);
            e.set_engaged(t);
            let x = render(&mut e, BLOCK, 2.0, |f| {
                if f == 0 {
                    vec![Command::NoteOn {
                        division: RECIT,
                        note: 60,
                    }]
                } else {
                    vec![]
                }
            });
            // Each harmonic at its own peak: every rank sits a few cents from equal
            // temperament on purpose, and at the twelfth harmonic that is outside a
            // one-second window's main lobe. Normalised to the loudest, so this
            // compares the SHAPE of the two voices.
            let p: Vec<f64> = (1..=12)
                .map(|k| {
                    let h = 261.6256 * k as f64;
                    (0..=60)
                        .map(|i| {
                            goertzel(
                                &x,
                                h * (0.997 + 0.0001 * i as f64),
                                SR as usize,
                                2 * SR as usize,
                            )
                        })
                        .fold(0.0, f64::max)
                })
                .map(db)
                .collect();
            let top = p.iter().copied().fold(f64::MIN, f64::max);
            p.into_iter().map(|v| v - top).collect::<Vec<_>>()
        };
        let (a, b) = (profile(basson), profile(trompette));
        a.iter()
            .zip(&b)
            .map(|(x, y)| (x - y).abs())
            .fold(0.0, f64::max)
    };
    let (onex, v2) = (difference(Calibration::OneX), difference(Calibration::V2));
    report(
        "Basson-Hautbois vs Trompette, largest harmonic diff",
        onex,
        v2,
        "dB",
    );
    assert!(onex < 1.0, "1.x voiced them alike: {onex} dB");
    assert!(v2 > 5.0, "2.0 voices them apart: {v2} dB");
}

/// Harmonic `k` of C4 against the fundamental, in a 40 ms window at `from`.
fn h_over_h1(x: &Render, k: usize, from: usize) -> f64 {
    let to = from + (0.04 * SR) as usize;
    db(goertzel(x, 261.6256 * k as f64, from, to)) - db(goertzel(x, 261.6256, from, to))
}

#[test]
fn a_pipe_darkens_as_it_dies_away_and_takes_as_long() {
    let organ = demo_organ();
    let montre = stop(&organ, GO, "Montre 8");
    let measure = |cal| {
        let mut e = engine(&organ, cal, BLOCK);
        let t = e.table_for(&organ, &[montre], &[]);
        e.set_engaged(t);
        let off = blocks_for(1.5) * BLOCK;
        let x = render(&mut e, BLOCK, 2.5, |f| match f {
            0 => vec![Command::NoteOn {
                division: GO,
                note: 60,
            }],
            f if f == off => vec![Command::NoteOff {
                division: GO,
                note: 60,
            }],
            _ => vec![],
        });
        let ms = |t: f64| (t * SR / 1000.0) as usize;
        let darkening = h_over_h1(&x, 4, off + ms(80.0)) - h_over_h1(&x, 4, off);
        let steady = rms(&x, off - SR as usize / 2, off);
        let win = ms(5.0);
        let env = envelope(&x, win);
        let release_ms = (off / win..env.len())
            .find(|&w| env[w] <= steady * 0.01)
            .map_or(-1.0, |w| (w * win - off) as f64 / SR * 1000.0);
        // The largest step between samples around the release, against the
        // steady state's own: a release that clicks makes a larger one.
        let step = |a: usize, b: usize| {
            (a..b)
                .map(|i| (x.l[i] - x.l[i - 1]).abs())
                .fold(0.0f32, f32::max)
        };
        let click = f64::from(step(off, off + ms(20.0)) / step(off - ms(20.0), off));
        (darkening, release_ms, click)
    };
    let (d1, r1, c1) = measure(Calibration::OneX);
    let (d2, r2, c2) = measure(Calibration::V2);
    report(
        "Montre 8 C4: H4/H1 change over 80 ms of release",
        d1,
        d2,
        "dB",
    );
    report("Montre 8 C4: release to -40 dB", r1, r2, "ms");
    report(
        "Montre 8 C4: largest step at release vs steady",
        c1,
        c2,
        "x",
    );
    assert!(d1.abs() < 0.5, "1.x released at one colour: {d1} dB");
    assert!(d2 < -2.0, "2.0 darkens as it dies: {d2} dB");
    assert!(
        (r2 / r1 - 1.0).abs() < 0.3,
        "release length kept: {r2} ms against {r1}"
    );
    assert!(c2 < 1.2, "no click at the release: {c2}x");
}

#[test]
fn every_family_lives_its_own_life() {
    use caecilia_synth::{AdditiveVoice, liveliness_for};
    let organ = demo_organ();
    let e = engine(&organ, Calibration::V2, BLOCK);
    for s in organ.stops() {
        for (cal, family) in [
            (Calibration::OneX, caecilia_model::TonalFamily::Principal),
            (Calibration::V2, s.family),
        ] {
            let mut v = AdditiveVoice::new(SR, cal);
            v.adopt_rank(&e.voicings()[s.id.index()]);
            assert_eq!(
                v.bank().liveliness(),
                liveliness_for(family),
                "{} under {cal:?}",
                s.name
            );
        }
    }
}
