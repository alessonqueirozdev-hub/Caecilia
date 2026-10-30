//! Proves the plugin's process never touches the heap.

#![allow(unsafe_code)]

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;

use caecilia_plugin::{Action, Binding, Caecilia, Source, coupler_param_id, stop_param_id};
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{Event, EventBuffer, EventKind, NoteRef};
use manguebyte_dsp::{ProcessContext, ProcessSpec, Transport};
use manguebyte_midi::MidiMessage;
use manguebyte_params::ParamSet;
use manguebyte_plugin::Plugin;

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

#[test]
fn processing_never_allocates() {
    let mut plugin = Caecilia::new();
    let spec = ProcessSpec {
        sample_rate: 48000.0,
        max_block_size: 256,
        num_channels: 2,
    };
    plugin.prepare(&spec);
    let params = plugin.params();
    let id_of = |id: &str| params.find(id).unwrap().param_id();

    let mut events = EventBuffer::with_capacity(512);
    let mut quiet = EventBuffer::with_capacity(8);
    for (i, key) in [36u8, 48, 55, 60, 64, 67, 72, 76].into_iter().enumerate() {
        let _ = events.push(Event {
            offset: i as u32 * 9,
            kind: EventKind::NoteOn {
                note: NoteRef::new(0, key),
                velocity: 0.9,
            },
        });
    }
    for n in 0..26 {
        let _ = events.push(Event {
            offset: 30,
            kind: EventKind::ParamValue {
                param: id_of(&stop_param_id(n)),
                value: 1.0,
            },
        });
    }
    for n in 0..3 {
        let _ = events.push(Event {
            offset: 31,
            kind: EventKind::ParamValue {
                param: id_of(&coupler_param_id(n)),
                value: 1.0,
            },
        });
    }
    let _ = events.push(Event {
        offset: 40,
        kind: EventKind::ParamValue {
            param: id_of("tremulant"),
            value: 1.0,
        },
    });
    let _ = events.push(Event {
        offset: 41,
        kind: EventKind::Midi(MidiMessage::ControlChange {
            channel: 1,
            controller: 11,
            value: 30,
        }),
    });
    let _ = events.push(Event {
        offset: 42,
        kind: EventKind::Midi(MidiMessage::ControlChange {
            channel: 0,
            controller: 64,
            value: 127,
        }),
    });
    let _ = quiet.push(Event {
        offset: 0,
        kind: EventKind::Midi(MidiMessage::ControlChange {
            channel: 0,
            controller: 123,
            value: 0,
        }),
    });

    // The console in the watched blocks: a program change recalls a general,
    // a learned controller toggles a stop, and an armed learn captures one.
    params.console.bind(Binding {
        source: Source::Controller {
            channel: 0,
            number: 80,
        },
        action: Action::ToggleStop(3),
    });
    params.console.arm(Action::General(2));
    for (offset, m) in [
        (
            50,
            MidiMessage::ProgramChange {
                channel: 0,
                program: 4,
            },
        ),
        (
            51,
            MidiMessage::ControlChange {
                channel: 0,
                controller: 80,
                value: 127,
            },
        ),
        (
            52,
            MidiMessage::ControlChange {
                channel: 0,
                controller: 81,
                value: 127,
            },
        ),
    ] {
        let _ = events.push(Event {
            offset,
            kind: EventKind::Midi(m),
        });
    }

    let transport = Transport::default();
    let mut buffer = AudioBuffer::<f32>::new(2, 256);
    let run = |plugin: &mut Caecilia, buffer: &mut AudioBuffer<f32>, ev: &EventBuffer| {
        let context = ProcessContext::new(ev, &transport);
        plugin.process(buffer, &context);
    };
    let empty = EventBuffer::with_capacity(1);
    // One block outside the watch, so nothing lazily initialised is counted.
    run(&mut plugin, &mut buffer, &empty);

    HEAP_CALLS.with(|c| c.set(0));
    WATCHING.with(|w| w.set(true));
    run(&mut plugin, &mut buffer, &events);
    for frames in [1usize, 64, 255, 256, 256] {
        buffer.set_num_frames(frames);
        run(&mut plugin, &mut buffer, &empty);
    }
    buffer.set_num_frames(256);
    run(&mut plugin, &mut buffer, &quiet);
    plugin.reset();
    run(&mut plugin, &mut buffer, &empty);
    WATCHING.with(|w| w.set(false));
    let calls = HEAP_CALLS.with(Cell::get);
    assert_eq!(calls, 0, "process used the heap {calls} time(s)");
    assert!(
        params.console.armed().is_none(),
        "the armed learn was captured"
    );
    assert_eq!(params.console.bindings().len(), 2);
}
