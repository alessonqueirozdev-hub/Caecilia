// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! What the engine does with keys, stops, couplers, pedals and boxes.

use caecilia_engine::{Command, OrganEngine, Timed};
use caecilia_model::{CouplerId, DivisionId, Organ, StopId, demo_organ};

const GO: DivisionId = DivisionId(1);
const RECIT: DivisionId = DivisionId(2);

fn setup(polyphony: usize) -> (Organ, OrganEngine) {
    let organ = demo_organ();
    let e = OrganEngine::new(&organ, 48000.0, 256, polyphony);
    (organ, e)
}

fn stops_of(organ: &Organ, division: DivisionId) -> Vec<StopId> {
    organ
        .stops()
        .iter()
        .filter(|s| s.division == division)
        .map(|s| s.id)
        .collect()
}

fn block(e: &mut OrganEngine, commands: &[Command]) -> f64 {
    let (mut l, mut r) = (vec![0.0; 256], vec![0.0; 256]);
    let events: Vec<Timed> = commands
        .iter()
        .map(|&command| Timed { offset: 0, command })
        .collect();
    e.process(&mut l, &mut r, &events);
    (l.iter()
        .chain(&r)
        .map(|s| f64::from(*s).powi(2))
        .sum::<f64>()
        / 512.0)
        .sqrt()
}

fn on(division: DivisionId, note: u8) -> Command {
    Command::NoteOn { division, note }
}

fn off(division: DivisionId, note: u8) -> Command {
    Command::NoteOff { division, note }
}

#[test]
fn nothing_drawn_is_silence() {
    let (_, mut e) = setup(64);
    assert_eq!(block(&mut e, &[on(GO, 60)]), 0.0);
    assert_eq!(e.active_voices(), 0);
}

#[test]
fn a_key_sounds_one_voice_per_drawn_rank_and_couplers_lend_theirs() {
    let (organ, mut e) = setup(256);
    let go = stops_of(&organ, GO);
    let recit = stops_of(&organ, RECIT);
    let mut all = go.clone();
    all.extend(&recit);
    let t = e.table_for(&organ, &all, &[]);
    e.set_engaged(t);
    block(&mut e, &[on(GO, 60)]);
    assert_eq!(e.active_voices(), go.len());

    // Récit/Grand-Orgue: the Grand-Orgue key now sounds the Récit's ranks too.
    let t = e.table_for(&organ, &all, &[CouplerId(0)]);
    e.set_engaged(t);
    block(&mut e, &[]);
    assert_eq!(
        e.active_voices(),
        go.len() + recit.len(),
        "drawing a coupler under a held key"
    );

    // The same pipe reached from the Récit's own key is not started twice, and
    // letting one hand go leaves what the other still holds.
    block(&mut e, &[on(RECIT, 60)]);
    assert_eq!(e.active_voices(), go.len() + recit.len());
    block(&mut e, &[off(GO, 60)]);
    for _ in 0..200 {
        block(&mut e, &[]);
    }
    assert_eq!(
        e.active_voices(),
        recit.len(),
        "the Récit key still holds its pipes"
    );
}

#[test]
fn a_registration_change_under_held_keys_only_touches_what_changed() {
    let (organ, mut e) = setup(256);
    let go = stops_of(&organ, GO);
    let t = e.table_for(&organ, &go[..3], &[]);
    e.set_engaged(t);
    block(&mut e, &[on(GO, 60), on(GO, 64)]);
    assert_eq!(e.active_voices(), 6);

    // Draw two more, retire one.
    let t = e.table_for(&organ, &[go[0], go[1], go[3], go[4]], &[]);
    e.set_engaged(t);
    block(&mut e, &[]);
    assert_eq!(
        e.active_voices(),
        10,
        "the retired rank releases as a pipe does"
    );
    for _ in 0..200 {
        block(&mut e, &[]);
    }
    assert_eq!(e.active_voices(), 8);
}

#[test]
fn the_sustain_pedal_holds_what_it_catches() {
    let (organ, mut e) = setup(64);
    let t = e.table_for(&organ, &stops_of(&organ, GO)[..1], &[]);
    e.set_engaged(t);
    block(
        &mut e,
        &[
            on(GO, 60),
            Command::Sustain {
                division: GO,
                down: true,
            },
            off(GO, 60),
        ],
    );
    for _ in 0..200 {
        block(&mut e, &[]);
    }
    assert_eq!(e.active_voices(), 1, "held by the pedal");
    block(
        &mut e,
        &[Command::Sustain {
            division: GO,
            down: false,
        }],
    );
    for _ in 0..200 {
        block(&mut e, &[]);
    }
    assert_eq!(e.active_voices(), 0);
}

#[test]
fn a_full_pool_takes_a_whole_key_or_none_of_it() {
    let (organ, mut e) = setup(20);
    let go = stops_of(&organ, GO);
    let t = e.table_for(&organ, &go[..6], &[]);
    e.set_engaged(t);
    block(&mut e, &[on(GO, 48), on(GO, 52), on(GO, 55)]);
    assert_eq!(e.active_voices(), 18);
    // A fourth key needs six and there are two: a whole earlier key is stolen.
    block(&mut e, &[on(GO, 60)]);
    assert_eq!(e.active_voices(), 18);
    assert_eq!(
        e.active_voices() % 6,
        0,
        "no key is left with a hole in its registration"
    );
}

#[test]
fn a_shut_box_quietens_and_dulls_only_its_own_division() {
    let level = |position: Option<f32>, division: DivisionId| {
        let (organ, mut e) = setup(64);
        let t = e.table_for(&organ, &stops_of(&organ, division)[..1], &[]);
        e.set_engaged(t);
        let mut c = vec![on(division, 60)];
        if let Some(p) = position {
            c.push(Command::Expression {
                division,
                position: p,
            });
        }
        block(&mut e, &c);
        (0..150).map(|_| block(&mut e, &[])).last().unwrap()
    };
    let open = level(None, RECIT);
    let shut = level(Some(0.0), RECIT);
    let drop_db = 20.0 * (shut / open).log10();
    assert!(drop_db < -13.0, "Récit shut: {drop_db:.1} dB");
    // The Grand-Orgue is not enclosed: its shoe moves the voices' gain but has no
    // box to close, so only the flat half applies.
    let go_open = level(None, GO);
    let go_shut = level(Some(0.0), GO);
    let go_drop = 20.0 * (go_shut / go_open).log10();
    assert!(
        (go_drop + 13.0).abs() < 0.2,
        "Grand-Orgue shoe: {go_drop:.1} dB"
    );
}
