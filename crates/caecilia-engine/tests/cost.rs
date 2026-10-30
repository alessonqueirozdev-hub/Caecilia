//! What a tutti costs, per calibration. Ignored by default (timing is not a
//! pass/fail fact on a shared machine); run with
//! `cargo test --profile caecilia-release -p caecilia-engine --test cost -- --ignored --nocapture`.

use std::time::Instant;

use caecilia_engine::{Command, OrganEngine, PoolConfig, Timed};
use caecilia_model::{Calibration, CouplerId, DivisionId, StopId, demo_organ};

#[test]
#[ignore]
fn tutti_real_time_factor() {
    let organ = demo_organ();
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let couplers: Vec<CouplerId> = organ.couplers().iter().map(|c| c.id).collect();
    let keys = [36u8, 43, 48, 52, 55, 60, 64, 67, 72, 76];
    let cores = std::thread::available_parallelism().map_or(1, usize::from);
    for (cal, threads, label) in [
        (Calibration::OneX, PoolConfig::offline(), "one thread"),
        (Calibration::V2, PoolConfig::offline(), "one thread"),
        (Calibration::V2, PoolConfig::all_cores(), "all cores"),
    ] {
        let mut best = f64::MAX;
        for _ in 0..3 {
            let mut e = OrganEngine::with_config(&organ, 48000.0, 256, 1024, cal, threads);
            let t = e.table_for(&organ, &all, &couplers);
            e.set_engaged(t);
            let on: Vec<Timed> = keys
                .iter()
                .map(|&note| Timed {
                    offset: 0,
                    command: Command::NoteOn {
                        division: DivisionId(1),
                        note,
                    },
                })
                .collect();
            let (mut l, mut r) = (vec![0.0; 256], vec![0.0; 256]);
            e.process(&mut l, &mut r, &on);
            for _ in 0..40 {
                e.process(&mut l, &mut r, &[]);
            }
            let blocks = 750; // 4 s
            let start = Instant::now();
            for _ in 0..blocks {
                e.process(&mut l, &mut r, &[]);
            }
            let rtf = start.elapsed().as_secs_f64() / (blocks as f64 * 256.0 / 48000.0);
            best = best.min(rtf);
            // Twelve Grand-Orgue ranks and the Récit's eight through the coupler, ten keys.
            assert_eq!(e.active_voices(), 200);
        }
        eprintln!(
            "{cal:?}, {label} ({cores} cores): tutti, 10 keys, couplers drawn: {:.3} of the block's time",
            best
        );
    }
}
