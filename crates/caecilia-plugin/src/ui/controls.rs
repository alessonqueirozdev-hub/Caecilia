//! The parts of the console the organist plays: the jambs, the key slip,
//! the shoes, the pedalboard and its toe studs, the status bar's buttons.
//!
//! Each works out where its controls are once, in a plan, and both draws
//! and answers the pointer from that plan, so what is seen is what is hit.

use std::rc::Rc;

use caecilia_model::TonalFamily;
use caecilia_registration::FACTORY_GENERALS;
use manguebyte_gfx::{
    Circle, Color, Extend, Point, RadialGradient, Rect, RoundedRect, Scene, Shadow, Size, Stop,
    Stroke, TextLayout, Transform,
};
use manguebyte_plugin::editor::Gesture;
use manguebyte_ui::access::Role;
use manguebyte_ui::{Button, Event, Handled, IntoView, View};

use super::actions;
use super::live::{Item, live};
use super::paint::{
    PistonKind, StopFace, arrow, drawstop, jamb, piston, plate, polygon, shoe, style, tablet, text,
    vertical, width_of,
};
use super::snapshot::{Section, Snapshot, Source, describe_stops, note_name};
use crate::console::{Action, GENERALS};

/// A jamb's width.
pub(crate) const JAMB: f32 = 500.0;

/// A drawstop's cell in a jamb.
const CELL: f32 = 80.0;

/// A row of drawstops.
const ROW: f32 = 90.0;

/// Drawstops in a row.
const COLUMNS: usize = 6;

/// Where the keys begin, past the plate that names a keyboard.
pub(crate) const PLATE: f32 = 150.0;

/// The pedalboard's height.
pub(crate) const PEDALS: f32 = 124.0;

/// How far from its centre a drawstop or piston answers.
const STOP_REACH: f32 = 36.0;
const PISTON_REACH: f32 = 16.0;

fn pressed(event: &Event) -> bool {
    matches!(
        event,
        Event::PointerDown {
            button: Button::Primary,
            ..
        }
    )
}

fn released(event: &Event) -> bool {
    matches!(event, Event::PointerUp { .. } | Event::PointerCancel)
}

fn around(c: Point, r: f32) -> Rect {
    Rect::new(c.x - r, c.y - r, 2.0 * r, 2.0 * r)
}

fn near(a: Point, b: Point, r: f32) -> bool {
    let (dx, dy) = (a.x - b.x, a.y - b.y);
    dx * dx + dy * dy <= r * r
}

// ---------------------------------------------------------------- jambs

/// What a jamb control does.
#[derive(Clone, Copy, PartialEq, Debug)]
enum JambControl {
    Stop(usize),
    Coupler(usize),
    Tremulant,
}

/// Where everything on a jamb goes, in the jamb's coordinates before it is
/// scaled by `k` to fit.
struct JambPlan {
    k: f32,
    plates: Vec<(Rect, String, String)>,
    stops: Vec<(usize, Point)>,
    tablets: Vec<(JambControl, Rect, String)>,
}

fn columns_for(section: &Section) -> usize {
    if section.stops.len() <= 8 && section.stops.len() > COLUMNS {
        4
    } else {
        COLUMNS
    }
}

/// A division's name as a coupler tablet has room for it.
fn short(name: &str) -> String {
    match name {
        "Grand-Orgue" => "G.-O.".into(),
        "Pédale" => "Péd.".into(),
        "Récit" => "Réc.".into(),
        "Positif" => "Pos.".into(),
        other if other.chars().count() > 6 => {
            let cut: String = other.chars().take(4).collect();
            format!("{cut}.")
        }
        other => other.into(),
    }
}

fn jamb_plan(size: Size, snapshot: &Snapshot, right: bool) -> JambPlan {
    let layout = &snapshot.layout;
    let sections = if right { &layout.right } else { &layout.left };
    let tablets = right && (!snapshot.organ.couplers().is_empty() || layout.tremulant);
    let sections_height: f32 = sections
        .iter()
        .map(|s| 26.0 + 8.0 + s.stops.len().div_ceil(columns_for(s)) as f32 * ROW + 16.0)
        .sum();
    let needed = 16.0 + sections_height + if tablets { 42.0 + 60.0 } else { 0.0 };
    let room = size.height - 8.0;
    let k = if needed > room { room / needed } else { 1.0 };
    let rect = Rect::new(0.0, 0.0, size.width / k, room / k);

    let mut plan = JambPlan {
        k,
        plates: Vec::new(),
        stops: Vec::new(),
        tablets: Vec::new(),
    };
    let mut y = 16.0;
    for section in sections {
        plan.plates.push((
            Rect::new(rect.x + 20.0, y, rect.width - 40.0, 26.0),
            section.title.clone(),
            section.note.clone(),
        ));
        let columns = columns_for(section);
        let mut cy = y + 26.0 + ROW / 2.0 + 4.0;
        for chunk in section.stops.chunks(columns) {
            let w = CELL * chunk.len() as f32;
            let x0 = rect.center().x - w / 2.0 + CELL / 2.0;
            for (i, &n) in chunk.iter().enumerate() {
                plan.stops.push((n, Point::new(x0 + CELL * i as f32, cy)));
            }
            cy += ROW;
        }
        y = cy - ROW / 2.0 - 4.0 + 16.0;
    }
    if tablets {
        plan.plates.push((
            Rect::new(rect.x + 20.0, y, rect.width - 40.0, 26.0),
            "COUPLERS".into(),
            String::new(),
        ));
        y += 42.0;
        let mut list: Vec<(JambControl, String)> = snapshot
            .organ
            .couplers()
            .iter()
            .enumerate()
            .map(|(n, c)| {
                let line = match c.name.split_once('/') {
                    Some((from, to)) => format!("{} / {}", short(from), short(to)),
                    None => c.name.clone(),
                };
                (JambControl::Coupler(n), line)
            })
            .collect();
        if layout.tremulant {
            list.push((JambControl::Tremulant, "Tremulant".into()));
        }
        let gap = 12.0;
        let count = list.len() as f32;
        let tw = ((rect.width - 40.0 - gap * (count - 1.0)) / count).min(96.0);
        let total = count * tw + (count - 1.0) * gap;
        let mut x = rect.center().x - total / 2.0;
        for (control, line) in list {
            plan.tablets
                .push((control, Rect::new(x, y, tw, 52.0), line));
            x += tw + gap;
        }
    }
    plan
}

fn jamb_hit(plan: &JambPlan, at: Point) -> Option<JambControl> {
    let p = Point::new(at.x / plan.k, at.y / plan.k);
    plan.stops
        .iter()
        .find(|(_, c)| near(*c, p, STOP_REACH))
        .map(|(n, _)| JambControl::Stop(*n))
        .or_else(|| {
            plan.tablets
                .iter()
                .find(|(_, r, _)| r.contains(p))
                .map(|(c, _, _)| *c)
        })
}

fn jamb_items(snapshot: &Snapshot, size: Size, right: bool) -> Vec<Item> {
    let plan = jamb_plan(size, snapshot, right);
    let scaled = |r: Rect| {
        Rect::new(
            r.x * plan.k,
            r.y * plan.k,
            r.width * plan.k,
            r.height * plan.k,
        )
    };
    let organ = &snapshot.organ;
    let mut items: Vec<Item> = plan
        .stops
        .iter()
        .filter_map(|(n, c)| {
            let stop = organ.stops().get(*n)?;
            let division = organ
                .division(stop.division)
                .map_or("", |d| d.name.as_str());
            Some(Item {
                role: Role::Switch,
                name: format!("{division} {}", stop.name),
                checked: snapshot.drawn.get(*n).copied(),
                rect: scaled(around(*c, STOP_REACH)),
            })
        })
        .collect();
    for (control, rect, _) in &plan.tablets {
        let (name, on) = match control {
            JambControl::Coupler(n) => (
                organ
                    .couplers()
                    .get(*n)
                    .map_or_else(String::new, |c| c.name.clone()),
                snapshot.couplers.get(*n).copied(),
            ),
            JambControl::Tremulant => ("Tremulant".into(), Some(snapshot.tremulant)),
            JambControl::Stop(_) => continue,
        };
        items.push(Item {
            role: Role::Switch,
            name,
            checked: on,
            rect: scaled(*rect),
        });
    }
    items
}

/// A jamb: its sections, and on the right the couplers and tremulant. A
/// click draws or retires.
pub(crate) fn jamb_view(source: &Rc<Source>, right: bool) -> View {
    let name = if right { "Right jamb" } else { "Left jamb" };
    let shared = source.shared().clone();
    let pointing = Rc::clone(source);
    let learner = Rc::clone(source);
    live(
        name,
        Size::new(JAMB, 400.0),
        source,
        Snapshot::stops_key,
        move |size, scene, snapshot| {
            let look = snapshot.look;
            jamb(scene, &look, Rect::new(0.0, 0.0, size.width, size.height));
            let plan = jamb_plan(size, snapshot, right);
            scene.transformed(Transform::scale(plan.k, plan.k), |scene| {
                for (rect, title, note) in &plan.plates {
                    plate(scene, &look, *rect, title, note);
                }
                for (n, c) in &plan.stops {
                    let Some(stop) = snapshot.organ.stops().get(*n) else {
                        continue;
                    };
                    let face = StopFace::of(
                        &stop.name,
                        stop.family == TonalFamily::Reed,
                        snapshot.drawn.get(*n).copied().unwrap_or(false),
                    );
                    drawstop(scene, &look, *c, &face);
                }
                for (control, rect, line) in &plan.tablets {
                    let on = match control {
                        JambControl::Coupler(n) => {
                            snapshot.couplers.get(*n).copied().unwrap_or(false)
                        }
                        JambControl::Tremulant => snapshot.tremulant,
                        JambControl::Stop(_) => false,
                    };
                    tablet(scene, &look, *rect, &[line.as_str()], on);
                }
            });
        },
    )
    .describe(if right {
        |s| describe_stops(s, &s.layout.right, true)
    } else {
        |s| describe_stops(s, &s.layout.left, false)
    })
    .items(if right {
        |s, size| jamb_items(s, size, true)
    } else {
        |s, size| jamb_items(s, size, false)
    })
    .hover(move |at, size, snapshot| {
        let stop = at.and_then(|at| match jamb_hit(&jamb_plan(size, snapshot, right), at) {
            Some(JambControl::Stop(n)) => Some(n),
            _ => None,
        });
        pointing.point_at(stop);
    })
    .input(move |cx, event, at, size, snapshot| {
        if !pressed(event) {
            return Handled::No;
        }
        let plan = jamb_plan(size, snapshot, right);
        match jamb_hit(&plan, at) {
            Some(JambControl::Stop(n)) if learner.learn(Action::ToggleStop(n as u16)) => {}
            Some(JambControl::Coupler(n)) if learner.learn(Action::ToggleCoupler(n as u16)) => {}
            Some(JambControl::Stop(n)) => actions::toggle_stop(cx.context(), &shared, n),
            Some(JambControl::Coupler(n)) => actions::toggle_coupler(cx.context(), &shared, n),
            Some(JambControl::Tremulant) => actions::toggle_tremulant(cx.context(), &shared),
            None => return Handled::No,
        }
        Handled::Yes
    })
    .width(JAMB)
    .into_view()
}

// ---------------------------------------------------------------- key slip

/// What a piston in the key slip does.
#[derive(Clone, Copy, PartialEq, Debug)]
enum SlipControl {
    General(usize),
    Previous,
    Next,
    Cancel,
    Set,
}

struct SlipPlan {
    wood: Rect,
    pistons: Vec<(SlipControl, Point)>,
    display: Rect,
}

const SLIP_RADIUS: f32 = 13.0;

fn slip_plan(size: Size) -> SlipPlan {
    let wood = Rect::new(PLATE + 14.0, 0.0, size.width - PLATE - 14.0, size.height);
    let step = 37.0;
    let seq_w = 150.0;
    let total = 16.0 * step + 26.0 + 2.0 * step + seq_w + 26.0 + 2.0 * step;
    let mut x = wood.center().x - total / 2.0 + step / 2.0;
    let y = wood.y + 21.0;
    let mut pistons = Vec::new();
    for n in 0..GENERALS {
        pistons.push((SlipControl::General(n), Point::new(x, y)));
        x += step;
        if n == 7 {
            x += 12.0;
        }
    }
    x += 14.0;
    pistons.push((SlipControl::Previous, Point::new(x, y)));
    x += step / 2.0 + 6.0;
    let display = Rect::new(x, y - 15.0, seq_w - 12.0, 30.0);
    x = display.right() + 6.0 + step / 2.0;
    pistons.push((SlipControl::Next, Point::new(x, y)));
    x += step + 14.0;
    pistons.push((SlipControl::Cancel, Point::new(x, y)));
    x += step;
    pistons.push((SlipControl::Set, Point::new(x, y)));
    SlipPlan {
        wood,
        pistons,
        display,
    }
}

fn slip_name(control: SlipControl) -> String {
    match control {
        SlipControl::General(n) => format!("General {}", n + 1),
        SlipControl::Previous => "Previous general".into(),
        SlipControl::Next => "Next general".into(),
        SlipControl::Cancel => "Cancel".into(),
        SlipControl::Set => "Set".into(),
    }
}

/// What a general piston does: recall it, or store in it with Set pressed.
fn general_pressed(cx: &mut manguebyte_ui::EventCx<'_>, source: &Source, n: usize) {
    if source.learn(Action::General(n as u16)) {
        return;
    }
    if source.set_armed() {
        actions::capture(source.shared(), n);
        source.arm_set(false);
    } else {
        actions::recall(cx.context(), source.shared(), n);
    }
}

/// The key slip's height.
pub(crate) const SLIP: f32 = 58.0;

/// The key slip: the general pistons, the sequencer's display and arrows,
/// cancel and set, drawn in a strip `size` large.
pub(crate) fn draw_slip(size: Size, scene: &mut Scene, snapshot: &Snapshot) {
    let look = snapshot.look;
    let plan = slip_plan(size);
    let rect = plan.wood;
    let wood = [
        Stop::new(0.0, look.wood_top),
        Stop::new(1.0, look.wood_bottom),
    ];
    scene.fill(RoundedRect::new(rect, 6.0), vertical(rect, &wood));
    scene.fill(Rect::new(rect.x, rect.y, rect.width, 1.0), look.sheen);
    let current = snapshot.general;
    let under = |scene: &mut Scene, c: Point, label: &str, color: Color| {
        text(
            scene,
            label,
            style(9.5, 650.0, color),
            Point::new(c.x, c.y + 17.0),
            0.5,
        );
    };
    for (control, c) in &plan.pistons {
        let c = *c;
        match *control {
            SlipControl::General(n) => {
                let kind = if n < 8 {
                    PistonKind::Ivory
                } else {
                    PistonKind::Ebony
                };
                let lit = current == Some(n);
                let label = format!("{}", n + 1);
                piston(scene, &look, c, SLIP_RADIUS, &label, kind, lit);
                let tag = if n < 8 {
                    FACTORY_GENERALS.get(n).map_or("", |g| g.tag)
                } else if snapshot.generals_set[n] {
                    "set"
                } else {
                    ""
                };
                let color = if lit { look.accent } else { look.text2 };
                under(scene, c, tag, color);
            }
            SlipControl::Previous | SlipControl::Next => {
                piston(scene, &look, c, SLIP_RADIUS, "", PistonKind::Ebony, false);
                arrow(scene, c, *control == SlipControl::Next, look.ivory);
            }
            SlipControl::Cancel => {
                piston(scene, &look, c, SLIP_RADIUS, "0", PistonKind::Ivory, false);
                under(scene, c, "cancel", look.text2);
            }
            SlipControl::Set => {
                let armed = snapshot.set_armed;
                piston(scene, &look, c, SLIP_RADIUS, "S", PistonKind::Set, armed);
                let color = if armed { look.accent } else { look.text2 };
                under(scene, c, if armed { "choose" } else { "set" }, color);
            }
        }
    }
    // The sequencer's display.
    let display = plan.display;
    let shape = RoundedRect::new(display, 6.0);
    let lcd = Color::hex(0xF2B95A);
    scene.fill(shape, Color::hex(0x0B0806));
    scene.shadow(shape, Shadow::glow(8.0, lcd.with_alpha(0x20)));
    let tag = |n: usize| FACTORY_GENERALS.get(n).map(|g| g.tag);
    let (number, line) = if snapshot.set_armed {
        ("S".to_string(), "press a general".to_string())
    } else {
        match current {
            Some(n) => {
                let next = (n + 1).min(GENERALS - 1);
                let here = tag(n).map_or_else(|| "general".to_string(), str::to_string);
                let line = match tag(next) {
                    Some(t) if next != n => format!("{here}  \u{b7}  next {t}"),
                    _ => here,
                };
                (format!("{}", n + 1), line)
            }
            None => ("\u{2013}".to_string(), "no general yet".to_string()),
        }
    };
    let w = text(
        scene,
        &number,
        style(17.0, 700.0, lcd),
        Point::new(display.x + 12.0, display.y + 4.0),
        0.0,
    )
    .width;
    text(
        scene,
        &line,
        style(11.0, 600.0, lcd.with_alpha(0xD0)),
        Point::new(display.x + 20.0 + w, display.y + 8.0),
        0.0,
    );
}

/// What the key slip says it shows.
pub(crate) fn describe_slip(s: &Snapshot) -> String {
    let general = match s.general {
        Some(n) => format!("General {} recalled", n + 1),
        None => "no general recalled".into(),
    };
    if s.set_armed {
        format!("{general}; Set pressed")
    } else {
        general
    }
}

/// The key slip's pistons, for assistive technology and tests.
pub(crate) fn slip_items(size: Size) -> Vec<Item> {
    slip_plan(size)
        .pistons
        .into_iter()
        .map(|(control, c)| Item {
            role: Role::Button,
            name: slip_name(control),
            checked: None,
            rect: around(c, PISTON_REACH),
        })
        .collect()
}

/// A press at `at` in the key slip.
pub(crate) fn slip_press(
    cx: &mut manguebyte_ui::EventCx<'_>,
    source: &Source,
    at: Point,
    size: Size,
) -> Handled {
    let plan = slip_plan(size);
    let Some((control, _)) = plan
        .pistons
        .iter()
        .find(|(_, c)| near(*c, at, PISTON_REACH))
    else {
        return Handled::No;
    };
    let shared = source.shared();
    match *control {
        SlipControl::General(n) => general_pressed(cx, source, n),
        SlipControl::Previous if source.learn(Action::PreviousGeneral) => {}
        SlipControl::Next if source.learn(Action::NextGeneral) => {}
        SlipControl::Cancel if source.learn(Action::Cancel) => {}
        SlipControl::Previous => actions::step(cx.context(), shared, false),
        SlipControl::Next => actions::step(cx.context(), shared, true),
        SlipControl::Cancel => actions::cancel(cx.context(), shared),
        SlipControl::Set => source.arm_set(!source.set_armed()),
    }
    Handled::Yes
}

// ---------------------------------------------------------------- shoes

/// Where the two shoes stand: the swell's, the crescendo's.
fn shoe_rects(size: Size) -> [Rect; 2] {
    let w = 46.0;
    let h = size.height - 42.0;
    let slot = size.width / 2.0;
    [0, 1].map(|i| Rect::new(slot * i as f32 + slot / 2.0 - w / 2.0 - 4.0, 4.0, w, h))
}

/// Where along a shoe the pointer is: 0 shut, 1 open.
fn shoe_position(rect: Rect, at: Point) -> f32 {
    ((rect.bottom() - at.y) / rect.height).clamp(0.0, 1.0)
}

/// A shoe being moved.
enum ShoeDrag {
    Swell(Gesture),
    /// The crescendo, and the step it stands at.
    Crescendo(usize),
}

/// Left of the pedalboard: the swell shoe and the crescendo. Dragged up
/// they open; the crescendo recalls the factory generals on its way.
pub(crate) fn shoes(source: &Rc<Source>) -> View {
    let shared = source.shared().clone();
    let mut drag: Option<ShoeDrag> = None;
    live(
        "Shoes",
        Size::new(PLATE, PEDALS),
        source,
        Snapshot::shoes_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let swell = snapshot.swell.clamp(0.0, 1.0);
            let (step, tag) = match snapshot.general {
                Some(n) if n < 8 => (n + 1, FACTORY_GENERALS.get(n).map_or("", |g| g.tag)),
                _ => (0, ""),
            };
            let crescendo = if step == 0 {
                "\u{2013}".to_string()
            } else {
                format!("{step} \u{b7} {tag}")
            };
            let [a, b] = shoe_rects(size);
            let swell_value = format!("{:.0} %", swell * 100.0);
            shoe(scene, &look, a, swell, &snapshot.layout.swell, &swell_value);
            shoe(scene, &look, b, step as f32 / 8.0, "Crescendo", &crescendo);
        },
    )
    .items(|s, size| {
        let [a, b] = shoe_rects(size);
        vec![
            Item {
                role: Role::Slider,
                name: s.layout.swell.clone(),
                checked: None,
                rect: a,
            },
            Item {
                role: Role::Slider,
                name: "Crescendo".into(),
                checked: None,
                rect: b,
            },
        ]
    })
    .input(move |cx, event, at, size, _snapshot| {
        let [a, b] = shoe_rects(size);
        // The level bar beside a shoe takes the pointer too.
        let reach = |r: Rect| Rect::new(r.x - 4.0, r.y, r.width + 20.0, r.height);
        if pressed(event) {
            if reach(a).contains(at) {
                let mut gesture = actions::swell_gesture(cx.context(), &shared);
                gesture.set(f64::from(shoe_position(a, at)));
                drag = Some(ShoeDrag::Swell(gesture));
            } else if reach(b).contains(at) {
                drag = Some(ShoeDrag::Crescendo(0));
            } else {
                return Handled::No;
            }
            cx.capture_pointer();
        }
        match drag.as_mut() {
            Some(ShoeDrag::Swell(gesture)) if at.y.is_finite() => {
                gesture.set(f64::from(shoe_position(a, at)));
            }
            Some(ShoeDrag::Crescendo(step)) if at.y.is_finite() => {
                let now = (shoe_position(b, at) * 8.0).round() as usize;
                if now != *step && now > 0 {
                    actions::recall(cx.context(), &shared, now - 1);
                }
                *step = now;
            }
            Some(_) => {}
            None => return Handled::No,
        }
        if released(event) {
            if let Some(ShoeDrag::Swell(gesture)) = drag.take() {
                gesture.end();
            }
            drag = None;
            cx.release_pointer();
        }
        Handled::Yes
    })
    .width(PLATE)
    .height(PEDALS)
    .into_view()
}

// ---------------------------------------------------------------- pedalboard

fn is_sharp(note: u8) -> bool {
    matches!(note % 12, 1 | 3 | 6 | 8 | 10)
}

/// The pedalboard in perspective: where a point of the board is, `u`
/// across from 0 to 1, `v` away from the player, 0 at the front edge and 1
/// under the console; and which key is under a point.
struct Board {
    near_y: f32,
    far_y: f32,
    near_half: f32,
    far_half: f32,
    cx: f32,
    low: u8,
    high: u8,
    naturals: Vec<u8>,
    slot: f32,
}

impl Board {
    fn new(size: Size, low: u8, high: u8) -> Board {
        let near_half = size.width * 0.37;
        let high = high.max(low);
        let naturals: Vec<u8> = (low..=high).filter(|n| !is_sharp(*n)).collect();
        let slot = 1.0 / naturals.len().max(1) as f32;
        Board {
            near_y: size.height - 12.0,
            far_y: 8.0,
            near_half,
            far_half: near_half * 0.92,
            cx: size.width / 2.0,
            low,
            high,
            naturals,
            slot,
        }
    }

    fn at(&self, u: f32, v: f32) -> Point {
        let half = self.near_half + (self.far_half - self.near_half) * v;
        Point::new(
            self.cx + (u - 0.5) * 2.0 * half,
            self.near_y + (self.far_y - self.near_y) * v,
        )
    }

    /// `(u, v)` of a point.
    fn uv(&self, p: Point) -> (f32, f32) {
        let v = (self.near_y - p.y) / (self.near_y - self.far_y);
        let half = self.near_half + (self.far_half - self.near_half) * v;
        (((p.x - self.cx) / (2.0 * half)) + 0.5, v)
    }

    /// Each sharp, and the `u` of its middle.
    fn sharps(&self) -> Vec<(u8, f32)> {
        let mut index = 0;
        let mut out = Vec::new();
        for note in self.low..=self.high {
            if is_sharp(note) {
                out.push((note, self.slot * index as f32));
            } else {
                index += 1;
            }
        }
        out
    }

    /// The key under `p`: a sharp first, where they stand, then a natural.
    fn key_at(&self, p: Point) -> Option<u8> {
        let (u, v) = self.uv(p);
        if !(0.0..=1.04).contains(&v) || !(0.0..=1.0).contains(&u) {
            return None;
        }
        if v >= 0.45 {
            if let Some((note, _)) = self
                .sharps()
                .into_iter()
                .find(|(_, c)| (u - c).abs() <= self.slot * 0.27)
            {
                return Some(note);
            }
        }
        let i = (u / self.slot) as usize;
        let within = u / self.slot - i as f32;
        (0.12..=0.88)
            .contains(&within)
            .then(|| self.naturals.get(i).copied())
            .flatten()
    }

    /// The toe studs' centres: generals 1 to 8, right of the board.
    fn studs(&self) -> Vec<Point> {
        let right = self.at(1.0, 0.0).x + 40.0;
        let middle = (self.near_y + self.far_y) / 2.0 + 2.0;
        (0..8)
            .map(|n| {
                let (row, col) = (n / 4, n % 4);
                Point::new(right + col as f32 * 30.0, middle - 16.0 + row as f32 * 34.0)
            })
            .collect()
    }
}

/// The pedalboard as the organist sees it from the bench, the notes held
/// lit; its plate on one side, the toe studs on the other. A pedal pressed
/// sounds; a stud recalls its general.
pub(crate) fn pedalboard(source: &Rc<Source>) -> View {
    let here = Rc::clone(source);
    let mut sounding: Option<(usize, u8)> = None;
    live(
        "P\u{e9}dalier",
        Size::new(1200.0, PEDALS),
        source,
        Snapshot::pedal_key,
        |size, scene, snapshot| {
            let Some(pedal) = &snapshot.layout.pedal else {
                return;
            };
            draw_pedalboard(size, scene, snapshot, pedal.low, pedal.high);
        },
    )
    .describe(|s| {
        let Some(pedal) = &s.layout.pedal else {
            return "no pedalboard".into();
        };
        let held = pedal.channel.map_or(0, |c| s.held[c]);
        let notes: Vec<String> = (pedal.low..=pedal.high)
            .filter(|n| held & (1u128 << (n & 0x7F)) != 0)
            .map(note_name)
            .collect();
        if notes.is_empty() {
            "no pedal held".into()
        } else {
            format!("{} held", notes.join(", "))
        }
    })
    .items(|s, size| {
        let Some(pedal) = &s.layout.pedal else {
            return Vec::new();
        };
        let board = Board::new(size, pedal.low, pedal.high);
        let mut items: Vec<Item> = board
            .studs()
            .into_iter()
            .enumerate()
            .map(|(n, c)| Item {
                role: Role::Button,
                name: format!("Toe stud {}", n + 1),
                checked: None,
                rect: around(c, 10.0),
            })
            .collect();
        for (i, &note) in board.naturals.iter().enumerate() {
            let u = board.slot * (i as f32 + 0.5);
            let c = board.at(u, 0.2);
            items.push(Item {
                role: Role::Button,
                name: format!("Pedal {}", note_name(note)),
                checked: None,
                rect: around(c, 6.0),
            });
        }
        for (note, u) in board.sharps() {
            let c = board.at(u, 0.8);
            items.push(Item {
                role: Role::Button,
                name: format!("Pedal {}", note_name(note)),
                checked: None,
                rect: around(Point::new(c.x, c.y - 6.0), 5.0),
            });
        }
        items
    })
    .input(move |cx, event, at, size, snapshot| {
        let Some(pedal) = &snapshot.layout.pedal else {
            return Handled::No;
        };
        let board = Board::new(size, pedal.low, pedal.high);
        let shared = here.shared();
        let channel = pedal.channel;
        if pressed(event) {
            if let Some(n) = board.studs().iter().position(|c| near(*c, at, 12.0)) {
                general_pressed(cx, &here, n);
                return Handled::Yes;
            }
            let (Some(channel), Some(note)) = (channel, board.key_at(at)) else {
                return Handled::No;
            };
            actions::play(shared, channel, note, true);
            sounding = Some((channel, note));
            cx.capture_pointer();
            return Handled::Yes;
        }
        let Some((channel, note)) = sounding else {
            return Handled::No;
        };
        if released(event) {
            actions::play(shared, channel, note, false);
            sounding = None;
            cx.release_pointer();
        } else if let Some(next) = board.key_at(at).filter(|n| *n != note) {
            // Sliding to the next pedal, as a foot does.
            actions::play(shared, channel, note, false);
            actions::play(shared, channel, next, true);
            sounding = Some((channel, next));
        }
        Handled::Yes
    })
    .height(PEDALS)
    .into_view()
}

fn draw_pedalboard(size: Size, scene: &mut Scene, snapshot: &Snapshot, low: u8, high: u8) {
    let look = snapshot.look;
    let Some(pedal) = &snapshot.layout.pedal else {
        return;
    };
    let rect = Rect::new(0.0, 0.0, size.width, size.height);
    let board = Board::new(size, low, high);
    let held = pedal.channel.map_or(0, |c| snapshot.held[c]);
    let lit = |note: u8| held & (1u128 << (note & 0x7F)) != 0;
    let at = |u: f32, v: f32| board.at(u, v);
    let quad = |u0: f32, u1: f32, v0: f32, v1: f32| {
        polygon(&[at(u0, v1), at(u1, v1), at(u1, v0), at(u0, v0)])
    };
    let well = polygon(&[
        at(-0.01, 1.04),
        at(1.01, 1.04),
        Point::new(at(1.01, 0.0).x, rect.bottom()),
        Point::new(at(-0.01, 0.0).x, rect.bottom()),
    ]);
    scene.fill(
        &well,
        Color::hex_rgba(if look.night { 0x0000_00A0 } else { 0x2A1A_0C60 }),
    );
    let oak = [
        Stop::new(0.0, Color::hex(0x8A6438)),
        Stop::new(0.5, Color::hex(0xB98F5C)),
        Stop::new(1.0, Color::hex(0xD8B47E)),
    ];
    let span = Rect::new(rect.x, board.far_y, rect.width, board.near_y - board.far_y);
    let slot = board.slot;
    for (i, &note) in board.naturals.iter().enumerate() {
        let (u0, u1) = (slot * (i as f32 + 0.2), slot * (i as f32 + 0.8));
        let top = quad(u0, u1, 0.0, 1.0);
        scene.shadow(
            &top,
            Shadow::new(Color::hex_rgba(0x0000_00A0))
                .with_offset(0.0, 3.0)
                .with_blur(5.0),
        );
        scene.fill(&top, vertical(span, &oak));
        scene.stroke(&top, Stroke::new(0.8), Color::hex_rgba(0x3A24_1080));
        let (a, b) = (at(u0, 0.0), at(u1, 0.0));
        let front = polygon(&[a, b, Point::new(b.x, b.y + 6.0), Point::new(a.x, a.y + 6.0)]);
        scene.fill(&front, Color::hex(0x6B4A26));
        if lit(note) {
            scene.shadow(&top, Shadow::glow(12.0, look.accent.with_alpha(0xA0)));
            scene.fill(&top, look.accent.with_alpha(0x99));
            scene.fill(&front, look.accent.with_alpha(0x70));
        }
        if note % 12 == 0 {
            let p = at((u0 + u1) / 2.0, 0.08);
            text(
                scene,
                &note_name(note),
                style(9.5, 700.0, Color::hex(0x4A3018)),
                Point::new(p.x, p.y - 12.0),
                0.5,
            );
        }
    }
    for (note, centre) in board.sharps() {
        let (u0, u1) = (centre - slot * 0.25, centre + slot * 0.25);
        let (v0, v1) = (0.5, 1.0);
        let lift = 6.0;
        let raise = |p: Point| Point::new(p.x, p.y - lift);
        let top = polygon(&[
            raise(at(u0, v1)),
            raise(at(u1, v1)),
            raise(at(u1, v0)),
            raise(at(u0, v0)),
        ]);
        let face = polygon(&[raise(at(u0, v0)), raise(at(u1, v0)), at(u1, v0), at(u0, v0)]);
        scene.shadow(
            &quad(u0, u1, v0, v1),
            Shadow::new(Color::hex_rgba(0x0000_00D0))
                .with_offset(0.0, 6.0)
                .with_blur(6.0),
        );
        let ebony = [
            Stop::new(0.0, Color::hex(0x120D0A)),
            Stop::new(1.0, Color::hex(0x3A2E26)),
        ];
        scene.fill(&face, Color::hex(0x5A4636));
        scene.fill(
            &top,
            vertical(
                Rect::new(
                    rect.x,
                    at(0.0, v1).y - lift,
                    rect.width,
                    at(0.0, v0).y - at(0.0, v1).y,
                ),
                &ebony,
            ),
        );
        scene.stroke(&top, Stroke::new(1.0), Color::hex(0x6E5A48));
        if lit(note) {
            scene.fill(&top, look.accent.with_alpha(0xA0));
        }
    }
    let left = at(0.0, 0.0).x;
    plate(
        scene,
        &look,
        Rect::new(left - 156.0, rect.center().y - 13.0, 140.0, 26.0),
        &pedal.title,
        &pedal.note,
    );
    for (n, c) in board.studs().into_iter().enumerate() {
        scene.shadow(
            Circle::new(c, 10.0),
            Shadow::new(Color::hex_rgba(0x0000_00B0))
                .with_offset(0.0, 3.0)
                .with_blur(4.0),
        );
        let brass = [
            Stop::new(0.0, look.brass_light),
            Stop::new(0.6, look.brass),
            Stop::new(1.0, look.brass_dark),
        ];
        scene.fill(
            Circle::new(c, 10.0),
            RadialGradient {
                center: Point::new(c.x - 3.0, c.y - 4.0),
                radius: 16.0,
                stops: &brass,
                extend: Extend::Pad,
            },
        );
        if snapshot.general == Some(n) {
            scene.shadow(
                Circle::new(c, 11.0),
                Shadow::glow(10.0, look.accent.with_alpha(0xB0)),
            );
            scene.stroke(Circle::new(c, 11.5), Stroke::new(1.5), look.brass_light);
        }
        let label = format!("{}", n + 1);
        let s = style(9.5, 760.0, look.plate_ink);
        let h = TextLayout::new(&label, &s, None).size().height;
        text(scene, &label, s, Point::new(c.x, c.y - h / 2.0), 0.5);
    }
}

// ---------------------------------------------------------------- status

/// The status bar's buttons.
#[derive(Clone, Copy, PartialEq, Debug)]
enum StatusButton {
    Undo,
    Redo,
    Light,
    Sound,
    Demos,
    Organ,
}

const STATUS_BUTTONS: [StatusButton; 6] = [
    StatusButton::Undo,
    StatusButton::Redo,
    StatusButton::Light,
    StatusButton::Sound,
    StatusButton::Demos,
    StatusButton::Organ,
];

/// A status button's label: the light it would change to, for Light.
fn status_label(button: StatusButton, snapshot: &Snapshot) -> &'static str {
    match button {
        StatusButton::Undo => "Undo",
        StatusButton::Redo => "Redo",
        StatusButton::Light if snapshot.look.night => "Day",
        StatusButton::Light => "Night",
        StatusButton::Sound => "Sound",
        StatusButton::Demos => "Demos",
        StatusButton::Organ => "Organ\u{2026}",
    }
}

/// A status chip: where, what, and the colour of its dot if it has one.
type Chip = (Rect, String, Option<Color>);

/// Where the chips and buttons go, right to left from the end.
fn status_plan(size: Size, snapshot: &Snapshot) -> (Vec<Chip>, Vec<(StatusButton, Rect)>) {
    let look = snapshot.look;
    let s = style(12.5, 500.0, look.text);
    let y = (size.height - 30.0) / 2.0;
    let mut x = size.width;
    let mut buttons = Vec::new();
    for button in STATUS_BUTTONS.iter().rev() {
        let w = width_of(status_label(*button, snapshot), s) + 32.0;
        x -= w;
        buttons.push((*button, Rect::new(x, y, w, 30.0)));
        x -= 10.0;
    }
    buttons.reverse();
    let wind = if snapshot.wind_steady {
        ("Wind steady".to_string(), look.good)
    } else {
        ("Wind under load".to_string(), look.accent)
    };
    let chips_in = [
        (snapshot.tuning.clone(), Some(look.accent)),
        (wind.0, Some(wind.1)),
        (snapshot.cpu.clone(), None),
    ];
    let mut chips = Vec::new();
    x -= 8.0;
    for (label, dot) in chips_in.into_iter().rev() {
        let w = width_of(&label, s) + 32.0 + if dot.is_some() { 14.0 } else { 0.0 };
        x -= w;
        chips.push((Rect::new(x, y, w, 30.0), label, dot));
        x -= 12.0;
    }
    (chips, buttons)
}

/// The status chips (the tuning, the wind, the load) and the buttons:
/// undo and redo through the history, and the organ.
pub(crate) fn status(source: &Rc<Source>) -> View {
    let shared = source.shared().clone();
    let here = Rc::clone(source);
    live(
        "Status",
        Size::new(900.0, 34.0),
        source,
        |s| {
            s.status_key()
                ^ s.registration_key()
                ^ u64::from(s.library.is_some())
                ^ (u64::from(s.demos.is_some()) << 1)
        },
        |size, scene, snapshot| {
            let look = snapshot.look;
            let s = style(12.5, 500.0, look.text);
            let (chips, buttons) = status_plan(size, snapshot);
            let face = if look.night {
                Color::hex(0x2A2019)
            } else {
                Color::hex(0xFBF6EC)
            };
            for (r, label, dot) in &chips {
                let shape = RoundedRect::new(*r, 15.0);
                scene.fill(shape, face);
                scene.stroke(shape, Stroke::new(1.0), look.edge);
                let mut tx = r.x + 16.0;
                if let Some(color) = dot {
                    let c = Point::new(tx + 3.0, r.center().y);
                    scene.shadow(
                        Circle::new(c, 3.5),
                        Shadow::glow(6.0, color.with_alpha(0x90)),
                    );
                    scene.fill(Circle::new(c, 3.5), *color);
                    tx += 14.0;
                }
                text(scene, label, s, Point::new(tx, r.y + 7.0), 0.0);
            }
            for (button, r) in &buttons {
                let label = status_label(*button, snapshot);
                let shape = RoundedRect::new(*r, 15.0);
                let primary = matches!(button, StatusButton::Organ | StatusButton::Demos);
                if primary {
                    let stops = [
                        Stop::new(0.0, look.brass_light),
                        Stop::new(1.0, look.accent),
                    ];
                    scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x60)));
                    scene.fill(shape, vertical(*r, &stops));
                } else {
                    scene.fill(shape, face);
                    scene.stroke(shape, Stroke::new(1.0), look.edge);
                }
                let ink = if primary { look.plate_ink } else { look.text };
                text(
                    scene,
                    label,
                    style(12.5, 600.0, ink),
                    Point::new(r.center().x, r.y + 7.0),
                    0.5,
                );
            }
        },
    )
    .items(|s, size| {
        let (_, buttons) = status_plan(size, s);
        buttons
            .into_iter()
            .map(|(button, rect)| Item {
                role: Role::Button,
                name: status_label(button, s).to_string(),
                checked: None,
                rect,
            })
            .collect()
    })
    .input(move |cx, event, at, size, snapshot| {
        if !pressed(event) {
            return Handled::No;
        }
        let (_, buttons) = status_plan(size, snapshot);
        match buttons
            .iter()
            .find(|(_, r)| r.contains(at))
            .map(|(b, _)| *b)
        {
            Some(StatusButton::Undo) => actions::undo(cx.context(), &shared),
            Some(StatusButton::Redo) => actions::redo(cx.context(), &shared),
            Some(StatusButton::Light) => {
                let params = &here.shared().params;
                params.set_console_day(!params.console_day());
                here.refresh();
            }
            Some(StatusButton::Sound) => here.toggle_sound(),
            Some(StatusButton::Demos) => here.toggle_demos(),
            Some(StatusButton::Organ) => here.toggle_library(),
            None => return Handled::No,
        }
        Handled::Yes
    })
    .width(900.0)
    .into_view()
}
