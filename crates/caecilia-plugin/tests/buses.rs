// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Each division of the organ on an output bus of its own, as a host
//! routes them: the buses by name, a bus switched off not rendered, the
//! buses' sum the main output, and organs of different sizes.
//!
//! Not under Miri, which is too slow for the wrappers.
#![cfg(not(miri))]
#![allow(unsafe_code)]

use std::sync::Mutex;

use caecilia_model::{built_in_organ, caecilia_organ};
use caecilia_plugin::{Caecilia, DIVISION_BUSES, set_organ_for_new_instances, stop_param_id};
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{Event, EventBuffer, EventKind, NoteRef};
use manguebyte_dsp::{ProcessContext, Transport};
use manguebyte_plugin::{Harness, Plugin};

/// The organ a new instance plays is a process-wide slot
/// ([`set_organ_for_new_instances`]): the tests take turns on it, each
/// choosing its organ before it builds.
static ORGAN_LOCK: Mutex<()> = Mutex::new(());

const SR: f64 = 48000.0;

/// "Division 01" for the organ's first division, and so on to 16.
const NAMES: [&str; 16] = [
    "Division 01",
    "Division 02",
    "Division 03",
    "Division 04",
    "Division 05",
    "Division 06",
    "Division 07",
    "Division 08",
    "Division 09",
    "Division 10",
    "Division 11",
    "Division 12",
    "Division 13",
    "Division 14",
    "Division 15",
    "Division 16",
];

fn harness() -> Harness<Caecilia> {
    Harness::<Caecilia>::new(SR, 256)
}

fn retire_all(h: &mut Harness<Caecilia>) {
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 0.0);
    }
}

fn note_on(h: &mut Harness<Caecilia>, channel: u8, key: u8) {
    h.schedule(
        h.position(),
        EventKind::NoteOn {
            note: NoteRef::new(channel, key),
            velocity: 0.8,
        },
    );
}

fn peak(b: &AudioBuffer<f32>) -> f32 {
    (0..b.num_channels())
        .flat_map(|c| b.channel(c).iter())
        .fold(0.0f32, |p, s| p.max(s.abs()))
}

/// The demonstration organ: Pédale on bus 0 (channel 2), Grand-Orgue on
/// bus 1 (channel 0), Récit on bus 2 (channel 1).
#[test]
fn each_division_is_heard_on_a_bus_of_its_own() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    for (channel, stop, bus, name) in [
        (2u8, 0usize, 0usize, "Pédale"),
        (0, 9, 1, "Grand-Orgue"),
        (1, 20, 2, "Récit"),
    ] {
        let mut h = harness();
        retire_all(&mut h);
        h.automate(0, &stop_param_id(stop), 1.0);
        note_on(&mut h, channel, 48);
        h.render(2400);
        for d in 0..16 {
            let p = peak(h.aux_output(d).expect("every bus is on"));
            if d == bus {
                assert!(p > 0.0, "the {name} is heard on bus {d}");
            } else {
                assert_eq!(p, 0.0, "the {name} is alone: bus {d} silent");
            }
        }
    }
}

#[test]
fn a_bus_switched_off_is_not_given_and_the_division_sounds_on_main() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut h = harness();
    retire_all(&mut h);
    h.automate(0, &stop_param_id(9), 1.0); // Grand-Orgue Bourdon 8
    h.set_aux_active(1, false);
    note_on(&mut h, 0, 60);
    let main = h.render(2400);
    assert!(h.aux_output(1).is_none(), "switched off, not given");
    assert!(h.aux_output(0).is_some());
    assert!(
        main.peak() > 0.0,
        "the Grand-Orgue sounds on the main output"
    );
}

#[test]
fn the_buses_sum_to_the_main_output() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut h = harness();
    retire_all(&mut h);
    for stop in [0usize, 9, 20] {
        h.automate(0, &stop_param_id(stop), 1.0);
    }
    // The master chain made transparent: no room tail, the EQ off so its
    // voicing is not voiced, and the level at unity, so the main output
    // is the engine's mix the buses are drawn from. The limiter, under
    // its ceiling, passes untouched.
    h.automate(0, "reverb", 0.0);
    h.automate(0, "eq", 0.0);
    note_on(&mut h, 0, 60);
    note_on(&mut h, 1, 64);
    note_on(&mut h, 2, 48);
    let main = h.render(4800);
    let (mut sum_l, mut sum_r) = (vec![0.0f32; 4800], vec![0.0f32; 4800]);
    for d in 0..16 {
        let bus = h.aux_output(d).unwrap();
        for (o, x) in sum_l.iter_mut().zip(bus.channel(0)) {
            *o += x;
        }
        for (o, x) in sum_r.iter_mut().zip(bus.channel(1)) {
            *o += x;
        }
    }
    let gap = |a: &[f32], b: &[f32]| {
        a.iter()
            .zip(b)
            .map(|(x, y)| (x - y).abs())
            .fold(0.0f32, f32::max)
    };
    let (gl, gr) = (gap(&sum_l, main.channel(0)), gap(&sum_r, main.channel(1)));
    // Three chests, a division each: the sum is the same arithmetic the
    // main output does. The gap stays at what was measured, 0 here.
    assert!(
        gl <= 1e-6 && gr <= 1e-6,
        "the buses' sum misses by {gl}/{gr}"
    );
}

#[test]
fn the_main_output_is_the_same_with_the_buses_off_or_on() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut on = harness();
    let mut off = harness();
    for d in 0..16 {
        off.set_aux_active(d, false);
    }
    for h in [&mut on, &mut off] {
        note_on(h, 0, 60);
        note_on(h, 1, 64);
    }
    let a = on.render(4800);
    let b = off.render(4800);
    assert_eq!(a.channel(0), b.channel(0));
    assert_eq!(a.channel(1), b.channel(1));
}

#[test]
fn organs_with_different_division_counts_work() {
    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    // The Chapel Organ: a manual and a pedal, channels 0 and 1.
    set_organ_for_new_instances(built_in_organ("Chapel Organ").unwrap());
    let mut h = harness();
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 1.0);
    }
    note_on(&mut h, 0, 60);
    note_on(&mut h, 1, 48);
    h.render(2400);
    for d in 0..2 {
        assert!(
            peak(h.aux_output(d).unwrap()) > 0.0,
            "the Chapel Organ's division {d} is heard"
        );
    }
    for d in 2..16 {
        assert_eq!(peak(h.aux_output(d).unwrap()), 0.0, "bus {d}");
    }

    // The French Romantic organ: four manuals and a pedal, channels 0..4.
    set_organ_for_new_instances(built_in_organ("French Romantic Organ").unwrap());
    let mut h = harness();
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 1.0);
    }
    for channel in 0..4u8 {
        note_on(&mut h, channel, 48);
    }
    h.render(2400);
    for d in 0..4 {
        assert!(
            peak(h.aux_output(d).unwrap()) > 0.0,
            "the French organ's division {d} is heard"
        );
    }
    for d in 4..16 {
        assert_eq!(peak(h.aux_output(d).unwrap()), 0.0, "bus {d}");
    }
}

/// How the hosted checks drive a processor: one block, main output and
/// the sixteen buses.
type Run<'a> =
    &'a mut dyn FnMut(&mut AudioBuffer<f32>, &ProcessContext<'_, f32>, &mut [AudioBuffer<f32>]);

/// A Grand-Orgue key through `run`: the demonstration organ's division 1
/// sounds on bus 1, and nowhere else.
fn check_a_division_on_its_bus(run: Run<'_>, muted: usize) {
    let mut events = EventBuffer::with_capacity(1);
    events
        .push(Event {
            offset: 0,
            kind: EventKind::NoteOn {
                note: NoteRef::new(0, 60),
                velocity: 0.8,
            },
        })
        .unwrap();
    let transport = Transport::STOPPED;
    let context = ProcessContext::new(&events, &transport);
    let mut main = AudioBuffer::<f32>::new(2, 2400);
    let mut aux: Vec<AudioBuffer<f32>> =
        (0..16).map(|_| AudioBuffer::<f32>::new(2, 2400)).collect();
    for _ in 0..10 {
        run(&mut main, &context, &mut aux);
    }
    assert!(main.peak() > 0.0, "the Grand-Orgue on the main output");
    for (d, bus) in aux.iter().enumerate() {
        let p = peak(bus);
        if d == 1 {
            assert_eq!(p > 0.0, muted != 1, "the Grand-Orgue's bus");
        } else {
            assert_eq!(p, 0.0, "bus {d}");
        }
    }
}

// --- CLAP ------------------------------------------------------------------------

#[test]
fn a_clap_host_sees_each_division_on_a_port_of_its_own() {
    use manguebyte_host::clap::ClapLibrary;

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    static ENTRY: manguebyte_clap::clap_sys::entry::clap_plugin_entry =
        manguebyte_clap::plugin_entry::<Caecilia>();
    // SAFETY: the entry point is the framework's own CLAP wrapper around
    // Caecilia, callable for as long as this test runs.
    let library = unsafe { ClapLibrary::from_entry(&raw const ENTRY, "caecilia") }.unwrap();
    let mut plugin = library.create(Caecilia::INFO.id).unwrap();
    let names: Vec<String> = plugin
        .audio_outputs()
        .iter()
        .map(|port| port.name.clone())
        .collect();
    let mut expected = vec!["Main".to_string()];
    expected.extend(NAMES.iter().map(|n| n.to_string()));
    assert_eq!(names, expected);
    for (i, port) in plugin.audio_outputs().iter().enumerate() {
        assert_eq!(port.channels, 2, "port {i} is stereo");
        assert_eq!(port.is_main, i == 0);
    }

    // The Grand-Orgue's bus switched off before activation, as a host does.
    plugin.set_audio_port_active(false, 2, false).unwrap();
    let mut processor = plugin.activate(SR, 256).unwrap();
    check_a_division_on_its_bus(
        &mut |main, context, aux| processor.process_with_aux(main, context, aux),
        1,
    );
    drop(processor);
    plugin.deactivate().unwrap();
    assert_eq!(plugin.failed_blocks(), 0);
}

// --- VST 3 ------------------------------------------------------------------------

#[test]
fn a_vst3_host_sees_each_division_on_a_bus_of_its_own() {
    use manguebyte_host::vst3::Vst3Module;

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    // SAFETY: the factory is the framework's own VST 3 wrapper around
    // Caecilia, callable for as long as this test runs.
    let module = unsafe {
        Vst3Module::from_factory(manguebyte_vst3::plugin_factory::<Caecilia>(), "caecilia")
    }
    .unwrap();
    let class = module.classes()[0].id;
    let mut plugin = module.create(class).unwrap();
    let names: Vec<String> = plugin
        .audio_outputs()
        .iter()
        .map(|bus| bus.name.clone())
        .collect();
    let mut expected = vec!["Output".to_string()];
    expected.extend(NAMES.iter().map(|n| n.to_string()));
    assert_eq!(names, expected);
    for (i, bus) in plugin.audio_outputs().iter().enumerate() {
        assert_eq!(bus.channels, 2, "bus {i} is stereo");
        assert_eq!(bus.is_main, i == 0);
    }

    // The Grand-Orgue's bus switched off before activation, as a host does.
    plugin.set_audio_output_active(2, false).unwrap();
    let mut processor = plugin.activate(SR, 256).unwrap();
    check_a_division_on_its_bus(
        &mut |main, context, aux| processor.process_with_aux(main, context, aux),
        1,
    );
    drop(processor);
    plugin.deactivate().unwrap();
    assert_eq!(plugin.failed_blocks(), 0);
}

#[test]
fn the_buses_are_named_in_division_order() {
    assert_eq!(DIVISION_BUSES.len(), NAMES.len());
    for (bus, name) in DIVISION_BUSES.iter().zip(NAMES) {
        assert_eq!(bus.name, name);
        assert_eq!(bus.channels, 2);
        assert!(bus.active_by_default);
    }
}

// --- An empty block ----------------------------------------------------------
//
// A block of no frames, which a host sends to deliver only events. The
// framework's own `Processor` skips it, so the test plays the host at the C
// boundary and calls the `process` entry a DAW calls, on a plugin made by
// the same entry point the `ClapLibrary` above opens. An empty block once
// panicked on `chunks_mut(0)` — and the panic, landing after the engine was
// taken out of its slot, left every later block silent.

mod empty_block {
    use std::ffi::{CStr, CString, c_char, c_void};
    use std::ptr;

    use manguebyte_clap::clap_sys::audio_buffer::clap_audio_buffer;
    use manguebyte_clap::clap_sys::entry::clap_plugin_entry;
    use manguebyte_clap::clap_sys::events::{
        CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, clap_event_header, clap_event_note,
        clap_input_events, clap_output_events,
    };
    use manguebyte_clap::clap_sys::ext::audio_ports_activation::{
        CLAP_EXT_AUDIO_PORTS_ACTIVATION, clap_plugin_audio_ports_activation,
    };
    use manguebyte_clap::clap_sys::ext::params::{
        CLAP_EXT_PARAMS, clap_host_params, clap_param_clear_flags, clap_param_rescan_flags,
    };
    use manguebyte_clap::clap_sys::factory::plugin_factory::{
        CLAP_PLUGIN_FACTORY_ID, clap_plugin_factory,
    };
    use manguebyte_clap::clap_sys::host::clap_host;
    use manguebyte_clap::clap_sys::plugin::clap_plugin;
    use manguebyte_clap::clap_sys::process::clap_process;
    use manguebyte_clap::clap_sys::version::CLAP_VERSION;
    use manguebyte_plugin::Plugin;

    use super::SR;
    use caecilia_plugin::Caecilia;

    // The `clap_host` the plugin is given: it asks for `params` alone.
    unsafe extern "C" fn rescan(_: *const clap_host, _: clap_param_rescan_flags) {}
    unsafe extern "C" fn clear(_: *const clap_host, _: u32, _: clap_param_clear_flags) {}
    unsafe extern "C" fn request(_: *const clap_host) {}
    static HOST_PARAMS: clap_host_params = clap_host_params {
        rescan: Some(rescan),
        clear: Some(clear),
        request_flush: Some(request),
    };
    unsafe extern "C" fn get_extension(_: *const clap_host, id: *const c_char) -> *const c_void {
        // SAFETY: `id` is the NUL-terminated extension name the plugin passes.
        if unsafe { CStr::from_ptr(id) } == CLAP_EXT_PARAMS {
            ptr::from_ref(&HOST_PARAMS).cast()
        } else {
            ptr::null()
        }
    }

    // The block's input events, a list of header pointers the ctx owns.
    unsafe extern "C" fn event_count(list: *const clap_input_events) -> u32 {
        // SAFETY: `ctx` is the `Vec<*const clap_event_header>` `run` sets.
        unsafe { (*(*list).ctx.cast::<Vec<*const clap_event_header>>()).len() as u32 }
    }
    unsafe extern "C" fn event_get(
        list: *const clap_input_events,
        index: u32,
    ) -> *const clap_event_header {
        // SAFETY: as above; `index` is below the size just reported.
        unsafe { (&*(*list).ctx.cast::<Vec<*const clap_event_header>>())[index as usize] }
    }
    unsafe extern "C" fn event_push(
        _: *const clap_output_events,
        _: *const clap_event_header,
    ) -> bool {
        true
    }

    /// The main output and the sixteen buses, as host memory: `samples`
    /// is port, channel, frames.
    pub struct OutputPorts {
        pub samples: Vec<Vec<Vec<f32>>>,
    }

    impl OutputPorts {
        pub fn new(frames: usize) -> Self {
            // One spare frame keeps every channel pointer valid at 0 frames.
            Self {
                samples: vec![vec![vec![0.0; frames.max(1)]; 2]; 17],
            }
        }

        pub fn peak(&self, port: usize) -> f32 {
            self.samples[port]
                .iter()
                .flatten()
                .fold(0.0f32, |p, s| p.max(s.abs()))
        }
    }

    /// A host at the C boundary: what a DAW is to the plugin. `plugin` is
    /// destroyed before `host` is freed, in `drop`.
    pub struct RawHost {
        entry: clap_plugin_entry,
        plugin: *const clap_plugin,
        host: *mut clap_host,
    }

    impl RawHost {
        pub fn new() -> Self {
            static ENTRY: clap_plugin_entry = manguebyte_clap::plugin_entry::<Caecilia>();
            let host = Box::into_raw(Box::new(clap_host {
                clap_version: CLAP_VERSION,
                host_data: ptr::null_mut(),
                name: c"bus test".as_ptr(),
                vendor: c"Caecilia".as_ptr(),
                url: c"".as_ptr(),
                version: c"1".as_ptr(),
                get_extension: Some(get_extension),
                request_restart: Some(request),
                request_process: Some(request),
                request_callback: Some(request),
            }));
            let id = CString::new(Caecilia::INFO.id).unwrap();
            // SAFETY: `ENTRY` is the framework's CLAP entry for Caecilia,
            // and `host` outlives the plugin it is given to, which `drop`
            // destroys before freeing it.
            unsafe {
                assert!((ENTRY.init.unwrap())(c"caecilia-buses.clap".as_ptr()));
                let factory = (ENTRY.get_factory.unwrap())(CLAP_PLUGIN_FACTORY_ID.as_ptr())
                    .cast::<clap_plugin_factory>();
                assert!(!factory.is_null());
                let plugin = ((*factory).create_plugin.unwrap())(factory, host, id.as_ptr());
                assert!(!plugin.is_null(), "the factory refused Caecilia");
                assert!((*plugin).init.unwrap()(plugin));
                Self {
                    entry: ENTRY,
                    plugin,
                    host,
                }
            }
        }

        /// `set_active` on `audio-ports-activation`, before activation as
        /// the extension requires; `index` counts the output ports, main
        /// first.
        pub fn set_output_active(&self, index: u32, active: bool) {
            // SAFETY: the extension is asked for by its own name and type.
            let ports = unsafe {
                (*self.plugin).get_extension.unwrap()(
                    self.plugin,
                    CLAP_EXT_AUDIO_PORTS_ACTIVATION.as_ptr(),
                )
                .cast::<clap_plugin_audio_ports_activation>()
                .as_ref()
            }
            .expect("the plugin offers audio-ports-activation");
            // SAFETY: the plugin is inactive and this is its main thread,
            // as the extension requires; the sample size is this host's.
            assert!(unsafe { ports.set_active.unwrap()(self.plugin, false, index, active, 32) });
        }

        pub fn activate(&self) {
            // SAFETY: the plugin was made in `new` and lives until `drop`.
            unsafe {
                assert!((*self.plugin).activate.unwrap()(self.plugin, SR, 1, 256));
                assert!((*self.plugin).start_processing.unwrap()(self.plugin));
            }
        }

        /// One `process` call of `frames` frames: `events` in, the
        /// plugin's 17 output ports pointed into `outs`. The status the
        /// wrapper gave comes back.
        pub fn run(
            &self,
            frames: u32,
            events: &[*const clap_event_header],
            outs: &mut OutputPorts,
        ) -> i32 {
            let mut events = events.to_vec();
            let input = clap_input_events {
                ctx: (&raw mut events).cast(),
                size: Some(event_count),
                get: Some(event_get),
            };
            let output = clap_output_events {
                ctx: ptr::null_mut(),
                try_push: Some(event_push),
            };
            let mut channel_ptrs: Vec<Vec<*mut f32>> = outs
                .samples
                .iter_mut()
                .map(|port| port.iter_mut().map(|c| c.as_mut_ptr()).collect())
                .collect();
            let mut ports: Vec<clap_audio_buffer> = channel_ptrs
                .iter_mut()
                .map(|p| clap_audio_buffer {
                    data32: p.as_mut_ptr(),
                    data64: ptr::null_mut(),
                    channel_count: p.len() as u32,
                    latency: 0,
                    constant_mask: 0,
                })
                .collect();
            let process = clap_process {
                steady_time: -1,
                frames_count: frames,
                transport: ptr::null(),
                audio_inputs: ptr::null(),
                audio_outputs: ports.as_mut_ptr(),
                audio_inputs_count: 0,
                audio_outputs_count: ports.len() as u32,
                in_events: &raw const input,
                out_events: &raw const output,
            };
            // SAFETY: `plugin` is active and this thread is the only one
            // calling `process`; every pointer in `process` is to memory
            // alive for the call, and each port's channels are at least
            // `frames` frames of `outs`' own.
            unsafe { (*self.plugin).process.unwrap()(self.plugin, &raw const process) }
        }
    }

    impl Drop for RawHost {
        fn drop(&mut self) {
            // SAFETY: a host's own order — processing stopped, the plugin
            // deactivated and destroyed, and only then the host it was
            // given freed. `deinit` balances `new`'s `init`.
            unsafe {
                (*self.plugin).stop_processing.unwrap()(self.plugin);
                (*self.plugin).deactivate.unwrap()(self.plugin);
                (*self.plugin).destroy.unwrap()(self.plugin);
                if let Some(deinit) = self.entry.deinit {
                    deinit();
                }
                drop(Box::from_raw(self.host));
            }
        }
    }

    /// A NoteOn as a CLAP event, on `channel` for `key`.
    pub fn note_on(channel: u8, key: u8) -> clap_event_note {
        clap_event_note {
            header: clap_event_header {
                size: size_of::<clap_event_note>() as u32,
                time: 0,
                space_id: CLAP_CORE_EVENT_SPACE_ID,
                type_: CLAP_EVENT_NOTE_ON,
                flags: 0,
            },
            note_id: -1,
            port_index: 0,
            channel: channel as i16,
            key: key as i16,
            velocity: 0.8,
        }
    }
}

/// An empty block through the real CLAP wrapper, buses on and off: it does
/// not fail, and the block after it still sounds on the main output and on
/// the division's bus.
#[test]
fn an_empty_block_reaches_the_clap_wrapper_and_harms_nothing() {
    use manguebyte_clap::clap_sys::process::CLAP_PROCESS_CONTINUE;
    use std::ptr;

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let note = empty_block::note_on(0, 60);
    let events: [*const manguebyte_clap::clap_sys::events::clap_event_header; 1] =
        [ptr::from_ref(&note).cast()];

    // Every bus on: an empty block, then the Grand-Orgue's key over the
    // blocks a host runs, none longer than the 256 it was activated with.
    set_organ_for_new_instances(caecilia_organ());
    let host = empty_block::RawHost::new();
    host.activate();
    let mut outs = empty_block::OutputPorts::new(0);
    assert_eq!(
        host.run(0, &events, &mut outs),
        CLAP_PROCESS_CONTINUE,
        "the empty block did not fail"
    );
    let mut outs = empty_block::OutputPorts::new(256);
    for block in 0..16 {
        // The key goes down once, on the first block, as a host sends it.
        let sent: &[_] = if block == 0 { &events } else { &[] };
        assert_eq!(
            host.run(256, sent, &mut outs),
            CLAP_PROCESS_CONTINUE,
            "block {block}"
        );
    }
    assert!(outs.peak(0) > 0.0, "the Grand-Orgue on the main output");
    assert!(outs.peak(2) > 0.0, "and on bus 1 of its own");

    // "Division 02" switched off before activation: the same story, and
    // the bus stays silent.
    set_organ_for_new_instances(caecilia_organ());
    let host = empty_block::RawHost::new();
    host.set_output_active(2, false);
    host.activate();
    let mut outs = empty_block::OutputPorts::new(0);
    assert_eq!(
        host.run(0, &events, &mut outs),
        CLAP_PROCESS_CONTINUE,
        "the empty block did not fail"
    );
    let mut outs = empty_block::OutputPorts::new(256);
    for block in 0..16 {
        let sent: &[_] = if block == 0 { &events } else { &[] };
        assert_eq!(
            host.run(256, sent, &mut outs),
            CLAP_PROCESS_CONTINUE,
            "block {block}"
        );
    }
    assert!(outs.peak(0) > 0.0, "the Grand-Orgue on the main output");
    assert_eq!(outs.peak(2), 0.0, "the switched-off bus stays silent");
}

/// A block longer than `prepare` promised: no adapter lets one through, but
/// if one ever did, the divisions' buses must not be left with an older
/// block's sound — the buses a host listens to are made silent, and the
/// main output still renders.
#[test]
fn a_block_past_the_prepared_size_silences_the_buses() {
    use manguebyte_dsp::{ProcessSpec, Transport};
    use manguebyte_plugin::output::OutputBuffer;
    use manguebyte_plugin::{AuxOutputs, Plugin};

    let _organ = ORGAN_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    set_organ_for_new_instances(caecilia_organ());
    let mut plugin = Caecilia::new();
    plugin.prepare(&ProcessSpec {
        sample_rate: SR,
        max_block_size: 256,
        num_channels: 2,
    });
    let mut events = EventBuffer::with_capacity(1);
    events
        .push(Event {
            offset: 0,
            kind: EventKind::NoteOn {
                note: NoteRef::new(0, 60),
                velocity: 0.8,
            },
        })
        .unwrap();
    let note = ProcessContext::new(&events, &Transport::STOPPED);
    let quiet = EventBuffer::with_capacity(1);
    let silent = ProcessContext::new(&quiet, &Transport::STOPPED);
    let mut output = OutputBuffer::new();
    let mut aux = AuxOutputs::for_plugin::<Caecilia>(512);

    // A first, promised-sized block with a key down: the Grand-Orgue's
    // sound stays in the scratch, which a careless block could hand out.
    let mut buffer = AudioBuffer::<f32>::new(2, 256);
    aux.start_block(256);
    plugin.process_with_aux(&mut buffer, &note, &mut output, &mut aux);
    assert!(peak(aux.bus(1).unwrap()) > 0.0, "the Grand-Orgue sounded");

    // A 512-frame block, longer than prepare's word: the scratch cannot
    // take it, so nothing is rendered to the buses and what the first
    // block left behind is overwritten with silence.
    let mut buffer = AudioBuffer::<f32>::new(2, 512);
    aux.start_block(512);
    plugin.process_with_aux(&mut buffer, &silent, &mut output, &mut aux);
    for d in 0..16 {
        assert_eq!(
            peak(aux.bus(d).unwrap()),
            0.0,
            "bus {d} kept no stale sound"
        );
    }
    assert!(
        buffer.peak() > 0.0,
        "the main output went on rendering the held note"
    );
}
