// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The console at half a screen: the window alone chooses it, and everything in
//! it does what the full console does.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::{Caecilia, EDITOR_SIZE};
use manguebyte_gui::HeadlessEditor;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_plugin::editor::{
    EditorContext, EditorHost, EditorSpec, HostLink, HostSize, OpenEditor, ParentWindow,
};
use manguebyte_plugin::{Harness, Plugin};
use manguebyte_ui::{AccessEntry, Role};

thread_local! {
    static OPENED: RefCell<Option<HeadlessEditor>> = const { RefCell::new(None) };
}

/// About half of a 1080p screen at 125 %.
const HALF: HostSize = HostSize::new(768, 830);

struct Console {
    h: Harness<Caecilia>,
    ui: HeadlessEditor,
    editor: Box<dyn OpenEditor>,
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
            editor,
            _link: link,
        }
    }

    fn resize(&mut self, size: HostSize) {
        self.editor.set_size(size);
        self.settle();
    }

    fn settle(&mut self) {
        for _ in 0..4 {
            self.h.render(256);
            self.ui.tick();
        }
    }

    fn find(&self, role: Role, name: &str) -> Option<AccessEntry> {
        self.ui.find(role, name)
    }

    fn get(&self, role: Role, name: &str) -> AccessEntry {
        self.find(role, name)
            .unwrap_or_else(|| panic!("no {role:?} named {name}"))
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self.get(role, name).bounds.center();
        self.ui.press(at);
        self.ui.release(at);
        self.settle();
    }

    fn is_compact(&self) -> bool {
        self.find(Role::Group, "Stops").is_some()
    }

    fn picture(&self, name: &str) {
        let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
        std::fs::create_dir_all(&dir).unwrap();
        let png = self.ui.pixels().expect("drawn").encode_png().unwrap();
        std::fs::write(dir.join(format!("{name}.png")), png).unwrap();
    }
}

#[test]
fn the_window_chooses_the_console_and_nothing_is_lost_between_them() {
    let mut c = Console::open();
    assert!(!c.is_compact(), "as large as it was designed");
    assert!(c.find(Role::Group, "Right jamb").is_some());

    // A window of the same shape, smaller: the whole console, zoomed.
    c.resize(HostSize::new(1200, 735));
    assert!(!c.is_compact());
    assert!(c.find(Role::Group, "Right jamb").is_some());

    c.resize(HALF);
    assert!(c.is_compact());
    assert!(c.find(Role::Group, "Right jamb").is_none());
    assert_eq!(c.ui.layout().expect("laid out").1, 1.0, "at its real size");

    // What's drawn here is drawn there.
    c.click(Role::Switch, "Récit Gambe 8");
    assert_eq!(
        c.get(Role::Switch, "Récit Gambe 8").node.state.checked,
        Some(true)
    );
    c.resize(EDITOR_SIZE);
    assert!(!c.is_compact());
    assert_eq!(
        c.get(Role::Switch, "Récit Gambe 8").node.state.checked,
        Some(true)
    );
    c.click(Role::Switch, "Récit Gambe 8");
    c.resize(HALF);
    assert_eq!(
        c.get(Role::Switch, "Récit Gambe 8").node.state.checked,
        Some(false)
    );
}

#[test]
fn every_stop_and_coupler_of_every_organ_can_be_pressed_at_half_a_screen() {
    let mut c = Console::open();
    c.resize(HALF);
    for (_, make) in caecilia_model::BUILT_IN_ORGANS {
        let organ = make();
        c.h.plugin_mut().params().use_organ(organ.clone()).unwrap();
        c.settle();
        for stop in organ.stops() {
            let division = organ.division(stop.division).unwrap();
            let name = format!("{} {}", division.name, stop.name);
            let before = c.get(Role::Switch, &name).node.state.checked;
            c.click(Role::Switch, &name);
            let after = c.get(Role::Switch, &name).node.state.checked;
            assert_ne!(before, after, "{name}");
        }
        for coupler in organ.couplers() {
            let before = c.get(Role::Switch, &coupler.name).node.state.checked;
            c.click(Role::Switch, &coupler.name);
            assert_ne!(
                before,
                c.get(Role::Switch, &coupler.name).node.state.checked,
                "{}",
                coupler.name
            );
        }
    }
}

#[test]
fn the_pistons_and_the_expression_answer() {
    let mut c = Console::open();
    c.resize(HALF);
    c.click(Role::Button, "General 5");
    let drawn = c.h.plugin_mut().params().stop_mask();
    assert_ne!(drawn, 0);
    c.click(Role::Button, "Cancel");
    assert_eq!(c.h.plugin_mut().params().stop_mask(), 0);
    c.click(Role::Button, "General 5");
    assert_eq!(c.h.plugin_mut().params().stop_mask(), drawn);
    c.click(Role::Button, "Next general");
    assert_ne!(c.h.plugin_mut().params().stop_mask(), drawn);
    c.click(Role::Button, "Undo");

    let swell = c.get(Role::Slider, "Récit").bounds;
    let at = manguebyte_gfx::Point::new(swell.x + swell.width / 4.0, swell.center().y);
    c.ui.press(at);
    c.ui.release(at);
    c.settle();
    let value = c.h.plugin_mut().params().swell.value();
    assert!((value - 0.25).abs() < 0.02, "the swell at {value}");
}

#[test]
fn a_panel_takes_the_stops_place_and_settings_keeps_what_the_bar_gave_up() {
    let mut c = Console::open();
    c.resize(HALF);
    assert!(c.find(Role::Button, "Day mode").is_none());
    assert!(c.find(Role::Button, "Demos").is_none());
    assert!(c.find(Role::Button, "Redo").is_none());

    c.click(Role::Button, "Models");
    assert!(c.find(Role::Switch, "Récit Gambe 8").is_none());
    c.picture("compact-models@1x");
    c.click(Role::Button, "Model Récit: flutes");
    assert_eq!(
        c.get(Role::Button, "Model Récit: flutes")
            .node
            .state
            .checked,
        Some(true)
    );
    c.click(Role::Button, "Models");
    assert!(c.find(Role::Switch, "Récit Gambe 8").is_some(), "and back");

    c.click(Role::Button, "Settings");
    c.picture("compact-settings@1x");
    c.click(Role::Button, "Day mode");
    assert!(c.find(Role::Button, "Night mode").is_some());
    c.click(Role::Button, "Demos");
    assert!(
        c.find(Role::Button, "Demos").is_some(),
        "lit, to close it by"
    );
    c.picture("compact-demos-day@1x");
    c.click(Role::Button, "Demos");
    assert!(c.find(Role::Switch, "Récit Gambe 8").is_some());
    c.picture("compact-day@1x");
}

#[test]
fn nothing_is_out_of_reach_in_the_smallest_window() {
    let mut c = Console::open();
    for (_, make) in caecilia_model::BUILT_IN_ORGANS {
        let organ = make();
        c.h.plugin_mut().params().use_organ(organ.clone()).unwrap();
        c.resize(HostSize::new(520, 520));
        let window = manguebyte_gfx::Rect::new(0.0, 0.0, 520.0, 520.0);
        for stop in organ.stops() {
            let division = organ.division(stop.division).unwrap();
            let at = c.get(Role::Switch, &format!("{} {}", division.name, stop.name));
            assert!(
                window.contains(at.bounds.center()) && at.bounds.bottom() <= 520.0,
                "{}: {} at {:?}",
                organ.name,
                stop.name,
                at.bounds
            );
        }
        for name in ["Undo", "Models", "Organs", "Settings"] {
            let at = c.get(Role::Button, name).bounds;
            assert!(at.x >= 200.0 && at.right() <= 520.0, "{name} at {at:?}");
        }
    }
}

#[test]
fn pictures_of_it() {
    let mut c = Console::open();
    c.resize(HALF);
    c.picture("compact@1x");
    c.h.plugin_mut()
        .params()
        .use_organ(caecilia_model::french_romantic_organ())
        .unwrap();
    c.settle();
    c.picture("compact-french@1x");
    c.resize(HostSize::new(640, 680));
    c.picture("compact-french-small@1x");
    c.resize(HostSize::new(1000, 620));
    c.picture("compact-french-wide@1x");
    c.resize(HostSize::new(546, 580));
    c.picture("compact-french-laptop@1x");
    c.resize(HostSize::new(520, 520));
    c.picture("compact-french-smallest@1x");
    c.resize(HostSize::new(1280, 784));
    c.picture("zoomed@1x");
}
