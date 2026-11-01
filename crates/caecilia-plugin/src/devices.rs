// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The sound card and the MIDI ports, as the console's Audio page shows
//! them: only where an application owns the devices (the standalone), never
//! in a host, which owns its own.
//!
//! The application serves the console on its main thread, the thread the
//! console runs on: [`serve_devices`] hands it a function the console calls
//! with what the player asked for, and which answers with what there is now.
//! The plugin crate never touches a device itself.

use std::cell::RefCell;

/// One device or port the Audio page offers.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct DeviceEntry {
    /// The id the application knows it by.
    pub id: String,
    /// What it is called.
    pub name: String,
    /// Whether it is the one in use (for a port: whether it is open).
    pub chosen: bool,
    /// Whether it is the system's default.
    pub is_default: bool,
}

/// What the Audio page shows: the outputs, the rates and buffer sizes the
/// output in use accepts and the ones chosen, the MIDI input ports, and
/// what plays, in plain words.
#[derive(Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct DevicesView {
    /// The outputs, the one in use chosen.
    pub outputs: Vec<DeviceEntry>,
    /// The rates the output in use accepts.
    pub sample_rates: Vec<u32>,
    /// The rate asked for; `None` lets the device choose.
    pub sample_rate: Option<u32>,
    /// The buffer sizes the output in use accepts.
    pub buffer_sizes: Vec<u32>,
    /// The buffer size asked for; `None` lets the device choose.
    pub buffer_size: Option<u32>,
    /// The MIDI input ports, the open ones chosen.
    pub midi_inputs: Vec<DeviceEntry>,
    /// What plays, then each thing that fell back or failed, a line each.
    pub report: String,
    /// Whether the last change failed (and was undone).
    pub failed: bool,
}

/// What the player asked of the devices on the Audio page.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DeviceRequest {
    /// Play on the output with this id.
    Output(String),
    /// Run at this rate, or the device's own.
    SampleRate(Option<u32>),
    /// Ask for this buffer size, or the device's own.
    BufferSize(Option<u32>),
    /// Open or close the MIDI input port with this id.
    MidiInput(String, bool),
}

/// What the application gives the console: it carries out the requests, in
/// order, looks for devices plugged in or pulled out, and says what there is
/// now; it lists the devices afresh (which can take a moment) only when
/// asked to `describe` them, and may answer from what it listed before.
type Service = Box<dyn FnMut(&[DeviceRequest], bool) -> DevicesView>;

thread_local! {
    static SERVICE: RefCell<Option<Service>> = const { RefCell::new(None) };
}

/// Let consoles opened on this thread show and change the devices through
/// `service`. Called by an application that owns the devices, on the
/// thread its windows run on.
pub fn serve_devices(service: impl FnMut(&[DeviceRequest], bool) -> DevicesView + 'static) {
    SERVICE.with(|slot| *slot.borrow_mut() = Some(Box::new(service)));
}

/// Stop serving the devices on this thread.
pub fn stop_serving_devices() {
    SERVICE.with(|slot| slot.borrow_mut().take());
}

/// Carry out `requests` and read the devices, listing them afresh if
/// `describe`; `None` if this thread does not serve them.
pub(crate) fn ask(requests: &[DeviceRequest], describe: bool) -> Option<DevicesView> {
    SERVICE.with(|slot| {
        // A request made while the service runs (it never should) is not
        // served rather than panicking.
        let mut slot = slot.try_borrow_mut().ok()?;
        slot.as_mut().map(|service| service(requests, describe))
    })
}
