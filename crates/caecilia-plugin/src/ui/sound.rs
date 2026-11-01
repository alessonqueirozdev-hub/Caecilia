//! The sound panel on the music desk: the level, the reverb and its room,
//! and the master EQ's five faders. Each control moves its parameter as a
//! gesture, so a host records it as it would its own knob.

use manguebyte_gfx::{Circle, Point, Rect, RoundedRect, Scene, Shadow, Size, Stop, Stroke};
use manguebyte_params::Param;
use manguebyte_plugin::editor::{EditorContext, Gesture};
use manguebyte_ui::access::Role;
use manguebyte_ui::{Button, Event, EventCx, Handled};

use super::Shared;
use super::live::Item;
use super::paint::{heading, style, text, vertical};
use super::snapshot::Snapshot;
use crate::{EQ_BANDS, ROOM_NAMES};

/// What the sound panel shows, read once a frame.
#[derive(Clone, PartialEq)]
pub(crate) struct SoundView {
    pub(crate) level_db: f32,
    pub(crate) level: f32,
    pub(crate) reverb: f32,
    pub(crate) room: usize,
    pub(crate) eq: bool,
    pub(crate) bands_db: [f32; 5],
    pub(crate) bands: [f32; 5],
}

impl std::hash::Hash for SoundView {
    fn hash<H: std::hash::Hasher>(&self, h: &mut H) {
        self.level.to_bits().hash(h);
        self.reverb.to_bits().hash(h);
        self.room.hash(h);
        self.eq.hash(h);
        for b in self.bands {
            b.to_bits().hash(h);
        }
    }
}

impl SoundView {
    pub(crate) fn read(shared: &Shared) -> SoundView {
        let p = &shared.params;
        SoundView {
            level_db: p.level.value(),
            level: p.level.normalized() as f32,
            reverb: p.reverb.value(),
            room: p.room.index(),
            eq: p.eq.value(),
            bands_db: std::array::from_fn(|i| p.eq_bands[i].value()),
            bands: std::array::from_fn(|i| p.eq_bands[i].normalized() as f32),
        }
    }
}

/// Where the panel's controls are.
struct Plan {
    level: Rect,
    reverb: Rect,
    rooms: Vec<Rect>,
    eq: Rect,
    /// Each fader's track, and the column it stands in.
    faders: Vec<(Rect, Rect)>,
}

const LABEL: f32 = 92.0;

fn plan(size: Size) -> Plan {
    let x = 20.0 + LABEL;
    let w = (size.width - x - 80.0).max(40.0);
    let level = Rect::new(x, 52.0, w, 24.0);
    let reverb = Rect::new(x, 84.0, w, 24.0);
    let room_w = (size.width - x - 20.0) / ROOM_NAMES.len() as f32;
    let rooms = (0..ROOM_NAMES.len())
        .map(|i| Rect::new(x + i as f32 * room_w, 118.0, room_w - 6.0, 26.0))
        .collect();
    let eq = Rect::new(x, 156.0, 58.0, 24.0);
    let top = 192.0;
    let bottom = (size.height - 40.0).max(top + 24.0);
    let col = (size.width - 40.0) / EQ_BANDS.len() as f32;
    let faders = (0..EQ_BANDS.len())
        .map(|i| {
            let column = Rect::new(20.0 + i as f32 * col, top - 8.0, col, bottom - top + 36.0);
            let track = Rect::new(column.center().x - 2.0, top, 4.0, bottom - top);
            (track, column)
        })
        .collect();
    Plan {
        level,
        reverb,
        rooms,
        eq,
        faders,
    }
}

fn knob(scene: &mut Scene, snapshot: &Snapshot, c: Point) {
    let look = snapshot.look;
    let stops = [
        Stop::new(0.0, look.brass_light),
        Stop::new(1.0, look.brass_dark),
    ];
    scene.shadow(
        Circle::new(c, 8.0),
        Shadow::new(manguebyte_gfx::Color::hex_rgba(0x0000_0090))
            .with_offset(0.0, 2.0)
            .with_blur(4.0),
    );
    scene.fill(
        Circle::new(c, 8.0),
        vertical(Rect::new(c.x - 8.0, c.y - 8.0, 16.0, 16.0), &stops),
    );
    scene.fill(Circle::new(c, 2.5), look.brass_dark);
}

/// A horizontal slider: its label, its track filled to `at` (0 to 1), and
/// what it reads.
fn slider(scene: &mut Scene, snapshot: &Snapshot, r: Rect, label: &str, at: f32, reads: &str) {
    let look = snapshot.look;
    text(
        scene,
        label,
        style(12.0, 600.0, look.text),
        Point::new(20.0, r.y + 4.0),
        0.0,
    );
    let y = r.center().y;
    let track = RoundedRect::new(Rect::new(r.x, y - 2.0, r.width, 4.0), 2.0);
    scene.fill(track, look.edge);
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

/// The sound panel.
pub(crate) fn draw(size: Size, scene: &mut Scene, snapshot: &Snapshot, sound: &SoundView) {
    let look = snapshot.look;
    let rect = Rect::new(0.0, 0.0, size.width, size.height);
    heading(scene, &look, rect, "Sound", "level, room and voicing");
    let plan = plan(size);
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
    text(
        scene,
        "Room",
        style(12.0, 600.0, look.text),
        Point::new(20.0, plan.rooms[0].y + 5.0),
        0.0,
    );
    for (i, r) in plan.rooms.iter().enumerate() {
        let shape = RoundedRect::new(*r, 13.0);
        let on = i == sound.room;
        if on {
            scene.fill(shape, look.accent.with_alpha(0x40));
            scene.stroke(shape, Stroke::new(1.0), look.accent);
        } else {
            scene.fill(shape, look.edge.with_alpha(0x60));
        }
        text(
            scene,
            ROOM_NAMES[i],
            style(11.5, if on { 650.0 } else { 500.0 }, look.text),
            Point::new(r.center().x, r.y + 5.0),
            0.5,
        );
    }
    text(
        scene,
        "EQ",
        style(12.0, 600.0, look.text),
        Point::new(20.0, plan.eq.y + 4.0),
        0.0,
    );
    let shape = RoundedRect::new(plan.eq, 12.0);
    if sound.eq {
        scene.fill(shape, look.accent.with_alpha(0x40));
        scene.stroke(shape, Stroke::new(1.0), look.accent);
    } else {
        scene.fill(shape, look.edge.with_alpha(0x60));
    }
    text(
        scene,
        if sound.eq { "On" } else { "Off" },
        style(11.5, 650.0, look.text),
        Point::new(plan.eq.center().x, plan.eq.y + 4.0),
        0.5,
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
        let shape = RoundedRect::new(*track, 2.0);
        scene.fill(shape, look.edge);
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

/// What the screen reader says of the panel.
pub(crate) fn describe(sound: &SoundView) -> String {
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

/// The panel's controls, for the screen reader and for tests.
pub(crate) fn items(sound: &SoundView, size: Size) -> Vec<Item> {
    let plan = plan(size);
    let mut items = vec![
        Item {
            role: Role::Slider,
            name: "Volume".into(),
            checked: None,
            rect: plan.level,
        },
        Item {
            role: Role::Slider,
            name: "Reverb".into(),
            checked: None,
            rect: plan.reverb,
        },
    ];
    for (i, r) in plan.rooms.iter().enumerate() {
        items.push(Item {
            role: Role::Button,
            name: format!("Room {}", ROOM_NAMES[i]),
            checked: Some(i == sound.room),
            rect: *r,
        });
    }
    items.push(Item {
        role: Role::Button,
        name: "EQ".into(),
        checked: Some(sound.eq),
        rect: plan.eq,
    });
    for (i, (_, column)) in plan.faders.iter().enumerate() {
        items.push(Item {
            role: Role::Slider,
            name: format!("EQ {}", EQ_BANDS[i].name),
            checked: None,
            rect: *column,
        });
    }
    items
}

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

/// The pointer on the panel: a slider or fader dragged, a room or the EQ
/// pressed.
pub(crate) fn input(
    cx: &mut EventCx<'_>,
    event: &Event,
    at: Point,
    size: Size,
    shared: &Shared,
    drag: &mut Option<Drag>,
) -> Handled {
    let p = &shared.params;
    if let Event::PointerDown {
        button: Button::Primary,
        ..
    } = event
    {
        let plan = plan(size);
        let row = |r: Rect| Rect::new(r.x - 10.0, r.y - 4.0, r.width + 20.0, r.height + 8.0);
        let context = cx.context().clone();
        if row(plan.level).contains(at) {
            *drag = Some(begin(&context, &p.level, plan.level, false, at));
        } else if row(plan.reverb).contains(at) {
            *drag = Some(begin(&context, &p.reverb, plan.reverb, false, at));
        } else if let Some(i) = plan.rooms.iter().position(|r| r.contains(at)) {
            let n = (ROOM_NAMES.len() - 1) as f64;
            Gesture::set_once(&context, p.room.param_id(), i as f64 / n);
            return Handled::Yes;
        } else if plan.eq.contains(at) {
            let next = if p.eq.value() { 0.0 } else { 1.0 };
            Gesture::set_once(&context, p.eq.param_id(), next);
            return Handled::Yes;
        } else if let Some(i) = plan.faders.iter().position(|(_, c)| c.contains(at)) {
            let track = plan.faders[i].0;
            *drag = Some(begin(&context, &p.eq_bands[i], track, true, at));
        } else {
            return Handled::No;
        }
        cx.capture_pointer();
        return Handled::Yes;
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
