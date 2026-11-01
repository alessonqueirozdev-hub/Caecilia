// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The registration panel: the selector line, the history and why, through
//! the console opened headless.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::Caecilia;
use manguebyte_gui::HeadlessEditor;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_plugin::editor::{
    EditorContext, EditorHost, EditorSpec, HostLink, Modifiers, OpenEditor, ParentWindow,
};
use manguebyte_plugin::{Harness, Plugin};
use manguebyte_ui::{AccessEntry, Key, Role};

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

    fn settle(&mut self) {
        self.h.render(256);
        self.ui.tick();
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self.get(role, name).bounds.center();
        self.ui.press(at);
        self.ui.release(at);
        self.settle();
    }

    fn registration(&self) -> String {
        self.get(Role::Group, "Registration")
            .node
            .value
            .unwrap_or_default()
    }

    /// What the selector line says beside its text.
    fn line(&self) -> String {
        self.get(Role::TextInput, "Selector")
            .node
            .description
            .unwrap_or_default()
    }

    fn drawn(&mut self, name: &str) -> bool {
        let n = self
            .h
            .plugin_mut()
            .organ()
            .stops()
            .iter()
            .position(|s| s.name == name)
            .unwrap();
        self.h.params().stop_mask() & (1 << n) != 0
    }

    /// Focus the selector line and type `text`.
    fn type_selector(&mut self, text: &str) {
        self.click(Role::TextInput, "Selector");
        assert!(self.ui.type_text(text));
        self.ui.tick();
    }
}

#[test]
fn a_selector_shows_what_it_takes_and_enter_draws_it() {
    let mut c = Console::open();
    c.type_selector("div:recit & family:string");
    assert_eq!(c.line(), "takes Gambe 8, Voix C\u{e9}leste 8");
    assert!(!c.drawn("Gambe 8"), "nothing drawn before Enter");
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-selector@1x.png"), png).unwrap();

    assert!(c.ui.key(Key::Enter));
    c.settle();
    assert!(c.drawn("Gambe 8") && c.drawn("Voix C\u{e9}leste 8"));
    assert_eq!(c.line(), "drew 2 stops");
    assert!(
        c.registration().contains("drawn by a selector"),
        "{}",
        c.registration()
    );

    // Up brings it back; Shift+Enter retires what it takes.
    assert!(c.ui.key(Key::Up));
    c.ui.tick();
    assert_eq!(
        c.get(Role::TextInput, "Selector").node.value.as_deref(),
        Some("div:recit & family:string")
    );
    let shift = Modifiers {
        shift: true,
        ..Modifiers::NONE
    };
    assert!(c.ui.key_with(Key::Enter, shift));
    c.settle();
    assert!(!c.drawn("Gambe 8"));
    assert_eq!(c.line(), "retired 2 stops");
}

#[test]
fn a_selector_that_does_not_parse_says_why_and_draws_nothing() {
    let mut c = Console::open();
    let before = c.h.params().stop_mask();
    c.type_selector("family:reed & (");
    let said = c.line();
    assert!(!said.is_empty() && !said.starts_with("takes"), "{said}");
    assert!(c.ui.key(Key::Enter));
    c.settle();
    assert_eq!(c.h.params().stop_mask(), before);
    // Escape clears it.
    assert!(c.ui.key(Key::Escape));
    c.ui.tick();
    assert_eq!(
        c.get(Role::TextInput, "Selector").node.value.as_deref(),
        Some("")
    );
}

#[test]
fn a_step_of_the_history_pressed_is_gone_back_to_and_forward_again() {
    let mut c = Console::open();
    let opening = c.h.params().stop_mask();
    c.click(Role::Button, "General 4");
    c.click(Role::Switch, "R\u{e9}cit Basson-Hautbois 8");
    let with_hautboy = c.h.params().stop_mask();

    // Back to the opening, two steps.
    c.click(Role::Button, "Step Opening");
    assert_eq!(c.h.params().stop_mask(), opening);
    // The way redo would go is still there, ahead.
    c.click(Role::Button, "Step + Basson-Hautbois");
    assert_eq!(c.h.params().stop_mask(), with_hautboy);
}

#[test]
fn the_stop_under_the_pointer_says_why_it_is_drawn_or_what_would_draw_it() {
    let mut c = Console::open();
    c.click(Role::Button, "General 4");
    let tierce = c
        .get(Role::Switch, "Grand-Orgue Tierce 1 3/5")
        .bounds
        .center();
    c.ui.move_to(tierce);
    c.settle();
    let why = c.registration();
    assert!(why.contains("Tierce 1 3/5 not drawn"), "{why}");
    assert!(why.contains("would draw it"), "{why}");

    // The Récit's Bourdon came with General 4; the Montre was there from
    // the opening, and General 4 left it.
    let bourdon = c.get(Role::Switch, "R\u{e9}cit Bourdon 8").bounds.center();
    c.ui.move_to(bourdon);
    c.settle();
    let why = c.registration();
    assert!(why.contains("Bourdon 8 drawn by General 4"), "{why}");
    let montre = c.get(Role::Switch, "Grand-Orgue Montre 8").bounds.center();
    c.ui.move_to(montre);
    c.settle();
    let why = c.registration();
    assert!(why.contains("Montre 8 drawn as the organ opened"), "{why}");
}
