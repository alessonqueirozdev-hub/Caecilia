//! Several cores, the same sound.

use std::time::Duration;

use caecilia_engine::{Command, OrganEngine, PoolConfig, Timed};
use caecilia_model::{Calibration, CouplerId, DivisionId, StopId, WindchestId, demo_organ};

/// A tutti with couplers, a chord moving over it, the tremulant and the swell,
/// rendered under `threads`, in uneven blocks.
fn render(threads: PoolConfig) -> (Vec<f32>, u64) {
    let organ = demo_organ();
    let mut e = OrganEngine::with_config(&organ, 48000.0, 512, 512, Calibration::V2, threads);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let couplers: Vec<CouplerId> = organ.couplers().iter().map(|c| c.id).collect();
    let t = e.table_for(&organ, &all, &couplers);
    e.set_engaged(t);

    let mut out = Vec::new();
    let mut frame = 0usize;
    for (b, frames) in [512usize, 100, 37, 512, 256, 64, 1, 300]
        .iter()
        .cycle()
        .take(500)
        .enumerate()
    {
        let mut events = Vec::new();
        if b == 0 {
            for (i, note) in [36u8, 48, 55, 60, 64, 67, 72].into_iter().enumerate() {
                events.push(Timed {
                    offset: (i * 7) as u32 % *frames as u32,
                    command: Command::NoteOn {
                        division: DivisionId(1),
                        note,
                    },
                });
            }
            events.push(Timed {
                offset: 0,
                command: Command::Tremulant {
                    chest: WindchestId(2),
                    enabled: true,
                    rate_hz: 5.5,
                    depth_fraction: 0.15,
                },
            });
        }
        if b == 40 {
            events.push(Timed {
                offset: 0,
                command: Command::NoteOff {
                    division: DivisionId(1),
                    note: 64,
                },
            });
            events.push(Timed {
                offset: 0,
                command: Command::NoteOn {
                    division: DivisionId(2),
                    note: 65,
                },
            });
            events.push(Timed {
                offset: 0,
                command: Command::Expression {
                    division: DivisionId(2),
                    position: 0.3,
                },
            });
        }
        let (mut l, mut r) = (vec![0.0f32; *frames], vec![0.0f32; *frames]);
        e.process(&mut l, &mut r, &events);
        for k in 0..*frames {
            out.push(l[k]);
            out.push(r[k]);
        }
        frame += frames;
    }
    assert!(frame > 48000 * 2, "{frame} frames");
    (out, e.missed_jobs())
}

#[test]
fn three_workers_render_bit_for_bit_what_one_thread_does() {
    let (alone, _) = render(PoolConfig::offline());
    // Room to wait: this checks the arithmetic, not the scheduling of a busy
    // test machine.
    let (pooled, missed) = render(
        PoolConfig::with_workers(3)
            .real_time(false)
            .wait_limit(Duration::from_secs(10)),
    );
    assert_eq!(missed, 0, "no job may miss when the pool can wait");
    assert!(alone.iter().any(|x| *x != 0.0));
    let first = alone
        .iter()
        .zip(&pooled)
        .position(|(a, b)| a.to_bits() != b.to_bits());
    assert_eq!(first, None, "threads changed the output");
}
