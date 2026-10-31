//! Playing the console: drawstops, pistons, shoes, keys and pedals, through
//! the editor opened headless as a host opens it.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_plugin::{Caecilia, stop_param_id};
use manguebyte_gfx::Point;
use manguebyte_gui::HeadlessEditor;
use manguebyte_params::{Param, ParamId};
use manguebyte_plugin::editor::testing::{HostCall, RecordingEditorHost};
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
    host: Rc<RecordingEditorHost>,
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
        let host = Rc::new(RecordingEditorHost::new().with_params(h.plugin_mut().params()));
        let host_dyn: Rc<dyn EditorHost> = host.clone();
        let (context, link) = EditorContext::attach(host_dyn);
        let parent = ParentWindow::win32(0x10).expect("not null");
        let mut editor = spec.open(parent, Some(1.0), context).expect("opens");
        let ui = OPENED.with(|slot| slot.borrow_mut().take()).expect("hook");
        editor.set_visible(true);
        ui.tick();
        Console {
            h,
            host,
            ui,
            _editor: editor,
            _link: link,
        }
    }

    /// The one control or part with this role and name.
    fn get(&self, role: Role, name: &str) -> AccessEntry {
        self.ui
            .find(role, name)
            .unwrap_or_else(|| panic!("no {role:?} named {name}"))
    }

    fn click(&mut self, role: Role, name: &str) {
        let at = self.get(role, name).bounds.center();
        self.ui.press(at);
        self.ui.release(at);
        self.settle();
    }

    /// A block of audio, then a frame.
    fn settle(&mut self) {
        self.h.render(256);
        self.ui.tick();
    }

    fn says(&self, name: &str) -> String {
        self.get(Role::Group, name).node.value.unwrap_or_default()
    }

    fn stop(&mut self, name: &str) -> usize {
        self.h
            .plugin_mut()
            .organ()
            .stops()
            .iter()
            .position(|s| s.name == name)
            .unwrap()
    }

    fn drawn(&self) -> u64 {
        self.h.params().stop_mask()
    }
}

#[test]
fn a_drawstop_drawn_by_hand_says_so_and_the_host_records_it() {
    let mut c = Console::open();
    let hautboy = c.stop("Basson-Hautbois 8");
    let param = ParamId::from_name(&stop_param_id(hautboy));
    c.host.take_calls();

    c.click(Role::Switch, "R\u{e9}cit Basson-Hautbois 8");
    assert!(c.drawn() & (1 << hautboy) != 0, "drawn");
    assert_eq!(
        c.host.take_calls(),
        [
            HostCall::BeginGesture(param),
            HostCall::Perform(param, 1.0),
            HostCall::EndGesture(param)
        ],
        "one edit the host can record"
    );
    let registration = c.says("Registration");
    assert!(
        registration.contains("Basson-Hautbois 8 drawn by hand"),
        "{registration}"
    );
    let switch = c.get(Role::Switch, "R\u{e9}cit Basson-Hautbois 8");
    assert_eq!(switch.node.state.checked, Some(true));

    // Again, and it goes back in.
    c.click(Role::Switch, "R\u{e9}cit Basson-Hautbois 8");
    assert!(c.drawn() & (1 << hautboy) == 0, "retired");

    // A coupler and the tremulant are tablets.
    c.click(Role::Switch, "R\u{e9}cit/Grand-Orgue");
    assert!(c.h.params().coupler_mask() & 1 != 0);
    c.click(Role::Switch, "Tremulant");
    assert!(c.h.params().tremulant.value());
    assert!(c.says("Right jamb").contains("Tremulant"));
}

#[test]
fn pistons_recall_set_stores_and_cancel_clears() {
    let mut c = Console::open();
    let general = |c: &Console, n: usize| c.h.params().console.general(n).unwrap();

    c.click(Role::Button, "General 4");
    assert_eq!(c.drawn(), general(&c, 3).0);
    assert_eq!(c.says("Generals"), "General 4 recalled");

    // Next steps along the row; the crescendo shoe follows.
    c.click(Role::Button, "Next general");
    assert_eq!(c.says("Generals"), "General 5 recalled");
    assert_eq!(c.drawn(), general(&c, 4).0);

    // Set, then General 12: what is drawn goes into it.
    let drawn = c.drawn();
    c.click(Role::Button, "Set");
    assert!(c.says("Generals").contains("Set pressed"));
    c.click(Role::Button, "General 12");
    assert_eq!(general(&c, 11).0, drawn);
    assert!(!c.says("Generals").contains("Set pressed"));

    // Cancel retires everything, and undo brings it back.
    c.click(Role::Button, "Cancel");
    assert_eq!(c.drawn(), 0);
    assert!(c.says("Registration").contains("Cancel"));
    c.click(Role::Button, "Undo");
    assert_eq!(c.drawn(), drawn);
    c.click(Role::Button, "Redo");
    assert_eq!(c.drawn(), 0);

    // A toe stud is a general for the feet.
    c.click(Role::Button, "Toe stud 2");
    assert_eq!(c.drawn(), general(&c, 1).0);
}

#[test]
fn the_shoes_open_the_swell_and_crescendo_through_the_generals() {
    let mut c = Console::open();
    let swell = c.get(Role::Slider, "R\u{e9}cit").bounds;
    let swell_id = c.h.params().swell.param_id();
    c.host.take_calls();
    // Shut, then halfway: one gesture.
    c.ui.press(Point::new(swell.center().x, swell.bottom() - 1.0));
    c.ui.move_to(swell.center());
    c.ui.release(swell.center());
    c.settle();
    let value = c.h.params().swell.value();
    assert!((value - 0.5).abs() < 0.05, "{value}");
    let calls = c.host.take_calls();
    assert_eq!(calls.first(), Some(&HostCall::BeginGesture(swell_id)));
    assert_eq!(calls.last(), Some(&HostCall::EndGesture(swell_id)));

    // The crescendo, pushed all the way: the tutti's general and beyond.
    let crescendo = c.get(Role::Slider, "Crescendo").bounds;
    c.ui.press(Point::new(crescendo.center().x, crescendo.bottom() - 1.0));
    c.ui.move_to(Point::new(crescendo.center().x, crescendo.y + 1.0));
    c.ui.release(Point::new(crescendo.center().x, crescendo.y + 1.0));
    c.settle();
    assert_eq!(c.says("Generals"), "General 8 recalled");
}

#[test]
fn keys_and_pedals_played_on_the_console_sound() {
    let mut c = Console::open();
    // The pedal's 16' first, by hand.
    c.click(Role::Switch, "P\u{e9}dale Soubasse 16");
    let pedal = c.get(Role::Button, "Pedal C2").bounds.center();
    c.ui.press(pedal);
    c.settle();
    c.settle();
    assert_eq!(c.says("P\u{e9}dalier"), "C2 held");
    assert!(c.h.plugin_mut().engine().unwrap().active_voices() > 0);
    c.ui.release(pedal);
    c.settle();
    c.settle();
    assert_eq!(c.says("P\u{e9}dalier"), "no pedal held");

    // Middle C on the lower manual, the Grand-Orgue: both manuals have a
    // C4, the Récit's above.
    let mut c4: Vec<AccessEntry> =
        c.ui.access_tree()
            .into_iter()
            .filter(|e| e.node.role == Role::Button && e.node.name == "C4")
            .collect();
    c4.sort_by(|a, b| a.bounds.y.total_cmp(&b.bounds.y));
    let lower = c4.last().expect("a C4").bounds;
    let at = Point::new(lower.center().x, lower.y + lower.height * 0.85);
    c.ui.press(at);
    c.settle();
    c.settle();
    let telemetry = c.h.plugin_mut().telemetry();
    assert!(
        telemetry.held(0) & (1 << 60) != 0,
        "C4 held on the Grand-Orgue"
    );
    c.ui.release(at);
    c.settle();
    c.settle();
    assert_eq!(telemetry.held(0) & (1 << 60), 0);
}
