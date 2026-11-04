// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Every organ the instrument carries plays, on every stop.

use caecilia_engine::{Command, OrganEngine, PoolConfig, Timed};
use caecilia_model::{BUILT_IN_ORGANS, Calibration, DivisionKind, read_organ, write_organ};

#[test]
fn every_stop_of_every_organ_speaks() {
    for (name, make) in BUILT_IN_ORGANS {
        let organ = make();
        assert_eq!(organ.name, name);
        assert!(organ.stops().len() <= 64, "{name}");
        let mut e = OrganEngine::with_space(
            &organ,
            48000.0,
            256,
            512,
            Calibration::V2,
            PoolConfig::offline(),
        );
        for stop in organ.stops() {
            let t = e.table_for(&organ, &[stop.id], &[]);
            e.set_engaged(t);
            let on = |on: bool| Timed {
                offset: 0,
                command: if on {
                    Command::NoteOn {
                        division: stop.division,
                        note: 48,
                    }
                } else {
                    Command::NoteOff {
                        division: stop.division,
                        note: 48,
                    }
                },
            };
            let (mut l, mut r) = (vec![0.0f32; 256], vec![0.0f32; 256]);
            let mut peak = 0.0f32;
            e.process(&mut l, &mut r, &[on(true)]);
            for _ in 0..60 {
                e.process(&mut l, &mut r, &[]);
                peak = l.iter().chain(&r).fold(peak, |p, v| p.max(v.abs()));
                assert!(l.iter().chain(&r).all(|v| v.is_finite()));
            }
            assert!(peak > 1e-3, "{name}: {} is silent: {peak}", stop.name);
            e.process(&mut l, &mut r, &[on(false)]);
            e.reset();
        }
    }
}

#[test]
fn every_organ_has_manuals_and_a_pedal_and_survives_the_file_format() {
    for (name, make) in BUILT_IN_ORGANS {
        let organ = make();
        let has = |kind: DivisionKind| organ.divisions().iter().any(|d| d.kind == kind);
        assert!(
            has(DivisionKind::Manual) && has(DivisionKind::Pedal),
            "{name}"
        );
        let back = read_organ(&write_organ(&organ), None)
            .expect("reads back")
            .organ;
        assert_eq!(back.stops().len(), organ.stops().len(), "{name}");
    }
}
