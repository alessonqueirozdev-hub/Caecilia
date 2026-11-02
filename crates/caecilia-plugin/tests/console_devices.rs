// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The Audio page: where an application serves the devices (the
//! standalone), the console shows the outputs, the rates, the buffer sizes
//! and the MIDI ports, and sends what the player chooses; in a host, which
//! owns its own devices, there is no such page. No real device is touched:
//! the service here is made up.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::{Caecilia, DeviceEntry, DeviceRequest, DevicesView};
use manguebyte_gui::HeadlessEditor;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_plugin::editor::{
    EditorContext, EditorHost, EditorSpec, HostLink, OpenEditor, ParentWindow,
};
use manguebyte_plugin::{Harness, Plugin};
use manguebyte_ui::{AccessEntry, Role};

thread_local! {
    static OPENED: RefCell<Option<HeadlessEditor>> = const { RefCell::new(None) };
}

struct Console {
    h: Harness<Caecilia>,
    ui: HeadlessEditor,
    _editor: Box<dyn OpenEditor>,
    _link: HostLink,
}

impl Console {
    fn open() -> Console {
        let mut h = Harness::<Caecilia>::new(48000.0, 256);
        h.render(256);
        let spec = h
            .plugin_mut()
            .editor_spec()
            .on_headless_open(|ui| OPENED.with(|slot| *slot.borrow_mut() = Some(ui)));
        let host: Rc<dyn EditorHost> =
            Rc::new(RecordingEditorHost::new().with_params(h.plugin_mut().params()));
        let (context, link) = EditorContext::attach(host);
        let parent = ParentWindow::win32(0x10).expect("not null");
        let mut editor = spec.open(parent, Some(1.0), context).expect("opens");
        let ui = OPENED.with(|slot| slot.borrow_mut().take()).expect("hook");
        editor.set_visible(true);
        ui.tick();
        Console {
            h,
            ui,
            _editor: editor,
            _link: link,
        }
    }

    fn find(&self, role: Role, name: &str) -> Option<AccessEntry> {
        self.ui.find(role, name)
    }

    fn says(&self, name: &str) -> String {
        self.find(Role::Group, name)
            .and_then(|e| e.node.value)
            .unwrap_or_default()
    }

    fn settle(&mut self) {
        for _ in 0..3 {
            self.h.render(256);
            self.ui.advance(std::time::Duration::from_millis(16));
            self.ui.tick();
        }
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self
            .find(role, name)
            .unwrap_or_else(|| panic!("no {role:?} named {name}"))
            .bounds
            .center();
        self.ui.press(at);
        self.ui.release(at);
        self.settle();
    }
}

fn entry(id: &str, name: &str, chosen: bool) -> DeviceEntry {
    DeviceEntry {
        id: id.into(),
        name: name.into(),
        chosen,
        is_default: false,
    }
}

/// A made-up sound card setup that does what it is asked.
fn serve() -> Rc<RefCell<Vec<DeviceRequest>>> {
    let asked: Rc<RefCell<Vec<DeviceRequest>>> = Rc::default();
    let seen = Rc::clone(&asked);
    let mut output = "speakers".to_string();
    let mut rate = None;
    let mut buffer = None;
    let mut keyboard = true;
    caecilia_plugin::serve_devices(move |requests, _describe| {
        for r in requests {
            seen.borrow_mut().push(r.clone());
            match r {
                DeviceRequest::Output(id) => output = id.clone(),
                DeviceRequest::SampleRate(r) => rate = *r,
                DeviceRequest::BufferSize(b) => buffer = *b,
                DeviceRequest::MidiInput(_, open) => keyboard = *open,
            }
        }
        let name = if output == "umc" {
            "UMC204HD"
        } else {
            "Speakers"
        };
        DevicesView {
            outputs: vec![
                entry("speakers", "Speakers", output == "speakers"),
                entry("umc", "UMC204HD", output == "umc"),
            ],
            sample_rates: vec![44_100, 48_000, 96_000],
            sample_rate: rate,
            buffer_sizes: vec![128, 256, 512],
            buffer_size: buffer,
            midi_inputs: vec![entry("kbd", "Organ keyboard", keyboard)],
            report: format!("Playing on \"{name}\" at {} Hz", rate.unwrap_or(48_000)),
            failed: false,
        }
    });
    asked
}

#[test]
fn in_a_host_there_is_no_audio_page() {
    caecilia_plugin::stop_serving_devices();
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    assert!(c.find(Role::Button, "Page MIDI").is_some());
    assert!(c.find(Role::Button, "Page Audio").is_none());
}

#[test]
fn the_audio_page_chooses_the_devices() {
    let asked = serve();
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    c.click(Role::Button, "Page Audio");
    assert_eq!(
        c.says("Wind"),
        "Audio: Playing on \"Speakers\" at 48000 Hz; rate Device, buffer Device; \
         MIDI in: Organ keyboard open"
    );

    // The outputs listed, one chosen.
    c.click(Role::Button, "Output");
    c.click(Role::Button, "Output UMC204HD");
    c.click(Role::Button, "Rate 96k");
    c.click(Role::Button, "Buffer 128");
    // The input ports are on the MIDI page.
    c.click(Role::Button, "Page MIDI");
    c.click(Role::Button, "MIDI Organ keyboard");
    c.click(Role::Button, "Page Audio");
    assert_eq!(
        *asked.borrow(),
        vec![
            DeviceRequest::Output("umc".into()),
            DeviceRequest::SampleRate(Some(96_000)),
            DeviceRequest::BufferSize(Some(128)),
            DeviceRequest::MidiInput("kbd".into(), false),
        ]
    );
    assert_eq!(
        c.says("Wind"),
        "Audio: Playing on \"UMC204HD\" at 96000 Hz; rate 96k, buffer 128; \
         MIDI in: Organ keyboard closed"
    );
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-audio@1x.png"), png).unwrap();
    caecilia_plugin::stop_serving_devices();
}

#[test]
fn the_audio_page_comes_when_the_devices_are_served_after_the_console_opens() {
    caecilia_plugin::stop_serving_devices();
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    assert!(c.find(Role::Button, "Page Audio").is_none());
    let _asked = serve();
    c.ui.advance(std::time::Duration::from_millis(1100));
    c.settle();
    assert!(c.find(Role::Button, "Page Audio").is_some());
    caecilia_plugin::stop_serving_devices();
}

/// Every control of every page lies inside the desk, even in a short
/// window.
#[test]
fn every_page_fits_the_desk_of_a_short_window() {
    let _asked = serve();
    let mut c = Console::open();
    c.ui.window().resize(manguebyte_window::PhysicalSize {
        width: 1600,
        height: 800,
    });
    c.settle();
    c.click(Role::Button, "Settings");
    let wind = c.find(Role::Group, "Wind").expect("the desk");
    let desk = wind.bounds;
    assert!(desk.height < 130.0, "a short desk: {desk:?}");
    for page in ["Sound", "EQ", "Tuning", "Tremulant", "MIDI", "Audio"] {
        c.click(Role::Button, &format!("Page {page}"));
        let controls: Vec<AccessEntry> =
            c.ui.access_tree()
                .into_iter()
                .filter(|e| e.id == wind.id && e.item.is_some())
                .collect();
        assert!(controls.len() > 3, "{page}: {controls:?}");
        for e in &controls {
            assert!(
                e.bounds.y >= desk.y && e.bounds.bottom() <= desk.bottom() + 0.5,
                "{page}: {} at {:?} leaves the desk {desk:?}",
                e.node.name,
                e.bounds
            );
        }
    }
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-short@1x.png"), png).unwrap();
    caecilia_plugin::stop_serving_devices();
}
