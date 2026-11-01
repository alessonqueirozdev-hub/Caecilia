// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Proves the engine never touches the heap while it plays.
//!
//! The global allocator is replaced by one that counts, and the test fails if
//! anything allocates, reallocates or frees while the engine processes -- a
//! full tutti landing, keys stolen out of a full pool, a registration changing
//! under held notes, couplers, sustain, the swell box and the tremulant.

#![allow(unsafe_code)]

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;

use caecilia_engine::{Command, OrganEngine, Timed};
use caecilia_model::{CouplerId, DivisionId, StopId, WindchestId, demo_organ};

thread_local! {
    static WATCHING: Cell<bool> = const { Cell::new(false) };
    static HEAP_CALLS: Cell<usize> = const { Cell::new(0) };
}

struct CountingAllocator;

impl CountingAllocator {
    fn record() {
        let _ = WATCHING.try_with(|w| {
            if w.get() {
                let _ = HEAP_CALLS.try_with(|c| c.set(c.get() + 1));
            }
        });
    }
}

// SAFETY: every method forwards to the system allocator unchanged; the only
// addition is bumping a thread-local counter, which does not allocate.
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        Self::record();
        // SAFETY: the caller upholds `GlobalAlloc::alloc`'s contract.
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        Self::record();
        // SAFETY: the caller upholds `GlobalAlloc::dealloc`'s contract.
        unsafe { System.dealloc(pointer, layout) }
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        Self::record();
        // SAFETY: the caller upholds `GlobalAlloc::realloc`'s contract.
        unsafe { System.realloc(pointer, layout, new_size) }
    }
}

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

fn assert_no_heap_use(what: &str, work: impl FnOnce()) {
    HEAP_CALLS.with(|c| c.set(0));
    WATCHING.with(|w| w.set(true));
    work();
    WATCHING.with(|w| w.set(false));
    let calls = HEAP_CALLS.with(Cell::get);
    assert_eq!(
        calls, 0,
        "{what} used the heap {calls} time(s) while processing"
    );
}

#[test]
fn the_guard_itself_catches_an_allocation() {
    let caught = std::panic::catch_unwind(|| {
        assert_no_heap_use("a Vec", || drop(std::hint::black_box(vec![1u8; 64])));
    });
    assert!(caught.is_err());
}

#[test]
fn playing_the_organ_never_allocates() {
    let organ = demo_organ();
    // A small pool, so a tutti chord has to steal.
    let mut e = OrganEngine::new(&organ, 48000.0, 256, 96);
    let all: Vec<StopId> = organ.stops().iter().map(|s| s.id).collect();
    let couplers: Vec<CouplerId> = organ.couplers().iter().map(|c| c.id).collect();
    let tutti = e.table_for(&organ, &all, &couplers);
    let plenum = e.table_for(
        &organ,
        &organ.default_opening_registration(organ.primary_manual()),
        &[],
    );

    let go = DivisionId(1);
    let recit = DivisionId(2);
    let chord: Vec<Timed> = [36u8, 48, 55, 60, 64, 67, 72, 76]
        .iter()
        .enumerate()
        .map(|(i, &note)| Timed {
            offset: (i * 17) as u32,
            command: Command::NoteOn { division: go, note },
        })
        .chain([
            Timed {
                offset: 5,
                command: Command::NoteOn {
                    division: recit,
                    note: 62,
                },
            },
            Timed {
                offset: 9,
                command: Command::Sustain {
                    division: go,
                    down: true,
                },
            },
            Timed {
                offset: 30,
                command: Command::Expression {
                    division: recit,
                    position: 0.2,
                },
            },
            Timed {
                offset: 40,
                command: Command::Tremulant {
                    chest: WindchestId(2),
                    enabled: true,
                    rate_hz: 5.5,
                    depth_fraction: 0.15,
                },
            },
        ])
        .collect();
    let release: Vec<Timed> = [36u8, 48, 55, 60]
        .iter()
        .map(|&note| Timed {
            offset: 3,
            command: Command::NoteOff { division: go, note },
        })
        .chain([Timed {
            offset: 100,
            command: Command::Sustain {
                division: go,
                down: false,
            },
        }])
        .collect();
    let mut l = vec![0.0f32; 256];
    let mut r = vec![0.0f32; 256];

    assert_no_heap_use("the engine", || {
        e.set_engaged(plenum);
        for (i, frames) in [256usize, 1, 64, 255, 256, 256, 128, 256]
            .into_iter()
            .enumerate()
        {
            let events: &[Timed] = match i {
                0 => &chord,
                3 => &release,
                _ => &[],
            };
            e.process(&mut l[..frames], &mut r[..frames], events);
            if i == 2 {
                // Drawing everything under a held chord: re-voicing and stealing.
                e.set_engaged(tutti);
            }
        }
        e.process(
            &mut l,
            &mut r,
            &[Timed {
                offset: 0,
                command: Command::Panic,
            }],
        );
        e.reset();
        e.process(&mut l, &mut r, &[]);
    });
    assert!(l.iter().chain(&r).all(|s| s.is_finite()));
}
