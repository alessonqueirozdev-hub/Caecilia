// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The sound panel: the level, the reverb and its room, the master EQ, each
//! moved from the console as a gesture the host records.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::Caecilia;
use manguebyte_dsp::event::EventKind;
use manguebyte_gfx::Point;
use manguebyte_gui::HeadlessEditor;
use manguebyte_midi::MidiMessage;
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

    fn get(&self, role: Role, name: &str) -> AccessEntry {
        self.ui
            .find(role, name)
            .unwrap_or_else(|| panic!("no {role:?} named {name}"))
    }

    fn says(&self, name: &str) -> String {
        self.get(Role::Group, name).node.value.unwrap_or_default()
    }

    fn settle(&mut self) {
        for _ in 0..3 {
            self.h.render(256);
            self.ui.tick();
        }
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self.get(role, name).bounds.center();
        self.ui.press(at);
        self.ui.release(at);
        self.settle();
    }

    /// Drag slider `name` from its middle by `dx`, `dy`.
    fn drag(&mut self, name: &str, dx: f32, dy: f32) {
        let c = self.get(Role::Slider, name).bounds.center();
        self.ui.press(c);
        self.ui.move_to(Point::new(c.x + dx / 2.0, c.y + dy / 2.0));
        self.ui.move_to(Point::new(c.x + dx, c.y + dy));
        self.ui.release(Point::new(c.x + dx, c.y + dy));
        self.settle();
    }
}

#[test]
fn the_sound_panel_says_how_the_organ_sounds() {
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    assert_eq!(
        c.says("Wind"),
        "Sound: volume +0.0 dB, reverb 25 % in the hall"
    );
    c.click(Role::Button, "Page EQ");
    assert_eq!(
        c.says("Wind"),
        "EQ on: Warmth +2.0 dB, Boxiness -2.5 dB, Body +0.0 dB, Presence +1.5 dB, Air +2.0 dB"
    );
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-sound@1x.png"), png).unwrap();
}

#[test]
fn every_control_moves_its_parameter() {
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    let params = c.h.plugin_mut().params();

    c.click(Role::Button, "Room Cathedral");
    assert_eq!(params.room.index(), 3);
    assert!(
        c.says("Wind").contains("in the cathedral"),
        "{}",
        c.says("Wind")
    );

    let before = params.level.value();
    c.drag("Volume", -120.0, 0.0);
    assert!(
        params.level.value() < before - 3.0,
        "{}",
        params.level.value()
    );

    c.drag("Reverb", 80.0, 0.0);
    assert!(params.reverb.value() > 0.3, "{}", params.reverb.value());

    c.click(Role::Button, "Page EQ");
    c.drag("EQ Air", 0.0, -20.0);
    assert!(
        params.eq_bands[4].value() > 2.5,
        "{}",
        params.eq_bands[4].value()
    );

    c.click(Role::Button, "EQ");
    assert!(!params.eq.value());
    assert!(c.says("Wind").contains("EQ off"));
}

#[test]
fn the_tuning_page_tunes_the_organ_and_its_tremulant() {
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    c.click(Role::Button, "Page Tuning");
    assert_eq!(c.says("Wind"), "Tuning: Equal on C, A 440.0 Hz");
    let params = c.h.plugin_mut().params();
    c.click(Role::Button, "Werckmeister III");
    c.click(Role::Button, "Key G");
    c.click(Role::Button, "A 415");
    assert_eq!(params.temperament.index(), 2);
    assert_eq!(params.temperament_key.index(), 7);
    assert!(
        (params.pitch.value() - 415.0).abs() < 0.05,
        "{}",
        params.pitch.value()
    );
    assert_eq!(c.says("Wind"), "Tuning: Werckmeister III on G, A 415.0 Hz");
    c.click(Role::Button, "Page Tremulant");
    assert_eq!(c.says("Wind"), "Tremulant: 5.50 Hz, depth 50 %");
    c.drag("Tremulant rate", 60.0, 0.0);
    c.drag("Tremulant depth", -60.0, 0.0);
    assert!(params.tremulant_rate.value() > 5.6);
    assert!(params.tremulant_depth.value() < 0.45);
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-tuning@1x.png"), png).unwrap();
}

#[test]
fn learn_binds_a_midi_control_to_a_stop_pressed_on_the_console() {
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    c.click(Role::Button, "Page MIDI");
    assert!(
        c.says("Wind").ends_with("no control learned"),
        "{}",
        c.says("Wind")
    );
    c.click(Role::Button, "Learn");
    let params = c.h.plugin_mut().params();
    let bourdon =
        c.h.plugin_mut()
            .organ()
            .stops()
            .iter()
            .position(|s| s.name == "Bourdon 16")
            .expect("a Bourdon 16");
    let before = params.stop_mask();
    c.click(Role::Switch, "Grand-Orgue Bourdon 16");
    assert_eq!(
        params.stop_mask(),
        before,
        "learning, the stop does not move"
    );
    assert!(
        c.says("Wind")
            .contains("waiting for a control for Bourdon 16"),
        "{}",
        c.says("Wind")
    );

    // The organist moves controller 20 on channel 1.
    let cc = |value| MidiMessage::ControlChange {
        channel: 0,
        controller: 20,
        value,
    };
    let at = c.h.position();
    c.h.schedule(at, EventKind::Midi(cc(127)));
    c.settle();
    let said = c.says("Wind");
    assert!(
        said.contains("CC 20 \u{b7} ch 1 now plays Bourdon 16"),
        "{said}"
    );
    assert!(said.contains("CC 20 \u{b7} ch 1 to Bourdon 16"), "{said}");

    // Learn off, the controller draws the stop.
    c.click(Role::Button, "Learn");
    let at = c.h.position();
    c.h.schedule(at, EventKind::Midi(cc(0)));
    c.h.schedule(at + 10, EventKind::Midi(cc(127)));
    c.settle();
    assert_ne!(params.stop_mask() & (1 << bourdon), before & (1 << bourdon));

    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-midi@1x.png"), png).unwrap();

    c.click(Role::Button, "Forget CC 20 \u{b7} ch 1");
    assert!(
        c.says("Wind").ends_with("no control learned"),
        "{}",
        c.says("Wind")
    );
}

#[test]
fn the_midi_page_sets_the_manual_switches() {
    let mut c = Console::open();
    c.click(Role::Button, "Settings");
    c.click(Role::Button, "Page MIDI");
    assert!(
        c.says("Wind").contains("no switch to Récit"),
        "{}",
        c.says("Wind")
    );
    c.click(Role::Button, "Switches on the top B and C");
    let said = c.says("Wind");
    assert!(said.contains("B6 switches to Récit"), "{said}");
    assert!(said.contains("C7 switches to Grand-Orgue"), "{said}");
    // A switch learned by hand: the next key moved switches to it.
    c.click(Role::Button, "Switch to Grand-Orgue");
    assert!(
        c.says("Wind")
            .contains("waiting for a control for Play on Grand-Orgue"),
        "{}",
        c.says("Wind")
    );
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-switches@1x.png"), png).unwrap();
}
