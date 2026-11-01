// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The engine against what 1.x sounded like.
//!
//! The same instrument, driven the same way 1.x's `tools/reference` drove the C++ (tag `v1-cpp-final`),
//! measured with the same analysis. The partials drift on a deterministic walk
//! and the two math libraries round a few transcendentals differently, so the
//! port is held to the measured figures within the tolerances stated here --
//! not to the samples.

mod common;

use caecilia_engine::{Command, DEFAULT_POLYPHONY, OrganEngine};
use caecilia_model::{Calibration, DivisionId, StopId, TonalFamily, WindchestId, demo_organ};
use common::*;

fn engine() -> (caecilia_model::Organ, OrganEngine) {
    let organ = demo_organ();
    // The C++ instrument, reproduced: that is what these references measure.
    let e = OrganEngine::with_calibration(&organ, SR, BLOCK, DEFAULT_POLYPHONY, Calibration::OneX);
    (organ, e)
}

#[test]
fn equal_temperament_matches_1x() {
    let (_, e) = engine();
    let r = reference();
    let equal = r["tuning"]
        .as_array()
        .unwrap()
        .iter()
        .find(|t| t["temperament"] == "Equal" && f(&t["a4"]) == 440.0);
    for (n, hz) in floats(&equal.unwrap()["hz"]).into_iter().enumerate() {
        assert!(
            (e.key_frequency(n as u8) - hz).abs() <= hz * 1e-12,
            "note {n}"
        );
    }
}

#[test]
fn every_temperament_of_1x_tunes_as_it_did() {
    use caecilia_engine::Tuning;
    use manguebyte_dsp::tuning::{Scale, Temperament, TuningSettings};
    let (_, mut e) = engine();
    let r = reference();
    for t in r["tuning"].as_array().unwrap() {
        let temperament = match t["temperament"].as_str().unwrap() {
            "Equal" => Temperament::equal(),
            "QuarterMeantone" => Temperament::quarter_comma_meantone(),
            "Werckmeister3" => Temperament::werckmeister_iii(),
            "Kirnberger3" => Temperament::kirnberger_iii(),
            "Pythagorean" => Temperament::pythagorean(),
            "Young" => Temperament::young(),
            other => panic!("unknown temperament {other}"),
        };
        let settings = TuningSettings {
            scale: Scale::Temperament {
                temperament,
                key: 0,
            },
            reference_hz: f(&t["a4"]),
            ..TuningSettings::default()
        };
        let tuning: Tuning = settings.build().unwrap();
        e.set_tuning(tuning);
        for (n, hz) in floats(&t["hz"]).into_iter().enumerate() {
            let ours = e.key_frequency(n as u8);
            let cents = 1200.0 * (ours / hz).log2();
            assert!(
                cents.abs() < 1e-9,
                "{} at {}: key {n} off by {cents} cents",
                t["temperament"],
                t["a4"]
            );
        }
    }
}

#[test]
fn every_rank_sounds_as_it_did_in_1x() {
    let (organ, mut e) = engine();
    let r = reference();
    let env_win = (SR * 0.005) as usize;
    let mut worst = (0.0f64, String::new());

    for case in r["rankNotes"].as_array().unwrap() {
        let stop = StopId(f(&case["stop"]) as u16);
        let note = f(&case["note"]) as u8;
        let s = organ.stop(stop).unwrap();
        let division = s.division;
        let name = format!("{} n{note}", s.name);

        e.reset();
        let table = e.table_for(&organ, &[stop], &[]);
        e.set_engaged(table);

        let on_block = blocks_for(0.25);
        let off_block = blocks_for(2.25);
        let x = run(
            &mut e,
            blocks_for(3.5),
            |b| {
                if b == on_block {
                    vec![Command::NoteOn { division, note }]
                } else if b == off_block {
                    vec![Command::NoteOff { division, note }]
                } else {
                    vec![]
                }
            },
            |_, _| {},
        );

        let (on, off) = (on_block * BLOCK, off_block * BLOCK);
        assert_eq!(on as f64, f(&case["onFrame"]));
        let steady_from = off - SR as usize;
        let steady = rms(&x, steady_from, off);
        let env = envelope(&x, env_win);
        let attack = (on / env_win..env.len())
            .find(|&w| env[w] >= steady * 0.707_945_78)
            .map_or(-1.0, |w| (w * env_win + env_win - on) as f64 * 1000.0 / SR);
        let release = (off / env_win..env.len())
            .find(|&w| env[w] <= steady * 0.01)
            .map_or(-1.0, |w| {
                (w * env_win) as f64 * 1000.0 / SR - off as f64 * 1000.0 / SR
            });

        let level = db(steady) - f(&case["steadyRmsDb"]);
        if level.abs() > worst.0 {
            worst = (level.abs(), name.clone());
        }
        assert!(
            level.abs() < 0.01,
            "{name}: level {:+.3} dB against 1.x",
            level
        );
        assert!(
            (attack - f(&case["attackMs"])).abs() < 0.01,
            "{name}: attack {attack} vs {}",
            f(&case["attackMs"])
        );
        assert!(
            (release - f(&case["releaseMs"])).abs() < 0.01,
            "{name}: release {release} vs {}",
            f(&case["releaseMs"])
        );
        let corr = correlation(&x, steady_from, off);
        assert!(
            (corr - f(&case["correlation"])).abs() < 0.002,
            "{name}: correlation {corr} vs {}",
            f(&case["correlation"])
        );

        // The harmonic profile, wherever it is within 50 dB of its loudest bin.
        let key_hz = e.key_frequency(note);
        let theirs = floats(&case["halfKeyProfileDb"]);
        let top = theirs.iter().copied().fold(f64::MIN, f64::max);
        for (k, &t) in theirs.iter().enumerate() {
            let hz = key_hz * 0.5 * (k + 1) as f64;
            if hz >= SR * 0.45 || t < top - 50.0 {
                continue;
            }
            let ours = db(goertzel(&x, hz, steady_from, off));
            assert!(
                (ours - t).abs() < 0.05,
                "{name}: half-key bin {} at {hz:.0} Hz: {ours:.2} vs {t:.2} dB",
                k + 1
            );
        }
    }
    eprintln!(
        "largest level difference against 1.x: {:.4} dB ({})",
        worst.0, worst.1
    );
}

#[test]
fn a_full_chord_lands_on_the_wind_as_it_did_in_1x() {
    let (organ, mut e) = engine();
    let r = reference();
    let w = &r["windChord"];
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let table = e.table_for(&organ, &all, &[]);
    e.set_engaged(table);

    let manual = organ.primary_manual();
    assert_eq!(manual.0 as f64, f(&w["division"]));
    let more: Vec<u8> = floats(&w["addedNotes"])
        .into_iter()
        .map(|n| n as u8)
        .collect();
    let (add, off) = (blocks_for(1.0), blocks_for(3.0));
    let chests = organ.windchests().len();
    let mut pressure = vec![Vec::new(); chests];
    let mut sag = Vec::new();
    let mut voices = Vec::new();
    run(
        &mut e,
        blocks_for(4.5),
        |b| {
            let mut c = Vec::new();
            if b == 0 {
                c.push(Command::NoteOn {
                    division: manual,
                    note: 60,
                });
            }
            if b == add {
                c.extend(more.iter().map(|&note| Command::NoteOn {
                    division: manual,
                    note,
                }));
            }
            if b == off {
                c.push(Command::NoteOff {
                    division: manual,
                    note: 60,
                });
                c.extend(more.iter().map(|&note| Command::NoteOff {
                    division: manual,
                    note,
                }));
            }
            c
        },
        |_, e| {
            for (c, p) in pressure.iter_mut().enumerate() {
                p.push(f64::from(e.wind().pressure_at(WindchestId(c as u16), 0)));
            }
            sag.push(f64::from(e.meters().wind_sag_norm));
            voices.push(f64::from(e.meters().active_voices));
        },
    );

    assert_eq!(
        voices,
        floats(&w["activeVoices"]),
        "voices sounding, block by block"
    );
    for (c, theirs) in w["chestPressurePa"].as_array().unwrap().iter().enumerate() {
        for (b, (ours, t)) in pressure[c].iter().zip(floats(theirs)).enumerate() {
            assert!(
                (ours - t).abs() < 0.001,
                "chest {c}, block {b}: {ours} Pa vs {t} Pa"
            );
        }
    }
    for (b, (ours, t)) in sag.iter().zip(floats(&w["sagNorm"])).enumerate() {
        assert!((ours - t).abs() < 1e-5, "block {b}: sag {ours} vs {t}");
    }
}

#[test]
fn the_tremulant_moves_the_wind_and_the_note_as_it_did_in_1x() {
    let (organ, mut e) = engine();
    let r = reference();
    let t = &r["tremulant"];
    let chest = organ
        .windchests()
        .iter()
        .find(|c| c.has_tremulant)
        .unwrap()
        .id;
    let mut pick = None::<&caecilia_model::Stop>;
    for s in organ.stops() {
        if e.voicings()[s.id.index()].chest != chest {
            continue;
        }
        if pick.is_none_or(|p| s.family == TonalFamily::Flute && p.family != TonalFamily::Flute) {
            pick = Some(s);
        }
    }
    let pick = pick.unwrap();
    assert_eq!(f64::from(pick.id.0), f(&t["stop"]));
    let table = e.table_for(&organ, &[pick.id], &[]);
    e.set_engaged(table);

    let division: DivisionId = pick.division;
    let depth = 0.6 * caecilia_wind::MAX_TREMULANT_DEPTH_FRACTION;
    let (on, off) = (blocks_for(1.0), blocks_for(4.0));
    let mut pressure = Vec::new();
    let x = run(
        &mut e,
        blocks_for(5.0),
        |b| {
            let trem = |enabled| Command::Tremulant {
                chest,
                enabled,
                rate_hz: 5.5,
                depth_fraction: depth,
            };
            match b {
                0 => vec![Command::NoteOn { division, note: 60 }],
                b if b == on => vec![trem(true)],
                b if b == off => vec![trem(false)],
                _ => vec![],
            }
        },
        |_, e| {
            for fr in (0..BLOCK).step_by(32) {
                pressure.push(f64::from(e.wind().pressure_at(chest, fr)));
            }
        },
    );

    for (i, (ours, theirs)) in pressure.iter().zip(floats(&t["pressurePa"])).enumerate() {
        assert!(
            (ours - theirs).abs() < 0.001,
            "pressure sample {i}: {ours} vs {theirs}"
        );
    }

    let env = envelope(&x, (SR * 0.005) as usize);
    let win = (SR * 0.005) as usize;
    let shaken = ((on * BLOCK + SR as usize / 2) / win)..((on * BLOCK + (SR * 2.5) as usize) / win);
    let swing = |r: std::ops::Range<usize>| {
        let (lo, hi) = r.fold((f64::MAX, 0.0f64), |(lo, hi), w| {
            (lo.min(env[w]), hi.max(env[w]))
        });
        db(hi) - db(lo)
    };
    let s = swing(shaken);
    assert!(
        (s - f(&t["envelopeSwingDb"])).abs() < 0.02,
        "envelope swing {s} dB vs {}",
        f(&t["envelopeSwingDb"])
    );
    let plain = swing((SR as usize / 2) / win..(on * BLOCK) / win);
    assert!(
        (plain - f(&t["plainEnvelopeSwingDb"])).abs() < 0.02,
        "plain swing {plain} vs {}",
        f(&t["plainEnvelopeSwingDb"])
    );
}
