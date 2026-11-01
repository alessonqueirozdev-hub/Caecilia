//! The sound panel: the level, the reverb and its room, the master EQ, each
//! moved from the console as a gesture the host records.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::Caecilia;
use manguebyte_gfx::Point;
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
    c.click(Role::Button, "Sound");
    assert_eq!(
        c.says("Wind"),
        "Sound: volume +0.0 dB, reverb 25 % in the hall; EQ on: Warmth +2.0 dB, \
         Boxiness -2.5 dB, Body +0.0 dB, Presence +1.5 dB, Air +2.0 dB"
    );
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    let png = c.ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(dir.join("console-sound@1x.png"), png).unwrap();
}

#[test]
fn every_control_moves_its_parameter() {
    let mut c = Console::open();
    c.click(Role::Button, "Sound");
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
