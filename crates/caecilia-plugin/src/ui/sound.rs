// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The settings panel on the music desk, in pages of three rows at most, so
//! each fits the desk of a short window: the sound (the level, the reverb
//! and its room), the master EQ, the tuning (temperament, its key, the
//! pitch of A), the tremulant, MIDI (learn, and the input ports where an
//! application serves them), and the audio devices. Each control moves its
//! parameter as a gesture, so a host records it as it would its own knob.

use manguebyte_gfx::{
    Circle, Color, Point, Rect, RoundedRect, Scene, Shadow, Size, Stop, Stroke, TextStyle,
};
use manguebyte_params::Param;
use manguebyte_plugin::editor::{EditorContext, Gesture};
use manguebyte_ui::access::Role;
use manguebyte_ui::{Button, Event, EventCx, Handled};

use super::live::Item;
use super::paint::{polygon, style, text, vertical, width_of};
use super::snapshot::{Snapshot, Source, note_name};
use crate::console::{Action, Source as Control};
use crate::devices::{DeviceRequest, DevicesView};
use crate::params::{KEY_NAMES, TEMPERAMENT_NAMES};
use crate::{EQ_BANDS, ROOM_NAMES};

/// The pages, in the order of their tabs; Audio only where an application
/// serves the devices.
pub(crate) const PAGES: [&str; 6] = ["Sound", "EQ", "Tuning", "Tremulant", "MIDI", "Audio"];
pub(crate) const EQ: usize = 1;
pub(crate) const TUNING: usize = 2;
pub(crate) const TREMULANT: usize = 3;
pub(crate) const MIDI: usize = 4;
pub(crate) const AUDIO: usize = 5;

/// The pitches of A a player reaches for.
const PITCHES: [f32; 4] = [392.0, 415.0, 440.0, 466.0];

/// A selector's list, open over the page.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub(crate) enum Pick {
    Temperament,
    Key,
    Pitch,
    Output,
    Rate,
    Buffer,
}

impl Pick {
    /// What the panel says it chooses.
    fn title(self) -> &'static str {
        match self {
            Self::Temperament => "Temperament",
            Self::Key => "Temperament's key",
            Self::Pitch => "Pitch of A",
            Self::Output => "Output",
            Self::Rate => "Sample rate",
            Self::Buffer => "Buffer",
        }
    }
}

/// What MIDI learn shows: whether it is on, what it waits to bind, what it
/// bound last, and every control learned.
#[derive(Clone, PartialEq, Hash, Default)]
pub(crate) struct LearnView {
    pub(crate) on: bool,
    pub(crate) waiting_for: Option<String>,
    pub(crate) last: Option<String>,
    /// Each binding: the control, and what it does.
    pub(crate) bindings: Vec<(String, String)>,
    /// The manual switches: each manual's division, its name, and the key
    /// that switches to it, if one is learned.
    pub(crate) switches: Vec<(u16, String, Option<String>)>,
    /// What the keyboard last switched plays: `Channel 1 plays Récit`.
    pub(crate) route: Option<String>,
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
    /// The devices, where an application serves them.
    pub(crate) devices: Option<DevicesView>,
    /// The selector's list open, if one is.
    pub(crate) picking: Option<Pick>,
}

impl SoundView {
    /// How many pages there are: the Audio page only with devices.
    fn pages(&self) -> usize {
        if self.devices.is_some() {
            PAGES.len()
        } else {
            PAGES.len() - 1
        }
    }
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
        (&self.devices, self.picking).hash(h);
    }
}

impl SoundView {
    pub(crate) fn read(
        source: &Source,
        page: usize,
        learn: LearnView,
        devices: Option<DevicesView>,
        picking: Option<Pick>,
    ) -> SoundView {
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
            devices,
            picking,
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
        Action::Manual(n) => organ
            .divisions()
            .get(usize::from(n))
            .map_or_else(|| format!("Manual {n}"), |d| format!("Play on {}", d.name)),
    }
}

// ------------------------------------------------------------------ plan

const LABEL: f32 = 92.0;
const ROW: f32 = 24.0;

/// The tops of a page's three rows, below the tabs, spread over what the
/// desk has: what a short window holds, roomier in a tall one.
fn rows(size: Size) -> [f32; 3] {
    let gap = ((size.height - 42.0 - ROW - 14.0) / 2.0).clamp(25.0, 42.0);
    [42.0, 42.0 + gap, 42.0 + 2.0 * gap]
}

/// A row of chips from `x` to the panel's edge, `n` of them.
fn chips(size: Size, y: f32, n: usize) -> Vec<Rect> {
    let x = 20.0 + LABEL;
    let w = (size.width - x - 20.0) / n.max(1) as f32;
    (0..n)
        .map(|i| Rect::new(x + i as f32 * w, y, w - 6.0, ROW))
        .collect()
}

fn track(size: Size, y: f32) -> Rect {
    let x = 20.0 + LABEL;
    Rect::new(x, y, (size.width - x - 80.0).max(40.0), ROW)
}

fn tabs(size: Size, pages: usize) -> Vec<Rect> {
    let mut x = size.width - 20.0;
    let s = style(11.5, 600.0, Color::hex(0));
    let mut out: Vec<Rect> = PAGES[..pages]
        .iter()
        .rev()
        .map(|p| {
            let w = width_of(p, s) + 20.0;
            x -= w;
            let r = Rect::new(x, 10.0, w, 22.0);
            x -= 5.0;
            r
        })
        .collect();
    out.reverse();
    out
}

/// The list a selector opens: as many columns as it wants, under the tabs.
fn list(size: Size, count: usize, columns: usize) -> Vec<Rect> {
    let columns = columns.max(1);
    let w = (size.width - 40.0) / columns as f32;
    (0..count)
        .map(|i| {
            let (row, col) = (i / columns, i % columns);
            Rect::new(
                20.0 + col as f32 * w,
                44.0 + row as f32 * (ROW + 4.0),
                w - 6.0,
                ROW,
            )
        })
        .filter(|r| r.bottom() <= size.height - 4.0)
        .collect()
}

/// What the list open offers: each choice's name on the desk, and its name
/// for the screen reader and tests; whether it is the one chosen.
fn choices(pick: Pick, sound: &SoundView) -> Vec<(String, String, bool)> {
    let auto_or =
        |values: &[u32], chosen: Option<u32>, name: fn(Option<u32>) -> String, tag: &str| {
            std::iter::once(None)
                .chain(values.iter().map(|v| Some(*v)))
                .map(|v| {
                    let shown = name(v);
                    (shown.clone(), format!("{tag} {shown}"), v == chosen)
                })
                .collect()
        };
    match pick {
        Pick::Temperament => TEMPERAMENT_NAMES
            .iter()
            .enumerate()
            .map(|(i, n)| (n.to_string(), n.to_string(), i == sound.temperament))
            .collect(),
        Pick::Key => KEY_NAMES
            .iter()
            .enumerate()
            .map(|(i, n)| (n.to_string(), format!("Key {n}"), i == sound.key))
            .collect(),
        Pick::Pitch => PITCHES
            .iter()
            .map(|hz| {
                let on = (sound.pitch_hz - hz).abs() < 0.05;
                (format!("A = {hz:.0} Hz"), format!("A {hz:.0}"), on)
            })
            .collect(),
        Pick::Output => sound.devices.as_ref().map_or_else(Vec::new, |d| {
            d.outputs
                .iter()
                .map(|o| {
                    let tag = if o.is_default { " (default)" } else { "" };
                    (
                        format!("{}{tag}", o.name),
                        format!("Output {}", o.name),
                        o.chosen,
                    )
                })
                .collect()
        }),
        Pick::Rate => sound.devices.as_ref().map_or_else(Vec::new, |d| {
            auto_or(&d.sample_rates, d.sample_rate, rate_name, "Rate")
        }),
        Pick::Buffer => sound.devices.as_ref().map_or_else(Vec::new, |d| {
            auto_or(&d.buffer_sizes, d.buffer_size, buffer_name, "Buffer")
        }),
    }
}

/// How many columns a list takes.
fn columns(pick: Pick, count: usize) -> usize {
    match pick {
        Pick::Temperament => 3,
        Pick::Key => 6,
        Pick::Pitch => 2,
        Pick::Output => {
            if count > 3 {
                2
            } else {
                1
            }
        }
        Pick::Rate | Pick::Buffer => 5,
    }
}

struct SoundPlan {
    level: Rect,
    reverb: Rect,
    rooms: Vec<Rect>,
}

fn sound_plan(size: Size) -> SoundPlan {
    let r = rows(size);
    SoundPlan {
        level: track(size, r[0]),
        reverb: track(size, r[1]),
        rooms: chips(size, r[2], ROOM_NAMES.len()),
    }
}

struct EqPlan {
    toggle: Rect,
    /// Each fader's track, and the column it stands in.
    faders: Vec<(Rect, Rect)>,
}

/// The faders stand in whatever height the desk has below the switch.
fn eq_plan(size: Size) -> EqPlan {
    let top = 44.0 + ROW + 16.0;
    let bottom = (size.height - 24.0).max(top + 12.0);
    let col = (size.width - 40.0) / EQ_BANDS.len() as f32;
    EqPlan {
        toggle: Rect::new(20.0 + LABEL, 44.0, 58.0, ROW),
        faders: (0..EQ_BANDS.len())
            .map(|i| {
                let column = Rect::new(20.0 + i as f32 * col, top - 6.0, col, bottom - top + 12.0);
                let track = Rect::new(column.center().x - 2.0, top, 4.0, bottom - top);
                (track, column)
            })
            .collect(),
    }
}

struct TuningPlan {
    temperament: Rect,
    key: Rect,
    pitch: Rect,
    presets: Rect,
}

fn tuning_plan(size: Size) -> TuningPlan {
    let x = 20.0 + LABEL;
    let r = rows(size);
    let wide = size.width - x - 20.0;
    let presets = 112.0;
    TuningPlan {
        temperament: Rect::new(x, r[0], wide, ROW),
        key: Rect::new(x, r[1], wide, ROW),
        pitch: Rect::new(x, r[2], (wide - presets - 18.0).max(40.0), ROW),
        presets: Rect::new(size.width - 20.0 - presets, r[2], presets, ROW),
    }
}

struct TremulantPlan {
    rate: Rect,
    depth: Rect,
}

fn tremulant_plan(size: Size) -> TremulantPlan {
    let r = rows(size);
    TremulantPlan {
        rate: track(size, r[0]),
        depth: track(size, r[1]),
    }
}

struct MidiPlan {
    learn: Rect,
    forget_all: Rect,
    /// Each manual's key, and the one that sets the top B and C.
    switches: Vec<Rect>,
    top_keys: Rect,
    /// Where the row of input ports goes: where an application serves them.
    ports_row: Option<f32>,
    /// The input ports' chips.
    ports: Vec<Rect>,
    /// Where the line under the learn button goes.
    note: f32,
    /// Each binding's row and its forget mark.
    rows: Vec<(Rect, Rect)>,
}

/// The ports shown in a row, at most.
const PORTS_SHOWN: usize = 4;

fn midi_plan(size: Size, count: usize, switches: usize, ports: Option<usize>) -> MidiPlan {
    let r = rows(size);
    let ports_row = ports.map(|_| r[2]);
    let ports = match ports {
        Some(n) if n > 0 => chips(size, r[2], n.min(PORTS_SHOWN)),
        _ => Vec::new(),
    };
    let note = if ports_row.is_some() {
        r[2] + ROW + 6.0
    } else {
        r[2] + 3.0
    };
    let mut switches = chips(size, r[1], switches + 1);
    let top_keys = switches.pop().unwrap_or_default();
    let top = note + 20.0;
    let room = ((size.height - top - 10.0) / 26.0).max(0.0) as usize;
    MidiPlan {
        learn: Rect::new(20.0, r[0], 84.0, ROW),
        forget_all: Rect::new(size.width - 20.0 - 90.0, r[0], 90.0, ROW),
        switches,
        top_keys,
        ports_row,
        ports,
        note,
        rows: (0..count.min(room))
            .map(|i| {
                let r = Rect::new(20.0, top + i as f32 * 26.0, size.width - 40.0, 22.0);
                (r, Rect::new(r.right() - 26.0, r.y, 26.0, r.height))
            })
            .collect(),
    }
}

struct AudioPlan {
    output: Rect,
    rate: Rect,
    buffer: Rect,
    /// Where each line of the report goes, beside the selectors, and how
    /// wide it can be.
    report: [Point; 2],
    report_width: f32,
}

fn rate_name(rate: Option<u32>) -> String {
    match rate {
        None => "Auto".into(),
        Some(r) if r % 1000 == 0 => format!("{} kHz", r / 1000),
        Some(r) => format!("{:.1} kHz", f64::from(r) / 1000.0),
    }
}

fn buffer_name(frames: Option<u32>) -> String {
    frames.map_or_else(|| "Auto".into(), |f| f.to_string())
}

fn audio_plan(size: Size) -> AudioPlan {
    let x = 20.0 + LABEL;
    let r = rows(size);
    let short = 110.0;
    let beside = x + short + 14.0;
    AudioPlan {
        output: Rect::new(x, r[0], size.width - x - 20.0, ROW),
        rate: Rect::new(x, r[1], short, ROW),
        buffer: Rect::new(x, r[2], short, ROW),
        report: [
            Point::new(beside, r[1] + 5.0),
            Point::new(beside, r[2] + 5.0),
        ],
        report_width: size.width - beside - 20.0,
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

/// `name`, cut to fit `width`.
fn fit(name: &str, s: TextStyle, width: f32) -> String {
    let mut out = name.to_string();
    while width_of(&out, s) > width && out.chars().count() > 4 {
        let kept: String = out.trim_end_matches('\u{2026}').chars().collect();
        let kept: String = kept.chars().take(kept.chars().count() - 1).collect();
        out = format!("{kept}\u{2026}");
    }
    out
}

/// A chip, lit when `on`; its name cut if it must be.
fn chip(scene: &mut Scene, snapshot: &Snapshot, r: Rect, name: &str, on: bool) {
    let look = snapshot.look;
    let shape = RoundedRect::new(r, r.height / 2.0);
    if on {
        scene.fill(shape, look.accent.with_alpha(0x40));
        scene.stroke(shape, Stroke::new(1.0), look.accent);
    } else {
        scene.fill(shape, look.edge.with_alpha(0x60));
    }
    let s = style(11.5, if on { 650.0 } else { 500.0 }, look.text);
    let shown = fit(name, s, r.width - 14.0);
    text(
        scene,
        &shown,
        s,
        Point::new(r.center().x, r.y + (r.height - 11.5 * 1.25) / 2.0),
        0.5,
    );
}

/// A selector: its value, and the mark that says it opens a list.
fn selector(scene: &mut Scene, snapshot: &Snapshot, r: Rect, value: &str) {
    let look = snapshot.look;
    let shape = RoundedRect::new(r, 8.0);
    scene.fill(shape, look.edge.with_alpha(0x60));
    scene.stroke(shape, Stroke::new(1.0), look.edge);
    let s = style(12.0, 600.0, look.text);
    text(
        scene,
        &fit(value, s, r.width - 36.0),
        s,
        Point::new(r.x + 12.0, r.y + 4.0),
        0.0,
    );
    let c = Point::new(r.right() - 14.0, r.center().y);
    let chevron = polygon(&[
        Point::new(c.x - 4.5, c.y - 2.0),
        Point::new(c.x + 4.5, c.y - 2.0),
        Point::new(c.x, c.y + 3.0),
    ]);
    scene.fill(&chevron, look.text2);
}

fn draw_sound(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
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
}

fn draw_eq(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let plan = eq_plan(size);
    label(scene, snapshot, plan.toggle.y + 1.0, "Master EQ");
    chip(
        scene,
        snapshot,
        plan.toggle,
        if sound.eq { "On" } else { "Off" },
        sound.eq,
    );
    text(
        scene,
        "the organ's voicing, \u{b1}18 dB",
        style(10.5, 450.0, look.muted),
        Point::new(plan.toggle.right() + 12.0, plan.toggle.y + 5.0),
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
            Point::new(column.center().x, track.bottom() + 6.0),
            0.5,
        );
        text(
            scene,
            &format!("{:+.1}", sound.bands_db[i]),
            style(10.0, 500.0, look.text2),
            Point::new(column.center().x + 30.0, track.center().y - 6.0),
            0.5,
        );
    }
}

fn draw_tuning(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let plan = tuning_plan(size);
    label(scene, snapshot, plan.temperament.y + 1.0, "Temperament");
    selector(
        scene,
        snapshot,
        plan.temperament,
        TEMPERAMENT_NAMES[sound.temperament],
    );
    label(scene, snapshot, plan.key.y + 1.0, "Key");
    selector(scene, snapshot, plan.key, KEY_NAMES[sound.key]);
    slider(scene, snapshot, plan.pitch, "Pitch of A", sound.pitch, "");
    selector(
        scene,
        snapshot,
        plan.presets,
        &format!("{:.1} Hz", sound.pitch_hz),
    );
}

fn draw_tremulant(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let plan = tremulant_plan(size);
    slider(
        scene,
        snapshot,
        plan.rate,
        "Rate",
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
    text(
        scene,
        "The Tremulant tablet on the console turns it on.",
        style(10.5, 450.0, snapshot.look.muted),
        Point::new(20.0, rows(size)[2] + 4.0),
        0.0,
    );
}

fn draw_midi(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let learn = &sound.learn;
    let ports = sound.devices.as_ref().map(|d| d.midi_inputs.len());
    let plan = midi_plan(size, learn.bindings.len(), learn.switches.len(), ports);
    chip(scene, snapshot, plan.learn, "Learn", learn.on);
    label(scene, snapshot, plan.top_keys.y + 1.0, "Manual keys");
    for (r, (_, name, key)) in plan.switches.iter().zip(&learn.switches) {
        let shown = match key {
            Some(key) => format!("{name}: {key}"),
            None => format!("{name}: none"),
        };
        let waiting = learn.waiting_for.as_deref() == Some(format!("Play on {name}").as_str());
        chip(scene, snapshot, *r, &shown, waiting);
    }
    chip(scene, snapshot, plan.top_keys, "Use top B and C", false);
    let right = if learn.bindings.is_empty() {
        size.width - 20.0
    } else {
        chip(scene, snapshot, plan.forget_all, "Forget all", false);
        plan.forget_all.x - 8.0
    };
    let say = match (learn.on, &learn.waiting_for) {
        (true, Some(what)) => format!("Move a key or controller for {what}"),
        (true, None) => "Choose a stop, coupler or piston on the console".to_string(),
        (false, _) => "Binds your MIDI keys and controls to the console".to_string(),
    };
    let s = style(12.0, 600.0, if learn.on { look.accent } else { look.text });
    let at = plan.learn.right() + 14.0;
    text(
        scene,
        &fit(&say, s, right - at),
        s,
        Point::new(at, plan.learn.y + 4.0),
        0.0,
    );
    if let Some(y) = plan.ports_row {
        label(scene, snapshot, y + 1.0, "Inputs");
        match &sound.devices {
            Some(d) if !d.midi_inputs.is_empty() => {
                for (r, port) in plan.ports.iter().zip(&d.midi_inputs) {
                    chip(scene, snapshot, *r, &port.name, port.chosen);
                }
            }
            _ => {
                text(
                    scene,
                    "No MIDI input port is connected",
                    style(11.5, 500.0, look.text2),
                    Point::new(20.0 + LABEL, y + 5.0),
                    0.0,
                );
            }
        }
    }
    let below = learn
        .last
        .clone()
        .or_else(|| learn.route.clone())
        .unwrap_or_else(|| {
            if learn.bindings.is_empty() {
                "Press a manual, then the key that switches your keyboard to it.".into()
            } else {
                String::new()
            }
        });
    let s = style(10.5, 450.0, look.muted);
    text(
        scene,
        &fit(&below, s, size.width - 40.0),
        s,
        Point::new(20.0, plan.note),
        0.0,
    );
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

fn draw_audio(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let Some(devices) = &sound.devices else {
        return;
    };
    let plan = audio_plan(size);
    label(scene, snapshot, plan.output.y + 1.0, "Output");
    let chosen = devices
        .outputs
        .iter()
        .find(|d| d.chosen)
        .map_or("No output", |d| d.name.as_str());
    selector(scene, snapshot, plan.output, chosen);
    label(scene, snapshot, plan.rate.y + 1.0, "Sample rate");
    selector(scene, snapshot, plan.rate, &rate_name(devices.sample_rate));
    label(scene, snapshot, plan.buffer.y + 1.0, "Buffer");
    selector(
        scene,
        snapshot,
        plan.buffer,
        &buffer_name(devices.buffer_size),
    );
    // What plays, beside them; a failure first.
    let all: Vec<&str> = devices.report.lines().collect();
    let n = plan.report.len().min(all.len());
    let lines = if devices.failed {
        &all[all.len() - n..]
    } else {
        &all[..n]
    };
    for (i, (line, at)) in lines.iter().zip(plan.report).enumerate() {
        let failed = devices.failed && i + 1 == lines.len();
        let ink = if failed {
            look.set_red
        } else if i == 0 {
            look.text2
        } else {
            look.accent
        };
        let s = style(10.5, if i == 0 { 500.0 } else { 450.0 }, ink);
        text(scene, &fit(line, s, plan.report_width), s, at, 0.0);
    }
}

/// The list a selector opened.
fn draw_list(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView, pick: Pick) {
    let options = choices(pick, sound);
    let rects = list(size, options.len(), columns(pick, options.len()));
    for (r, (shown, _, on)) in rects.iter().zip(&options) {
        chip(scene, snapshot, *r, shown, *on);
    }
}

/// The settings panel, on its page.
pub(crate) fn draw(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let tabs = tabs(size, sound.pages());
    let title = sound.picking.map_or("Settings", Pick::title);
    let heading = style(13.0, 650.0, look.text);
    // Its name, where the tabs leave room for it.
    if tabs
        .first()
        .is_none_or(|t| t.x > 28.0 + width_of(title, heading))
    {
        text(scene, title, heading, Point::new(20.0, 13.0), 0.0);
    }
    for (i, r) in tabs.iter().enumerate() {
        chip(scene, snapshot, *r, PAGES[i], i == sound.page);
    }
    if let Some(pick) = sound.picking {
        draw_list(size, scene, snapshot, sound, pick);
        return;
    }
    match sound.page {
        EQ => draw_eq(size, scene, snapshot, sound),
        TUNING => draw_tuning(size, scene, snapshot, sound),
        TREMULANT => draw_tremulant(size, scene, snapshot, sound),
        MIDI => draw_midi(size, scene, snapshot, sound),
        AUDIO => draw_audio(size, scene, snapshot, sound),
        _ => draw_sound(size, scene, snapshot, sound),
    }
}

/// The MIDI input ports, as the screen reader says them.
fn ports_said(devices: &DevicesView) -> String {
    let ports: Vec<String> = devices
        .midi_inputs
        .iter()
        .map(|p| format!("{} {}", p.name, if p.chosen { "open" } else { "closed" }))
        .collect();
    if ports.is_empty() {
        "none".to_string()
    } else {
        ports.join(", ")
    }
}

/// What the screen reader says of the panel.
pub(crate) fn describe(sound: &SoundView) -> String {
    match sound.page {
        AUDIO => {
            let Some(devices) = &sound.devices else {
                return "Audio".into();
            };
            let mut said = format!(
                "Audio: {}; rate {}, buffer {}; MIDI in: {}",
                devices.report.lines().next().unwrap_or(""),
                rate_name(devices.sample_rate),
                buffer_name(devices.buffer_size),
                ports_said(devices)
            );
            for line in devices.report.lines().skip(1) {
                said.push_str(&format!("; {line}"));
            }
            said
        }
        TUNING => format!(
            "Tuning: {} on {}, A {:.1} Hz",
            TEMPERAMENT_NAMES[sound.temperament], KEY_NAMES[sound.key], sound.pitch_hz,
        ),
        TREMULANT => format!(
            "Tremulant: {:.2} Hz, depth {:.0} %",
            sound.rate_hz,
            sound.depth * 100.0
        ),
        EQ => {
            let bands: Vec<String> = EQ_BANDS
                .iter()
                .zip(sound.bands_db)
                .map(|(b, db)| format!("{} {db:+.1} dB", b.name))
                .collect();
            format!(
                "EQ {}: {}",
                if sound.eq { "on" } else { "off" },
                bands.join(", ")
            )
        }
        MIDI => {
            let learn = &sound.learn;
            let mut said = match (learn.on, &learn.waiting_for) {
                (true, Some(what)) => format!("MIDI: learning, waiting for a control for {what}"),
                (true, None) => "MIDI: learning, choose a control on the console".into(),
                (false, _) => "MIDI".into(),
            };
            let switches: Vec<String> = learn
                .switches
                .iter()
                .map(|(_, name, key)| match key {
                    Some(key) => format!("{key} switches to {name}"),
                    None => format!("no switch to {name}"),
                })
                .collect();
            if !switches.is_empty() {
                said.push_str(&format!("; {}", switches.join(", ")));
            }
            if let Some(route) = &learn.route {
                said.push_str(&format!("; {route}"));
            }
            if let Some(devices) = &sound.devices {
                said.push_str(&format!("; inputs: {}", ports_said(devices)));
            }
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
        _ => format!(
            "Sound: volume {:+.1} dB, reverb {:.0} % in the {}",
            sound.level_db,
            sound.reverb * 100.0,
            ROOM_NAMES[sound.room].to_lowercase(),
        ),
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
    let mut items: Vec<Item> = tabs(size, sound.pages())
        .into_iter()
        .enumerate()
        .map(|(i, r)| button(format!("Page {}", PAGES[i]), Some(i == sound.page), r))
        .collect();
    if let Some(pick) = sound.picking {
        let options = choices(pick, sound);
        let rects = list(size, options.len(), columns(pick, options.len()));
        for (r, (_, name, on)) in rects.iter().zip(options) {
            items.push(button(name, Some(on), *r));
        }
        return items;
    }
    match sound.page {
        AUDIO => {
            let plan = audio_plan(size);
            items.push(button("Output".into(), None, plan.output));
            items.push(button("Sample rate".into(), None, plan.rate));
            items.push(button("Buffer".into(), None, plan.buffer));
        }
        TUNING => {
            let plan = tuning_plan(size);
            items.push(button("Temperament".into(), None, plan.temperament));
            items.push(button("Key".into(), None, plan.key));
            items.push(slider_item("Pitch of A", plan.pitch));
            items.push(button("Pitch presets".into(), None, plan.presets));
        }
        TREMULANT => {
            let plan = tremulant_plan(size);
            items.push(slider_item("Tremulant rate", plan.rate));
            items.push(slider_item("Tremulant depth", plan.depth));
        }
        EQ => {
            let plan = eq_plan(size);
            items.push(button("EQ".into(), Some(sound.eq), plan.toggle));
            for (i, (_, column)) in plan.faders.iter().enumerate() {
                items.push(slider_item(&format!("EQ {}", EQ_BANDS[i].name), *column));
            }
        }
        MIDI => {
            let learn = &sound.learn;
            let ports = sound.devices.as_ref().map(|d| d.midi_inputs.len());
            let plan = midi_plan(size, learn.bindings.len(), learn.switches.len(), ports);
            items.push(button("Learn".into(), Some(learn.on), plan.learn));
            for (r, (_, name, _)) in plan.switches.iter().zip(&learn.switches) {
                items.push(button(format!("Switch to {name}"), None, *r));
            }
            items.push(button("Use the top B and C".into(), None, plan.top_keys));
            if !learn.bindings.is_empty() {
                items.push(button("Forget all".into(), None, plan.forget_all));
            }
            if let Some(devices) = &sound.devices {
                for (r, port) in plan.ports.iter().zip(&devices.midi_inputs) {
                    items.push(button(format!("MIDI {}", port.name), Some(port.chosen), *r));
                }
            }
            for ((_, x), (control, _)) in plan.rows.iter().zip(&learn.bindings) {
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
        if let Some(i) = tabs(size, sound.pages())
            .iter()
            .position(|r| r.contains(at))
        {
            source.sound_page(i);
            return Handled::Yes;
        }
        if let Some(pick) = sound.picking {
            press_list(&context, source, sound, pick, size, at);
            return Handled::Yes;
        }
        let pressed = match sound.page {
            AUDIO => press_audio(source, sound, size, at),
            TUNING => press_tuning(&context, source, size, at),
            TREMULANT => press_tremulant(&context, source, size, at),
            EQ => press_eq(&context, source, size, at),
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
    } else {
        Pressed::Nothing
    }
}

fn press_eq(context: &EditorContext, source: &Source, size: Size, at: Point) -> Pressed {
    let p = &source.shared().params;
    let plan = eq_plan(size);
    if plan.toggle.contains(at) {
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
    if plan.temperament.contains(at) {
        source.pick(Some(Pick::Temperament));
        Pressed::Done
    } else if plan.key.contains(at) {
        source.pick(Some(Pick::Key));
        Pressed::Done
    } else if plan.presets.contains(at) {
        source.pick(Some(Pick::Pitch));
        Pressed::Done
    } else if row(plan.pitch).contains(at) {
        Pressed::Drag(begin(context, &p.pitch, plan.pitch, false, at))
    } else {
        Pressed::Nothing
    }
}

fn press_tremulant(context: &EditorContext, source: &Source, size: Size, at: Point) -> Pressed {
    let p = &source.shared().params;
    let plan = tremulant_plan(size);
    if row(plan.rate).contains(at) {
        Pressed::Drag(begin(context, &p.tremulant_rate, plan.rate, false, at))
    } else if row(plan.depth).contains(at) {
        Pressed::Drag(begin(context, &p.tremulant_depth, plan.depth, false, at))
    } else {
        Pressed::Nothing
    }
}

fn press_midi(source: &Source, sound: &SoundView, size: Size, at: Point) -> Pressed {
    let learn = &sound.learn;
    let ports = sound.devices.as_ref().map(|d| d.midi_inputs.len());
    let plan = midi_plan(size, learn.bindings.len(), learn.switches.len(), ports);
    if plan.learn.contains(at) {
        source.set_learning(!learn.on);
        Pressed::Done
    } else if let Some(i) = plan.switches.iter().position(|r| r.contains(at)) {
        source.learn_switch(learn.switches[i].0);
        Pressed::Done
    } else if plan.top_keys.contains(at) {
        source.switches_on_top_keys();
        Pressed::Done
    } else if !learn.bindings.is_empty() && plan.forget_all.contains(at) {
        source.forget_controls(None);
        Pressed::Done
    } else if let (Some(i), Some(devices)) = (
        plan.ports.iter().position(|r| r.contains(at)),
        &sound.devices,
    ) {
        let port = &devices.midi_inputs[i];
        source.ask_devices(DeviceRequest::MidiInput(port.id.clone(), !port.chosen));
        Pressed::Done
    } else if let Some(i) = plan.rows.iter().position(|(_, x)| x.contains(at)) {
        source.forget_controls(Some(i));
        Pressed::Done
    } else {
        Pressed::Nothing
    }
}

fn press_audio(source: &Source, sound: &SoundView, size: Size, at: Point) -> Pressed {
    if sound.devices.is_none() {
        return Pressed::Nothing;
    }
    let plan = audio_plan(size);
    let pick = if plan.output.contains(at) {
        Pick::Output
    } else if plan.rate.contains(at) {
        Pick::Rate
    } else if plan.buffer.contains(at) {
        Pick::Buffer
    } else {
        return Pressed::Nothing;
    };
    source.pick(Some(pick));
    Pressed::Done
}

/// A press while a list is open: a choice taken, and the list closes either
/// way.
fn press_list(
    context: &EditorContext,
    source: &Source,
    sound: &SoundView,
    pick: Pick,
    size: Size,
    at: Point,
) -> Pressed {
    let p = &source.shared().params;
    let count = choices(pick, sound).len();
    let chosen = list(size, count, columns(pick, count))
        .iter()
        .position(|r| r.contains(at));
    if let Some(i) = chosen {
        match pick {
            Pick::Temperament => {
                let n = TEMPERAMENT_NAMES.len();
                Gesture::set_once(context, p.temperament.param_id(), choice(i, n));
            }
            Pick::Key => {
                let n = KEY_NAMES.len();
                Gesture::set_once(context, p.temperament_key.param_id(), choice(i, n));
            }
            Pick::Pitch => {
                let normalized = p.pitch.range().normalize(PITCHES[i]);
                Gesture::set_once(context, p.pitch.param_id(), normalized);
            }
            Pick::Output => {
                if let Some(d) = &sound.devices {
                    source.ask_devices(DeviceRequest::Output(d.outputs[i].id.clone()));
                }
            }
            Pick::Rate => {
                if let Some(d) = &sound.devices {
                    let rate = i.checked_sub(1).map(|i| d.sample_rates[i]);
                    source.ask_devices(DeviceRequest::SampleRate(rate));
                }
            }
            Pick::Buffer => {
                if let Some(d) = &sound.devices {
                    let frames = i.checked_sub(1).map(|i| d.buffer_sizes[i]);
                    source.ask_devices(DeviceRequest::BufferSize(frames));
                }
            }
        }
    }
    source.pick(None);
    Pressed::Done
}
