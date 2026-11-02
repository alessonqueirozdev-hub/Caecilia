// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The console's last part: the organ library, the console laying itself
//! out again for another organ, the host's names following it, and the day's
//! light.

use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::Rc;

use caecilia_plugin::Caecilia;
use manguebyte_gui::HeadlessEditor;
use manguebyte_params::Param;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_plugin::editor::{
    EditorContext, EditorHost, EditorSpec, HostLink, OpenEditor, ParentWindow,
};
use manguebyte_plugin::{Harness, Plugin};
use manguebyte_ui::{AccessEntry, Role};

thread_local! {
    static OPENED: RefCell<Option<HeadlessEditor>> = const { RefCell::new(None) };
}

const CHAMBER: &str = r#"{
  "name": "Chamber organ",
  "windchests": [ { "name": "Main", "pressurePa": 650, "tremulant": true } ],
  "ranks": [
    { "name": "Stopped Diapason 8", "family": "Flute", "footage": 8, "windchest": "Main" },
    { "name": "Principal 4", "family": "Principal", "footage": 4, "windchest": "Main" },
    { "name": "Hautboy 8", "family": "Reed", "footage": 8, "windchest": "Main" }
  ],
  "divisions": [
    { "name": "Great", "midiChannel": 0, "manual": 1 },
    { "name": "Swell", "enclosed": true, "midiChannel": 1, "manual": 2 }
  ],
  "stops": [
    { "name": "Stopped Diapason 8", "family": "Flute", "division": "Great", "rank": "Stopped Diapason 8" },
    { "name": "Principal 4", "footage": 4, "pitchClass": "Octave", "role": "Chorus", "division": "Great", "rank": "Principal 4" },
    { "name": "Hautboy 8", "family": "Reed", "role": "Solo", "division": "Swell", "rank": "Hautboy 8" }
  ],
  "couplers": [ { "name": "Swell to Great", "from": "Swell", "to": "Great" } ]
}"#;

const POSITIVE: &str = r#"{
  "name": "Positive organ",
  "windchests": [ { "name": "Chest", "pressurePa": 520 } ],
  "ranks": [
    { "name": "Gedackt 8", "family": "Flute", "footage": 8, "windchest": "Chest" },
    { "name": "Principal 2", "family": "Principal", "footage": 2, "windchest": "Chest" }
  ],
  "divisions": [ { "name": "Manual", "midiChannel": 0, "manual": 1, "lowNote": 36, "highNote": 84 } ],
  "stops": [
    { "name": "Gedackt 8", "family": "Flute", "division": "Manual", "rank": "Gedackt 8" },
    { "name": "Principal 2", "footage": 2, "pitchClass": "Super", "role": "Chorus", "division": "Manual", "rank": "Principal 2" }
  ]
}"#;

/// A folder of organs of this test's own.
fn organs(test: &str) -> PathBuf {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR"))
        .join("organs")
        .join(test);
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("chamber.organ.json"), CHAMBER).unwrap();
    std::fs::write(dir.join("positive.organ.json"), POSITIVE).unwrap();
    dir
}

struct Console {
    h: Harness<Caecilia>,
    ui: HeadlessEditor,
    _editor: Box<dyn OpenEditor>,
    _link: HostLink,
}

impl Console {
    fn open(mut h: Harness<Caecilia>) -> Console {
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

    fn get(&self, role: Role, name: &str) -> AccessEntry {
        self.find(role, name)
            .unwrap_or_else(|| panic!("no {role:?} named {name}"))
    }

    fn settle(&mut self) {
        for _ in 0..4 {
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

    fn says(&self, name: &str) -> String {
        self.get(Role::Group, name).node.value.unwrap_or_default()
    }
}

fn harness_on(organ: &std::path::Path) -> Harness<Caecilia> {
    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    h.plugin_mut().params().load_organ_file(organ).unwrap();
    h
}

#[test]
fn the_library_changes_the_organ_and_the_console_follows() {
    let dir = organs("follows");
    let mut c = Console::open(harness_on(&dir.join("chamber.organ.json")));
    assert!(
        c.says("Title").starts_with("Chamber organ"),
        "{}",
        c.says("Title")
    );
    assert!(c.find(Role::Button, "Great C4").is_some());

    c.click(Role::Button, "Organs");
    let library = c.says("Wind");
    assert!(
        library.starts_with("Organs: Caecilia Pipe Organ"),
        "{library}"
    );
    assert!(library.contains("chamber (playing)"), "{library}");
    assert!(library.contains("positive"), "{library}");

    c.click(Role::Button, "Organ positive");
    assert!(
        c.says("Title").starts_with("Positive organ"),
        "{}",
        c.says("Title")
    );
    assert_eq!(c.says("Left jamb"), "Gedackt 8, Principal 2 drawn");
    // One manual now, and its compass: no Great, no Swell, no C7.
    assert!(c.find(Role::Button, "Manual C4").is_some());
    assert!(c.find(Role::Button, "Manual C7").is_none());
    assert!(c.find(Role::Button, "Great C4").is_none());
    // The library closed; the wind is the positive's.
    assert!(c.says("Wind").starts_with("Manual"), "{}", c.says("Wind"));
    // The host's names follow the organ.
    let params = c.h.plugin_mut().params();
    assert_eq!(&*params.stops[0].info().display_name(), "Manual Gedackt 8");
    assert_eq!(&*params.stops[2].info().display_name(), "Stop 02 (unused)");

    // And back to the demonstration organ.
    c.click(Role::Button, "Organs");
    c.click(Role::Button, "Organ Caecilia Pipe Organ");
    assert!(c.find(Role::Button, "R\u{e9}cit C4").is_some());
    assert_eq!(
        &*params.stops[0].info().display_name(),
        "P\u{e9}dale Contrebasse 16"
    );
}

#[test]
fn an_organ_file_with_an_error_says_so_and_changes_nothing() {
    let dir = organs("error");
    std::fs::write(dir.join("broken.organ.json"), "{ \"name\": \"Broken\", ").unwrap();
    let mut c = Console::open(harness_on(&dir.join("chamber.organ.json")));
    c.click(Role::Button, "Organs");
    c.click(Role::Button, "Organ broken");
    let library = c.says("Wind");
    assert!(library.contains("broken:"), "{library}");
    assert!(c.says("Title").starts_with("Chamber organ"));
}

#[test]
fn the_day_s_light_is_chosen_and_the_session_keeps_it() {
    let mut c = Console::open(Harness::<Caecilia>::new(48000.0, 256));
    assert!(c.says("Title").ends_with("night's light"));
    c.click(Role::Button, "Day mode");
    assert!(
        c.says("Title").ends_with("day's light"),
        "{}",
        c.says("Title")
    );
    assert!(c.find(Role::Button, "Night mode").is_some());
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-day@1x.png"), png).unwrap();

    let state = c.h.save_state();
    let again = Harness::<Caecilia>::new(48000.0, 256);
    again.load_state(&state).unwrap();
    assert!(again.params().console_day());
}
