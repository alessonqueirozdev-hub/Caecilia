//! The console, opened headless through the plugin as a host opens it,
//! showing what plays.

use std::cell::RefCell;
use std::rc::Rc;

use caecilia_model::read_organ;
use caecilia_plugin::{Caecilia, EDITOR_SIZE, stop_param_id};
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_gfx::IRect;
use manguebyte_gui::HeadlessEditor;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_plugin::editor::{
    EditorContext, EditorHost, EditorSpec, HostLink, OpenEditor, ParentWindow,
};
use manguebyte_plugin::{Harness, Plugin};
use manguebyte_ui::Role;

thread_local! {
    static OPENED: RefCell<Option<HeadlessEditor>> = const { RefCell::new(None) };
}

/// The console of the plugin in `h`, open and shown, at `scale`.
fn open(h: &mut Harness<Caecilia>, scale: f64) -> (Box<dyn OpenEditor>, HostLink, HeadlessEditor) {
    let spec = h
        .plugin_mut()
        .editor_spec()
        .on_headless_open(|ui| OPENED.with(|slot| *slot.borrow_mut() = Some(ui)));
    let host = Rc::new(RecordingEditorHost::new().with_params(h.plugin_mut().params()));
    let host: Rc<dyn EditorHost> = host;
    let (context, link) = EditorContext::attach(host);
    let parent = ParentWindow::win32(0x10).expect("not null");
    let mut editor = spec.open(parent, Some(scale), context).expect("opens");
    let ui = OPENED
        .with(|slot| slot.borrow_mut().take())
        .expect("the hook ran");
    editor.set_visible(true);
    ui.tick();
    (editor, link, ui)
}

/// What the part named `name` says it shows.
fn says(ui: &HeadlessEditor, name: &str) -> String {
    ui.find(Role::Group, name)
        .unwrap_or_else(|| panic!("no part named {name}"))
        .node
        .value
        .unwrap_or_default()
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

/// Where the pictures go: the target's scratch folder.
fn pictures() -> std::path::PathBuf {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("console");
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn the_console_shows_what_the_organ_plays() {
    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    assert_eq!(
        h.editor().expect("Caecilia has a console").size(1.0),
        EDITOR_SIZE
    );
    let (editor, link, ui) = open(&mut h, 1.0);
    assert_eq!(editor.size(), EDITOR_SIZE);

    // Every part is in the window.
    let window = IRect::new(0, 0, EDITOR_SIZE.width, EDITOR_SIZE.height);
    for name in [
        "Left jamb",
        "Right jamb",
        "Registration",
        "Wind",
        "Generals",
        "Shoes",
        "P\u{e9}dalier",
    ] {
        let part = ui
            .find(Role::Group, name)
            .unwrap_or_else(|| panic!("{name}"));
        assert!(
            window.contains_rect(&part.device),
            "{name}: {:?}",
            part.device
        );
    }
    // It opens on the demonstration organ's opening registration.
    assert!(
        says(&ui, "Left jamb").contains("Montre 8"),
        "{}",
        says(&ui, "Left jamb")
    );

    // Program change 4 recalls General 4; a chord on the Grand-Orgue, the
    // low C on the pedals, and the Récit's Hautbois drawn by hand.
    h.schedule(
        h.position(),
        EventKind::Midi(manguebyte_midi::MidiMessage::ProgramChange {
            channel: 0,
            program: 3,
        }),
    );
    for key in [55, 60, 64] {
        note_on(&mut h, 0, key);
    }
    note_on(&mut h, 2, 36);
    h.render(4800);
    let hautboy = h
        .plugin_mut()
        .organ()
        .stops()
        .iter()
        .position(|s| s.name == "Basson-Hautbois 8")
        .unwrap();
    h.automate(h.position(), &stop_param_id(hautboy), 1.0);
    h.render(4800);
    ui.tick();

    assert_eq!(says(&ui, "Generals"), "General 4 recalled");
    assert!(
        says(&ui, "Right jamb").contains("Basson-Hautbois 8"),
        "{}",
        says(&ui, "Right jamb")
    );
    assert_eq!(says(&ui, "P\u{e9}dalier"), "C2 held");
    let registration = says(&ui, "Registration");
    assert!(registration.contains("General 4 (mf)"), "{registration}");
    assert!(registration.contains("+ Basson-Hautbois"), "{registration}");
    // The wind of the Grand-Orgue's chest gives under the chord.
    let wind = says(&ui, "Wind");
    assert!(wind.contains("Grand-Orgue"), "{wind}");

    let png = ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(pictures().join("console-playing@1x.png"), png).unwrap();

    drop(editor);
    drop(link);
    assert!(!ui.is_open());
}

#[test]
fn the_console_draws_at_twice_the_scale() {
    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    for key in [48, 52, 55] {
        note_on(&mut h, 0, key);
    }
    h.render(2400);
    let (editor, link, ui) = open(&mut h, 2.0);
    assert_eq!(editor.size(), EDITOR_SIZE.scale_by(2.0));
    let png = ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(pictures().join("console@2x.png"), png).unwrap();
    drop(editor);
    drop(link);
}

/// A two-manual chamber organ without pedals.
const CHAMBER: &str = r#"{
  "name": "Chamber organ",
  "windchests": [ { "name": "Main", "pressurePa": 650, "tremulant": true } ],
  "ranks": [
    { "name": "Stopped Diapason 8", "family": "Flute", "footage": 8, "windchest": "Main" },
    { "name": "Principal 4", "family": "Principal", "footage": 4, "windchest": "Main" },
    { "name": "Fifteenth 2", "family": "Principal", "footage": 2, "windchest": "Main" },
    { "name": "Hautboy 8", "family": "Reed", "footage": 8, "windchest": "Main" }
  ],
  "divisions": [
    { "name": "Great", "midiChannel": 0, "manual": 1 },
    { "name": "Swell", "enclosed": true, "lowNote": 48, "midiChannel": 1, "manual": 2 }
  ],
  "stops": [
    { "name": "Stopped Diapason 8", "family": "Flute", "division": "Great", "rank": "Stopped Diapason 8" },
    { "name": "Principal 4", "footage": 4, "pitchClass": "Octave", "role": "Chorus", "division": "Great", "rank": "Principal 4" },
    { "name": "Fifteenth 2", "footage": 2, "pitchClass": "Super", "role": "Chorus", "division": "Great", "rank": "Fifteenth 2" },
    { "name": "Hautboy 8", "family": "Reed", "role": "Solo", "division": "Swell", "rank": "Hautboy 8" }
  ],
  "couplers": [ { "name": "Swell to Great", "from": "Swell", "to": "Great" } ]
}"#;

#[test]
fn another_organ_lays_out_its_own_console() {
    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    let organ = read_organ(CHAMBER, None).unwrap().organ;
    h.plugin_mut().params().use_organ(organ).unwrap();
    h.render(4800);
    let (editor, link, ui) = open(&mut h, 1.0);
    assert_eq!(
        says(&ui, "Left jamb"),
        "Stopped Diapason 8, Principal 4, Fifteenth 2 drawn"
    );
    assert_eq!(says(&ui, "Right jamb"), "nothing drawn");
    assert_eq!(says(&ui, "P\u{e9}dalier"), "no pedalboard");
    assert!(
        says(&ui, "Wind").starts_with("Great"),
        "{}",
        says(&ui, "Wind")
    );
    let png = ui.pixels().expect("drawn").encode_png().unwrap();
    std::fs::write(pictures().join("console-chamber@1x.png"), png).unwrap();
    drop(editor);
    drop(link);
}
