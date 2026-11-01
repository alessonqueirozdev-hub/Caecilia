// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demonstrations: the organ heard without a keyboard. A piece draws
//! its stops, plays on its own (the keys lighting as they go), ends by
//! itself or when pressed again; the tour plays every stop alone.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::Duration;

use caecilia_plugin::Caecilia;
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

const BLOCK: usize = 256;

struct Console {
    h: Harness<Caecilia>,
    ui: HeadlessEditor,
    _editor: Box<dyn OpenEditor>,
    _link: HostLink,
}

impl Console {
    fn open() -> Console {
        let mut h = Harness::<Caecilia>::new(48000.0, BLOCK);
        h.render(BLOCK);
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

    /// `seconds` of sound and of the console's clock, frame by frame; the
    /// loudest sample heard.
    fn play_for(&mut self, seconds: f32) -> f32 {
        let blocks = (seconds * 48000.0 / BLOCK as f32).ceil() as usize;
        let mut peak = 0.0f32;
        for _ in 0..blocks {
            let out = self.h.render(BLOCK);
            for ch in 0..out.num_channels() {
                for v in out.channel(ch) {
                    peak = peak.max(v.abs());
                }
            }
            self.ui
                .advance(Duration::from_secs_f32(BLOCK as f32 / 48000.0));
            self.ui.tick();
        }
        peak
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self.get(role, name).bounds.center();
        self.ui.press(at);
        self.ui.release(at);
        self.play_for(0.02);
    }
}

/// Whether no key is down on any manual.
fn nothing_held(c: &Console) -> bool {
    let manuals = c.says("Manuals");
    manuals.matches(" held").count() == manuals.matches("nothing held").count()
}

const TOCCATA: &str = "Play J. S. Bach \u{2014} Toccata in D minor, BWV 565 (opening)";

#[test]
fn a_piece_draws_its_stops_and_plays_by_itself() {
    let mut c = Console::open();
    c.click(Role::Button, "Demos");
    assert!(
        c.says("Wind").starts_with("Demonstrations: J. S. Bach"),
        "{}",
        c.says("Wind")
    );
    c.click(Role::Button, "Cancel");
    assert!(c.play_for(0.5) < 1e-3, "silent with nothing drawn");

    c.click(Role::Button, TOCCATA);
    // The great's chorus and reeds drawn, the swell's flutes not.
    let left = c.says("Left jamb");
    assert!(left.contains("Montre 8"), "{left}");
    let why = c.says("Registration");
    assert!(why.contains("Toccata"), "the history names it: {why}");
    assert!(
        c.says("Wind")
            .contains("Toccata in D minor, BWV 565 (opening) (playing)")
    );
    // It sounds, and the keys under it go down.
    let peak = c.play_for(0.3);
    assert!(peak > 1e-3, "the toccata is heard: {peak}");
    assert!(!nothing_held(&c), "{}", c.says("Manuals"));
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-demos@1x.png"), png).unwrap();

    // It ends by itself, every key lifted.
    c.play_for(7.0);
    assert!(!c.says("Wind").contains("(playing)"), "{}", c.says("Wind"));
    assert!(nothing_held(&c), "{}", c.says("Manuals"));
}

#[test]
fn pressing_a_piece_again_stops_it() {
    let mut c = Console::open();
    c.click(Role::Button, "Demos");
    c.click(Role::Button, TOCCATA);
    c.play_for(4.0);
    c.click(Role::Button, TOCCATA);
    c.play_for(0.1);
    assert!(!c.says("Wind").contains("(playing)"), "{}", c.says("Wind"));
    assert!(nothing_held(&c), "{}", c.says("Manuals"));
}

#[test]
fn the_tour_plays_every_stop_alone() {
    let mut c = Console::open();
    c.click(Role::Button, "Demos");
    c.click(Role::Button, "Play Every stop, one by one");
    c.play_for(0.5);
    let first = c.says("Wind");
    assert!(first.contains("(playing "), "{first}");
    let drawn = |c: &Console| {
        let jambs = format!("{} {}", c.says("Left jamb"), c.says("Right jamb"));
        jambs.matches(", ").count()
    };
    assert_eq!(drawn(&c), 0, "one stop drawn");
    c.play_for(2.4);
    let second = c.says("Wind");
    assert_ne!(first, second, "the tour moved on");
    assert_eq!(drawn(&c), 0, "still one stop drawn");
}
