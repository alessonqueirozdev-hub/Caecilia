//! The sound panel on the music desk, in three pages: the sound (the level,
//! the reverb and its room, the master EQ), the tuning (temperament, its
//! key, the pitch of A, the tremulant), and MIDI learn. Each control moves
//! its parameter as a gesture, so a host records it as it would its own
//! knob.

use manguebyte_gfx::{Circle, Color, Point, Rect, RoundedRect, Scene, Shadow, Size, Stop, Stroke};
use manguebyte_params::Param;
use manguebyte_plugin::editor::{EditorContext, Gesture};
use manguebyte_ui::access::Role;
use manguebyte_ui::{Button, Event, EventCx, Handled};

use super::live::Item;
use super::paint::{style, text, vertical, width_of};
use super::snapshot::{Snapshot, Source, note_name};
use crate::console::{Action, Source as Control};
use crate::params::{KEY_NAMES, TEMPERAMENT_NAMES};
use crate::{EQ_BANDS, ROOM_NAMES};

/// The pages, in the order of their tabs.
pub(crate) const PAGES: [&str; 3] = ["Sound", "Tuning", "MIDI"];
pub(crate) const TUNING: usize = 1;
pub(crate) const MIDI: usize = 2;

/// The temperaments as their chips name them.
const TEMPERAMENT_CHIPS: [&str; 8] = [
    "Equal",
    "Meantone \u{bc}",
    "Werckmeister III",
    "Kirnberger III",
    "Vallotti",
    "Young",
    "Pythagorean",
    "Just",
];

/// The pitches of A a player reaches for.
const PITCHES: [f32; 4] = [392.0, 415.0, 440.0, 466.0];

/// What MIDI learn shows: whether it is on, what it waits to bind, what it
/// bound last, and every control learned.
#[derive(Clone, PartialEq, Hash, Default)]
pub(crate) struct LearnView {
    pub(crate) on: bool,
    pub(crate) waiting_for: Option<String>,
    pub(crate) last: Option<String>,
    /// Each binding: the control, and what it does.
    pub(crate) bindings: Vec<(String, String)>,
}

/// What the sound panel shows, read once a frame.
#[derive(Clone, PartialEq)]
pub(crate) struct SoundView {
    pub(crate) page: usize,
    pub(crate) level_db: f32,
    pub(crate) level: f32,
    pub(crate) reverb: f32,
    pub(crate) room: usize,
    pub(crate) eq: bool,
    pub(crate) bands_db: [f32; 5],
    pub(crate) bands: [f32; 5],
    pub(crate) temperament: usize,
    pub(crate) key: usize,
    pub(crate) pitch_hz: f32,
    pub(crate) pitch: f32,
    pub(crate) rate_hz: f32,
    pub(crate) rate: f32,
    pub(crate) depth: f32,
    pub(crate) learn: LearnView,
}

impl std::hash::Hash for SoundView {
    fn hash<H: std::hash::Hasher>(&self, h: &mut H) {
        self.page.hash(h);
        for v in [self.level, self.reverb, self.pitch, self.rate, self.depth]
            .iter()
            .chain(&self.bands)
        {
            v.to_bits().hash(h);
        }
        (self.room, self.eq, self.temperament, self.key).hash(h);
        self.learn.hash(h);
    }
}

impl SoundView {
    pub(crate) fn read(source: &Source, page: usize, learn: LearnView) -> SoundView {
        let p = &source.shared().params;
        SoundView {
            page,
            level_db: p.level.value(),
            level: p.level.normalized() as f32,
            reverb: p.reverb.value(),
            room: p.room.index(),
            eq: p.eq.value(),
            bands_db: std::array::from_fn(|i| p.eq_bands[i].value()),
            bands: std::array::from_fn(|i| p.eq_bands[i].normalized() as f32),
            temperament: p.temperament.index(),
            key: p.temperament_key.index(),
            pitch_hz: p.pitch.value(),
            pitch: p.pitch.normalized() as f32,
            rate_hz: p.tremulant_rate.value(),
            rate: p.tremulant_rate.normalized() as f32,
            depth: p.tremulant_depth.value(),
            learn,
        }
    }
}

/// What a learned control is called: `CC 20 · ch 1`, `C2 · ch 3`.
pub(crate) fn control_name(control: Control) -> String {
    match control {
        Control::Controller { channel, number } => format!("CC {number} \u{b7} ch {}", channel + 1),
        Control::Key { channel, note } => format!("{} \u{b7} ch {}", note_name(note), channel + 1),
    }
}

/// What a learned action is called on `organ`.
pub(crate) fn action_name(organ: &caecilia_model::Organ, action: Action) -> String {
    match action {
        Action::ToggleStop(n) => organ
            .stops()
            .get(usize::from(n))
            .map_or_else(|| format!("Stop {n}"), |s| s.name.clone()),
        Action::ToggleCoupler(n) => organ
            .couplers()
            .get(usize::from(n))
            .map_or_else(|| format!("Coupler {n}"), |c| c.name.clone()),
        Action::General(n) => format!("General {}", n + 1),
        Action::NextGeneral => "Next general".into(),
        Action::PreviousGeneral => "Previous general".into(),
        Action::Cancel => "Cancel".into(),
    }
}

// ------------------------------------------------------------------ plan

const LABEL: f32 = 92.0;
const ROW: f32 = 24.0;

/// A row of chips from `x` to the panel's edge, `n` of them.
fn chips(size: Size, y: f32, n: usize) -> Vec<Rect> {
    let x = 20.0 + LABEL;
    let w = (size.width - x - 20.0) / n as f32;
    (0..n)
        .map(|i| Rect::new(x + i as f32 * w, y, w - 5.0, ROW))
        .collect()
}

fn track(size: Size, y: f32) -> Rect {
    let x = 20.0 + LABEL;
    Rect::new(x, y, (size.width - x - 80.0).max(40.0), ROW)
}

fn tabs(size: Size) -> Vec<Rect> {
    let mut x = size.width - 20.0;
    let s = style(11.5, 600.0, Color::hex(0));
    let mut out: Vec<Rect> = PAGES
        .iter()
        .rev()
        .map(|p| {
            let w = width_of(p, s) + 24.0;
            x -= w;
            let r = Rect::new(x, 12.0, w, 24.0);
            x -= 6.0;
            r
        })
        .collect();
    out.reverse();
    out
}

struct SoundPlan {
    level: Rect,
    reverb: Rect,
    rooms: Vec<Rect>,
    eq: Rect,
    /// Each fader's track, and the column it stands in.
    faders: Vec<(Rect, Rect)>,
}

fn sound_plan(size: Size) -> SoundPlan {
    let top = 192.0;
    let bottom = (size.height - 40.0).max(top + 24.0);
    let col = (size.width - 40.0) / EQ_BANDS.len() as f32;
    SoundPlan {
        level: track(size, 52.0),
        reverb: track(size, 84.0),
        rooms: chips(size, 118.0, ROOM_NAMES.len()),
        eq: Rect::new(20.0 + LABEL, 156.0, 58.0, ROW),
        faders: (0..EQ_BANDS.len())
            .map(|i| {
                let column = Rect::new(20.0 + i as f32 * col, top - 8.0, col, bottom - top + 36.0);
                let track = Rect::new(column.center().x - 2.0, top, 4.0, bottom - top);
                (track, column)
            })
            .collect(),
    }
}

struct TuningPlan {
    temperaments: Vec<Rect>,
    keys: Vec<Rect>,
    pitch: Rect,
    pitches: Vec<Rect>,
    rate: Rect,
    depth: Rect,
}

fn tuning_plan(size: Size) -> TuningPlan {
    let mut temperaments = chips(size, 50.0, 4);
    temperaments.extend(chips(size, 78.0, 4));
    let x = 20.0 + LABEL;
    let pw = 46.0;
    let pitch = Rect::new(
        x,
        146.0,
        (size.width - x - 20.0 - 4.0 * (pw + 4.0)).max(40.0),
        ROW,
    );
    let pitches = (0..PITCHES.len())
        .map(|i| {
            Rect::new(
                pitch.right() + 10.0 + i as f32 * (pw + 4.0),
                146.0,
                pw - 4.0,
                ROW,
            )
        })
        .collect();
    TuningPlan {
        temperaments,
        keys: chips(size, 112.0, KEY_NAMES.len()),
        pitch: Rect::new(pitch.x, pitch.y, pitch.width - 60.0, ROW),
        pitches,
        rate: track(size, 190.0),
        depth: track(size, 222.0),
    }
}

struct MidiPlan {
    learn: Rect,
    forget_all: Rect,
    /// Each binding's row and its forget mark.
    rows: Vec<(Rect, Rect)>,
}

fn midi_plan(size: Size, count: usize) -> MidiPlan {
    let top = 118.0;
    let room = ((size.height - top - 16.0) / 26.0).max(0.0) as usize;
    MidiPlan {
        learn: Rect::new(20.0, 52.0, 84.0, 26.0),
        forget_all: Rect::new(size.width - 20.0 - 90.0, 52.0, 90.0, 26.0),
        rows: (0..count.min(room))
            .map(|i| {
                let r = Rect::new(20.0, top + i as f32 * 26.0, size.width - 40.0, 22.0);
                (r, Rect::new(r.right() - 26.0, r.y, 26.0, r.height))
            })
            .collect(),
    }
}

// --------------------------------------------------------------- drawing

fn knob(scene: &mut Scene, snapshot: &Snapshot, c: Point) {
    let look = snapshot.look;
    let stops = [
        Stop::new(0.0, look.brass_light),
        Stop::new(1.0, look.brass_dark),
    ];
    scene.shadow(
        Circle::new(c, 8.0),
        Shadow::new(Color::hex_rgba(0x0000_0090))
            .with_offset(0.0, 2.0)
            .with_blur(4.0),
    );
    scene.fill(
        Circle::new(c, 8.0),
        vertical(Rect::new(c.x - 8.0, c.y - 8.0, 16.0, 16.0), &stops),
    );
    scene.fill(Circle::new(c, 2.5), look.brass_dark);
}

fn label(scene: &mut Scene, snapshot: &Snapshot, y: f32, name: &str) {
    text(
        scene,
        name,
        style(12.0, 600.0, snapshot.look.text),
        Point::new(20.0, y + 4.0),
        0.0,
    );
}

/// A horizontal slider: its label, its track filled to `at` (0 to 1), and
/// what it reads.
fn slider(scene: &mut Scene, snapshot: &Snapshot, r: Rect, name: &str, at: f32, reads: &str) {
    let look = snapshot.look;
    label(scene, snapshot, r.y, name);
    let y = r.center().y;
    scene.fill(
        RoundedRect::new(Rect::new(r.x, y - 2.0, r.width, 4.0), 2.0),
        look.edge,
    );
    let x = r.x + r.width * at.clamp(0.0, 1.0);
    scene.fill(
        RoundedRect::new(Rect::new(r.x, y - 2.0, x - r.x, 4.0), 2.0),
        look.accent,
    );
    knob(scene, snapshot, Point::new(x, y));
    text(
        scene,
        reads,
        style(11.5, 600.0, look.text2),
        Point::new(r.right() + 60.0, r.y + 5.0),
        1.0,
    );
}

/// A chip, lit when `on`.
fn chip(scene: &mut Scene, snapshot: &Snapshot, r: Rect, name: &str, on: bool) {
    let look = snapshot.look;
    let shape = RoundedRect::new(r, r.height / 2.0);
    if on {
        scene.fill(shape, look.accent.with_alpha(0x40));
        scene.stroke(shape, Stroke::new(1.0), look.accent);
    } else {
        scene.fill(shape, look.edge.with_alpha(0x60));
    }
    let size = if width_of(name, style(11.5, 500.0, look.text)) > r.width - 8.0 {
        10.0
    } else {
        11.5
    };
    text(
        scene,
        name,
        style(size, if on { 650.0 } else { 500.0 }, look.text),
        Point::new(r.center().x, r.y + (r.height - size * 1.25) / 2.0),
        0.5,
    );
}

fn draw_sound(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let plan = sound_plan(size);
    slider(
        scene,
        snapshot,
        plan.level,
        "Volume",
        sound.level,
        &format!("{:+.1} dB", sound.level_db),
    );
    slider(
        scene,
        snapshot,
        plan.reverb,
        "Reverb",
        sound.reverb,
        &format!("{:.0} %", sound.reverb * 100.0),
    );
    label(scene, snapshot, plan.rooms[0].y + 1.0, "Room");
    for (i, r) in plan.rooms.iter().enumerate() {
        chip(scene, snapshot, *r, ROOM_NAMES[i], i == sound.room);
    }
    label(scene, snapshot, plan.eq.y, "EQ");
    chip(
        scene,
        snapshot,
        plan.eq,
        if sound.eq { "On" } else { "Off" },
        sound.eq,
    );
    text(
        scene,
        "the organ's voicing, \u{b1}18 dB",
        style(10.5, 450.0, look.muted),
        Point::new(plan.eq.right() + 12.0, plan.eq.y + 5.0),
        0.0,
    );
    let ink = if sound.eq { look.text } else { look.muted };
    for (i, (track, column)) in plan.faders.iter().enumerate() {
        scene.fill(RoundedRect::new(*track, 2.0), look.edge);
        // The flat line, 0 dB, across the middle.
        let mid = track.center().y;
        scene.fill(
            Rect::new(track.x - 7.0, mid - 0.5, track.width + 14.0, 1.0),
            look.muted,
        );
        let y = track.bottom() - track.height * sound.bands[i];
        let cap = Rect::new(track.center().x - 10.0, y - 5.0, 20.0, 10.0);
        let stops = [
            Stop::new(0.0, look.brass_light),
            Stop::new(1.0, look.brass_dark),
        ];
        scene.fill(RoundedRect::new(cap, 3.0), vertical(cap, &stops));
        scene.fill(
            Rect::new(cap.x + 3.0, cap.center().y - 0.5, cap.width - 6.0, 1.0),
            look.brass_dark,
        );
        text(
            scene,
            EQ_BANDS[i].name,
            style(11.0, 600.0, ink),
            Point::new(column.center().x, track.bottom() + 8.0),
            0.5,
        );
        text(
            scene,
            &format!("{:+.1}", sound.bands_db[i]),
            style(10.0, 500.0, look.text2),
            Point::new(column.center().x + 26.0, track.y - 4.0),
            0.5,
        );
    }
}

fn draw_tuning(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let plan = tuning_plan(size);
    label(scene, snapshot, plan.temperaments[0].y, "Temperament");
    for (i, r) in plan.temperaments.iter().enumerate() {
        chip(
            scene,
            snapshot,
            *r,
            TEMPERAMENT_CHIPS[i],
            i == sound.temperament,
        );
    }
    label(scene, snapshot, plan.keys[0].y, "Key");
    for (i, r) in plan.keys.iter().enumerate() {
        chip(scene, snapshot, *r, KEY_NAMES[i], i == sound.key);
    }
    slider(
        scene,
        snapshot,
        plan.pitch,
        "Pitch of A",
        sound.pitch,
        &format!("{:.1} Hz", sound.pitch_hz),
    );
    for (r, hz) in plan.pitches.iter().zip(PITCHES) {
        chip(
            scene,
            snapshot,
            *r,
            &format!("{hz:.0}"),
            (sound.pitch_hz - hz).abs() < 0.05,
        );
    }
    slider(
        scene,
        snapshot,
        plan.rate,
        "Tremulant",
        sound.rate,
        &format!("{:.2} Hz", sound.rate_hz),
    );
    slider(
        scene,
        snapshot,
        plan.depth,
        "Depth",
        sound.depth,
        &format!("{:.0} %", sound.depth * 100.0),
    );
}

fn draw_midi(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let learn = &sound.learn;
    let plan = midi_plan(size, learn.bindings.len());
    chip(scene, snapshot, plan.learn, "Learn", learn.on);
    if !learn.bindings.is_empty() {
        chip(scene, snapshot, plan.forget_all, "Forget all", false);
    }
    let say = match (learn.on, &learn.waiting_for) {
        (true, Some(what)) => format!("Move a key, pedal or controller for {what}"),
        (true, None) => "Choose a stop, coupler or piston on the console".to_string(),
        (false, _) => "Learn binds your MIDI controls to the console's".to_string(),
    };
    text(
        scene,
        &say,
        style(12.0, 600.0, if learn.on { look.accent } else { look.text }),
        Point::new(plan.learn.right() + 14.0, plan.learn.y + 5.0),
        0.0,
    );
    let below = learn.last.clone().unwrap_or_else(|| {
        "Press Learn, then a stop, coupler or piston, then the control to bind to it.".into()
    });
    text(
        scene,
        &below,
        style(10.5, 450.0, look.muted),
        Point::new(20.0, 88.0),
        0.0,
    );
    if learn.bindings.is_empty() {
        text(
            scene,
            "No control learned yet.",
            style(11.5, 500.0, look.text2),
            Point::new(20.0, 120.0),
            0.0,
        );
    }
    for ((r, x), (control, action)) in plan.rows.iter().zip(&learn.bindings) {
        scene.fill(RoundedRect::new(*r, 6.0), look.edge.with_alpha(0x60));
        text(
            scene,
            control,
            style(11.5, 600.0, look.text),
            Point::new(r.x + 12.0, r.y + 4.0),
            0.0,
        );
        text(
            scene,
            &format!("\u{2192}  {action}"),
            style(11.5, 500.0, look.text2),
            Point::new(r.x + 150.0, r.y + 4.0),
            0.0,
        );
        text(
            scene,
            "\u{d7}",
            style(13.0, 600.0, look.muted),
            Point::new(x.center().x, x.y + 2.0),
            0.5,
        );
    }
}

/// The sound panel, on its page.
pub(crate) fn draw(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    text(
        scene,
        PAGES[sound.page],
        style(13.0, 650.0, look.text),
        Point::new(20.0, 16.0),
        0.0,
    );
    for (i, r) in tabs(size).iter().enumerate() {
        chip(scene, snapshot, *r, PAGES[i], i == sound.page);
    }
    match sound.page {
        TUNING => draw_tuning(size, scene, snapshot, sound),
        MIDI => draw_midi(size, scene, snapshot, sound),
        _ => draw_sound(size, scene, snapshot, sound),
    }
}

/// What the screen reader says of the panel.
pub(crate) fn describe(sound: &SoundView) -> String {
    match sound.page {
        TUNING => format!(
            "Tuning: {} on {}, A {:.1} Hz; tremulant {:.2} Hz, depth {:.0} %",
            TEMPERAMENT_NAMES[sound.temperament],
            KEY_NAMES[sound.key],
            sound.pitch_hz,
            sound.rate_hz,
            sound.depth * 100.0
        ),
        MIDI => {
            let learn = &sound.learn;
            let mut said = match (learn.on, &learn.waiting_for) {
                (true, Some(what)) => format!("MIDI: learning, waiting for a control for {what}"),
                (true, None) => "MIDI: learning, choose a control on the console".into(),
                (false, _) => "MIDI".into(),
            };
            if let Some(last) = &learn.last {
                said.push_str(&format!("; {last}"));
            }
            let bindings: Vec<String> = learn
                .bindings
                .iter()
                .map(|(c, a)| format!("{c} to {a}"))
                .collect();
            if bindings.is_empty() {
                said.push_str("; no control learned");
            } else {
                said.push_str(&format!("; {}", bindings.join(", ")));
            }
            said
        }
        _ => {
            let bands: Vec<String> = EQ_BANDS
                .iter()
                .zip(sound.bands_db)
                .map(|(b, db)| format!("{} {db:+.1} dB", b.name))
                .collect();
            format!(
                "Sound: volume {:+.1} dB, reverb {:.0} % in the {}; EQ {}: {}",
                sound.level_db,
                sound.reverb * 100.0,
                ROOM_NAMES[sound.room].to_lowercase(),
                if sound.eq { "on" } else { "off" },
                bands.join(", ")
            )
        }
    }
}

fn button(name: String, checked: Option<bool>, rect: Rect) -> Item {
    Item {
        role: Role::Button,
        name,
        checked,
        rect,
    }
}

fn slider_item(name: &str, rect: Rect) -> Item {
    Item {
        role: Role::Slider,
        name: name.into(),
        checked: None,
        rect,
    }
}

/// The panel's controls, for the screen reader and for tests.
pub(crate) fn items(sound: &SoundView, size: Size) -> Vec<Item> {
    let mut items: Vec<Item> = tabs(size)
        .into_iter()
        .enumerate()
        .map(|(i, r)| button(format!("Page {}", PAGES[i]), Some(i == sound.page), r))
        .collect();
    match sound.page {
        TUNING => {
            let plan = tuning_plan(size);
            for (i, r) in plan.temperaments.iter().enumerate() {
                let on = Some(i == sound.temperament);
                items.push(button(TEMPERAMENT_NAMES[i].into(), on, *r));
            }
            for (i, r) in plan.keys.iter().enumerate() {
                items.push(button(
                    format!("Key {}", KEY_NAMES[i]),
                    Some(i == sound.key),
                    *r,
                ));
            }
            items.push(slider_item("Pitch of A", plan.pitch));
            for (r, hz) in plan.pitches.iter().zip(PITCHES) {
                let on = Some((sound.pitch_hz - hz).abs() < 0.05);
                items.push(button(format!("A {hz:.0}"), on, *r));
            }
            items.push(slider_item("Tremulant rate", plan.rate));
            items.push(slider_item("Tremulant depth", plan.depth));
        }
        MIDI => {
            let plan = midi_plan(size, sound.learn.bindings.len());
            items.push(button("Learn".into(), Some(sound.learn.on), plan.learn));
            if !sound.learn.bindings.is_empty() {
                items.push(button("Forget all".into(), None, plan.forget_all));
            }
            for ((_, x), (control, _)) in plan.rows.iter().zip(&sound.learn.bindings) {
                items.push(button(format!("Forget {control}"), None, *x));
            }
        }
        _ => {
            let plan = sound_plan(size);
            items.push(slider_item("Volume", plan.level));
            items.push(slider_item("Reverb", plan.reverb));
            for (i, r) in plan.rooms.iter().enumerate() {
                let on = Some(i == sound.room);
                items.push(button(format!("Room {}", ROOM_NAMES[i]), on, *r));
            }
            items.push(button("EQ".into(), Some(sound.eq), plan.eq));
            for (i, (_, column)) in plan.faders.iter().enumerate() {
                items.push(slider_item(&format!("EQ {}", EQ_BANDS[i].name), *column));
            }
        }
    }
    items
}

// ----------------------------------------------------------------- input

/// A control being dragged: its gesture, and how the pointer maps to it.
pub(crate) struct Drag {
    gesture: Gesture,
    track: Rect,
    upright: bool,
}

impl Drag {
    fn to(&mut self, at: Point) {
        let v = if self.upright {
            (self.track.bottom() - at.y) / self.track.height
        } else {
            (at.x - self.track.x) / self.track.width
        };
        self.gesture.set(f64::from(v.clamp(0.0, 1.0)));
    }
}

fn begin(context: &EditorContext, p: &dyn Param, track: Rect, upright: bool, at: Point) -> Drag {
    let mut drag = Drag {
        gesture: Gesture::begin(context, p.param_id(), p.normalized()),
        track,
        upright,
    };
    drag.to(at);
    drag
}

/// Choice `i` of `n`, as a normalized value.
fn choice(i: usize, n: usize) -> f64 {
    i as f64 / (n.max(2) - 1) as f64
}

/// The pointer on the panel: a tab, a slider or fader dragged, a chip
/// pressed.
pub(crate) fn input(
    cx: &mut EventCx<'_>,
    event: &Event,
    at: Point,
    size: Size,
    source: &Source,
    sound: Option<&SoundView>,
    drag: &mut Option<Drag>,
) -> Handled {
    if let (
        Event::PointerDown {
            button: Button::Primary,
            ..
        },
        Some(sound),
    ) = (event, sound)
    {
        let context = cx.context().clone();
        if let Some(i) = tabs(size).iter().position(|r| r.contains(at)) {
            source.sound_page(i);
            return Handled::Yes;
        }
        let pressed = match sound.page {
            TUNING => press_tuning(&context, source, size, at),
            MIDI => press_midi(source, sound, size, at),
            _ => press_sound(&context, source, size, at),
        };
        match pressed {
            Pressed::Nothing => return Handled::No,
            Pressed::Done => return Handled::Yes,
            Pressed::Drag(d) => {
                *drag = Some(d);
                cx.capture_pointer();
                return Handled::Yes;
            }
        }
    }
    let Some(d) = drag.as_mut() else {
        return Handled::No;
    };
    if at.x.is_finite() && at.y.is_finite() {
        d.to(at);
    }
    if matches!(event, Event::PointerUp { .. } | Event::PointerCancel) {
        if let Some(d) = drag.take() {
            d.gesture.end();
        }
        cx.release_pointer();
    }
    Handled::Yes
}

enum Pressed {
    Nothing,
    Done,
    Drag(Drag),
}

fn row(r: Rect) -> Rect {
    Rect::new(r.x - 10.0, r.y - 4.0, r.width + 20.0, r.height + 8.0)
}

fn press_sound(context: &EditorContext, source: &Source, size: Size, at: Point) -> Pressed {
    let p = &source.shared().params;
    let plan = sound_plan(size);
    if row(plan.level).contains(at) {
        Pressed::Drag(begin(context, &p.level, plan.level, false, at))
    } else if row(plan.reverb).contains(at) {
        Pressed::Drag(begin(context, &p.reverb, plan.reverb, false, at))
    } else if let Some(i) = plan.rooms.iter().position(|r| r.contains(at)) {
        Gesture::set_once(context, p.room.param_id(), choice(i, ROOM_NAMES.len()));
        Pressed::Done
    } else if plan.eq.contains(at) {
        let next = if p.eq.value() { 0.0 } else { 1.0 };
        Gesture::set_once(context, p.eq.param_id(), next);
        Pressed::Done
    } else if let Some(i) = plan.faders.iter().position(|(_, c)| c.contains(at)) {
        let track = plan.faders[i].0;
        Pressed::Drag(begin(context, &p.eq_bands[i], track, true, at))
    } else {
        Pressed::Nothing
    }
}

fn press_tuning(context: &EditorContext, source: &Source, size: Size, at: Point) -> Pressed {
    let p = &source.shared().params;
    let plan = tuning_plan(size);
    if let Some(i) = plan.temperaments.iter().position(|r| r.contains(at)) {
        let n = TEMPERAMENT_NAMES.len();
        Gesture::set_once(context, p.temperament.param_id(), choice(i, n));
        Pressed::Done
    } else if let Some(i) = plan.keys.iter().position(|r| r.contains(at)) {
        let n = KEY_NAMES.len();
        Gesture::set_once(context, p.temperament_key.param_id(), choice(i, n));
        Pressed::Done
    } else if let Some(i) = plan.pitches.iter().position(|r| r.contains(at)) {
        let normalized = p.pitch.range().normalize(PITCHES[i]);
        Gesture::set_once(context, p.pitch.param_id(), normalized);
        Pressed::Done
    } else if row(plan.pitch).contains(at) {
        Pressed::Drag(begin(context, &p.pitch, plan.pitch, false, at))
    } else if row(plan.rate).contains(at) {
        Pressed::Drag(begin(context, &p.tremulant_rate, plan.rate, false, at))
    } else if row(plan.depth).contains(at) {
        Pressed::Drag(begin(context, &p.tremulant_depth, plan.depth, false, at))
    } else {
        Pressed::Nothing
    }
}

fn press_midi(source: &Source, sound: &SoundView, size: Size, at: Point) -> Pressed {
    let plan = midi_plan(size, sound.learn.bindings.len());
    if plan.learn.contains(at) {
        source.set_learning(!sound.learn.on);
        Pressed::Done
    } else if !sound.learn.bindings.is_empty() && plan.forget_all.contains(at) {
        source.forget_controls(None);
        Pressed::Done
    } else if let Some(i) = plan.rows.iter().position(|(_, x)| x.contains(at)) {
        source.forget_controls(Some(i));
        Pressed::Done
    } else {
        Pressed::Nothing
    }
}
