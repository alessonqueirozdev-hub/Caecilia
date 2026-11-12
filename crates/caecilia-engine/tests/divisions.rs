// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! `process_divisions`: the mix `process` makes, and beside it a bus per
//! division of the organ.

use caecilia_engine::{Command, DivisionBus, MAX_DIVISIONS, OrganEngine, Timed};
use caecilia_model::{DivisionId, Organ, StopId, caecilia_organ, demo_organ, read_organ};

const SR: f64 = 48000.0;
const BLOCK: usize = 256;

/// A bus per division slot the engine can write.
struct Buses {
    left: Vec<Vec<f32>>,
    right: Vec<Vec<f32>>,
}

impl Buses {
    fn new(frames: usize) -> Self {
        Self {
            left: vec![vec![0.0; frames]; MAX_DIVISIONS],
            right: vec![vec![0.0; frames]; MAX_DIVISIONS],
        }
    }

    /// Every division given a bus.
    fn all(&mut self) -> Vec<DivisionBus<'_>> {
        self.left
            .iter_mut()
            .zip(&mut self.right)
            .map(|(l, r)| Some((l.as_mut_slice(), r.as_mut_slice())))
            .collect()
    }

    /// As [`all`](Self::all), with `except` not given.
    fn without(&mut self, except: usize) -> Vec<DivisionBus<'_>> {
        let mut buses = self.all();
        buses[except] = None;
        buses
    }

    fn peak(&self, division: usize) -> f32 {
        self.left[division]
            .iter()
            .chain(&self.right[division])
            .fold(0.0f32, |peak, s| peak.max(s.abs()))
    }

    /// What a host that sums the buses hears.
    fn sum(&self, frames: usize) -> (Vec<f32>, Vec<f32>) {
        let (mut l, mut r) = (vec![0.0; frames], vec![0.0; frames]);
        for d in 0..MAX_DIVISIONS {
            for (o, x) in l.iter_mut().zip(&self.left[d][..frames]) {
                *o += x;
            }
            for (o, x) in r.iter_mut().zip(&self.right[d][..frames]) {
                *o += x;
            }
        }
        (l, r)
    }
}

fn on(division: DivisionId, note: u8) -> Timed {
    Timed {
        offset: 0,
        command: Command::NoteOn { division, note },
    }
}

fn stops_of(organ: &Organ, division: DivisionId) -> Vec<StopId> {
    organ
        .stops()
        .iter()
        .filter(|s| s.division == division)
        .map(|s| s.id)
        .collect()
}

/// The largest gap between the buses' sum and the main output.
fn gap(a: &[f32], b: &[f32]) -> f32 {
    a.iter()
        .zip(b)
        .map(|(x, y)| (x - y).abs())
        .fold(0.0, f32::max)
}

#[test]
fn each_division_sounds_on_a_bus_of_its_own() {
    let organ = demo_organ();
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let (go, recit) = (DivisionId(1), DivisionId(2));
    e.set_engaged(e.table_for(&organ, &stops_of(&organ, go), &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    e.process_divisions(&mut l, &mut r, &[on(go, 60)], &mut buses.all());
    assert!(buses.peak(1) > 0.0, "the Grand-Orgue is heard");
    assert_eq!(buses.peak(0), 0.0, "nothing drawn on the Pédale");
    assert_eq!(buses.peak(2), 0.0, "nothing drawn on the Récit");

    // And the Récit, enclosed, on its own.
    e.reset();
    e.set_engaged(e.table_for(&organ, &stops_of(&organ, recit), &[]));
    e.process_divisions(&mut l, &mut r, &[on(recit, 60)], &mut buses.all());
    assert!(buses.peak(2) > 0.0, "the Récit is heard");
    assert_eq!(buses.peak(0), 0.0);
    assert_eq!(buses.peak(1), 0.0);
}

#[test]
fn a_bus_not_given_is_not_rendered_and_the_division_sounds_on_main() {
    let organ = demo_organ();
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let go = DivisionId(1);
    e.set_engaged(e.table_for(&organ, &stops_of(&organ, go), &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    // Fill with a sentinel: a bus that is not given must not be touched.
    for (l, r) in buses.left.iter_mut().zip(&mut buses.right) {
        l.fill(9.0);
        r.fill(9.0);
    }
    e.process_divisions(&mut l, &mut r, &[on(go, 60)], &mut buses.without(1));
    assert_eq!(buses.peak(1), 9.0, "untouched where nobody listens");
    assert!(l.iter().any(|s| *s != 0.0), "the Grand-Orgue on main");
}

#[test]
fn the_buses_sum_to_the_main_output() {
    let organ = demo_organ();
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    e.set_engaged(e.table_for(&organ, &all, &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    // A chord on every division, some events inside the block so the render
    // falls in several slices.
    let events = [
        on(DivisionId(0), 48),
        Timed {
            offset: 64,
            command: Command::NoteOn {
                division: DivisionId(1),
                note: 60,
            },
        },
        Timed {
            offset: 200,
            command: Command::NoteOn {
                division: DivisionId(2),
                note: 67,
            },
        },
    ];
    e.process_divisions(&mut l, &mut r, &events, &mut buses.all());
    for _ in 0..3 {
        e.process_divisions(&mut l, &mut r, &[], &mut buses.all());
    }
    let (sl, sr) = buses.sum(BLOCK);
    // Every chest answers to one division, so the sum is the main output,
    // bit for bit.
    assert_eq!(sl, l);
    assert_eq!(sr, r);
}

#[test]
fn the_main_output_is_the_same_whether_or_not_buses_are_written() {
    let organ = demo_organ();
    let mut plain = OrganEngine::new(&organ, SR, BLOCK, 256);
    let mut routed = OrganEngine::new(&organ, SR, BLOCK, 256);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    for e in [&mut plain, &mut routed] {
        e.set_engaged(e.table_for(&organ, &all, &[]));
    }

    let events = [on(DivisionId(1), 60), on(DivisionId(2), 64)];
    let (mut al, mut ar) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let (mut bl, mut br) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    plain.process(&mut al, &mut ar, &events);
    routed.process_divisions(&mut bl, &mut br, &events, &mut buses.all());
    assert_eq!(al, bl, "buses written");
    assert_eq!(ar, br);

    // And with every bus switched off.
    let mut off: [DivisionBus<'_>; MAX_DIVISIONS] = std::array::from_fn(|_| None);
    routed.process_divisions(&mut bl, &mut br, &events, &mut off);
    plain.process(&mut al, &mut ar, &events);
    assert_eq!(al, bl, "buses off");
    assert_eq!(ar, br);
}

/// One chest two divisions draw from: it sounds on the division of the
/// first rank standing on it, and the sum still equals the main output.
const SHARED: &str = r#"{
  "name": "Shared chest organ",
  "windchests": [ { "name": "Main", "pressurePa": 700 } ],
  "ranks": [
    { "name": "Diapason 8", "family": "Principal", "footage": 8, "windchest": "Main" },
    { "name": "Flute 4", "family": "Flute", "footage": 4, "windchest": "Main" }
  ],
  "divisions": [
    { "name": "Great", "midiChannel": 0, "manual": 1 },
    { "name": "Solo", "midiChannel": 1, "manual": 2 }
  ],
  "stops": [
    { "name": "Diapason 8", "division": "Great", "rank": "Diapason 8" },
    { "name": "Flute 4", "division": "Solo", "rank": "Flute 4" }
  ]
}"#;

#[test]
fn a_chest_shared_by_two_divisions_sounds_on_one_bus() {
    let organ = read_organ(SHARED, None).expect("the organ loads").organ;
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let (great, solo) = (DivisionId(0), DivisionId(1));
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    e.set_engaged(e.table_for(&organ, &all, &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    e.process_divisions(&mut l, &mut r, &[on(solo, 60)], &mut buses.all());
    assert!(
        buses.peak(0) > 0.0,
        "the shared chest sounds for the Great, whose first rank stands on it"
    );
    assert_eq!(buses.peak(1), 0.0);
    e.process_divisions(&mut l, &mut r, &[on(great, 60)], &mut buses.all());
    assert!(buses.peak(0) > 0.0);
    assert_eq!(buses.peak(1), 0.0);

    let (sl, sr) = buses.sum(BLOCK);
    assert_eq!(sl, l);
    assert_eq!(sr, r);
}

#[test]
fn divisions_past_the_slots_fold_into_the_last_bus() {
    // Seventeen divisions on the engine's sixteen chests: divisions 15 and
    // 16 share the last chest, and division 16's rank stands first on it,
    // so the chest answers to a division the buses have no slot for. Its
    // sound is folded into the last bus rather than lost from the sum.
    let mut b = caecilia_model::OrganBuilder::new("Seventeen", "test", 2026);
    let chests: Vec<_> = (0..MAX_DIVISIONS)
        .map(|i| b.chest(&format!("chest {i}"), 700.0, false))
        .collect();
    let mut divisions = Vec::new();
    for i in 0..=MAX_DIVISIONS {
        divisions.push(b.division(
            &format!("division {i}"),
            caecilia_model::DivisionKind::Manual,
            36,
            96,
            false,
            false,
            chests[i.min(MAX_DIVISIONS - 1)],
        ));
    }
    let spec = || {
        caecilia_model::StopSpec::new(
            "Diapason 8",
            caecilia_model::TonalFamily::Principal,
            caecilia_model::Footage::EIGHT,
            caecilia_model::PitchClass::Unison,
            caecilia_model::ChorusRole::Foundation,
        )
    };
    // Division 16's rank first: it is the first rank standing on the last
    // chest, so the chest answers to division 16.
    b.stop(divisions[MAX_DIVISIONS], spec());
    for &d in divisions.iter().take(MAX_DIVISIONS) {
        b.stop(d, spec());
    }
    let organ = b.build();
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    e.set_engaged(e.table_for(&organ, &all, &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    let last = DivisionId(MAX_DIVISIONS as u16);
    e.process_divisions(&mut l, &mut r, &[on(last, 60)], &mut buses.all());
    assert!(
        buses.peak(MAX_DIVISIONS - 1) > 0.0,
        "the seventeenth division is folded into the last bus"
    );
    let (sl, sr) = buses.sum(BLOCK);
    // Two divisions under one bus: the sum is still the main output
    // within the reassociation's rounding.
    assert!(gap(&sl, &l) < 1e-6 && gap(&sr, &r) < 1e-6);
}

#[test]
fn an_organ_with_fewer_divisions_leaves_the_rest_silent() {
    let organ = caecilia_organ();
    assert_eq!(organ.divisions().len(), 3);
    let mut e = OrganEngine::new(&organ, SR, BLOCK, 256);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    e.set_engaged(e.table_for(&organ, &all, &[]));

    let (mut l, mut r) = (vec![0.0; BLOCK], vec![0.0; BLOCK]);
    let mut buses = Buses::new(BLOCK);
    e.process_divisions(&mut l, &mut r, &[on(DivisionId(1), 60)], &mut buses.all());
    for d in 3..MAX_DIVISIONS {
        assert_eq!(buses.peak(d), 0.0, "bus {d} past the organ's divisions");
    }
}
