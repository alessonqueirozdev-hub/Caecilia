//! The console's parts, laid out as the approved mockup has them, each
//! drawn from the frame's reading.

use std::rc::Rc;

use caecilia_model::TonalFamily;
use caecilia_registration::FACTORY_GENERALS;
use manguebyte_gfx::{
    Circle, Color, Extend, Point, RadialGradient, Rect, RoundedRect, Scene, Shadow, Size, Stop,
    Stroke, TextLayout, Transform,
};
use manguebyte_ui::layout::Align;
use manguebyte_ui::{
    IntoView, KeySpan, KeyboardState, View, canvas, column, label, piano_keyboard_named, pill, row,
    stack,
};

use super::live::{KeysPoller, live};
use super::look::Look;
use super::paint::{
    PistonKind, StopFace, arrow, drawstop, gauge, heading, jamb, panel, piston, plate, polygon,
    shoe, style, tablet, text, vertical, width_of,
};
use super::snapshot::{Section, Snapshot, Source, describe_stops, note_name};
use crate::console::GENERALS;

/// A jamb's width.
const JAMB: f32 = 500.0;

/// A drawstop's cell in a jamb.
const CELL: f32 = 80.0;

/// A row of drawstops.
const ROW: f32 = 90.0;

/// Drawstops in a row.
const COLUMNS: usize = 6;

/// Where the keys begin, past the plate that names a keyboard.
const PLATE: f32 = 150.0;

/// The pedalboard's height.
const PEDALS: f32 = 124.0;

/// A manual's height.
const MANUAL: f32 = 96.0;

/// A division's plate and its stops in rows; returns where the next thing
/// goes.
fn division(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    y: f32,
    section: &Section,
    snapshot: &Snapshot,
) -> f32 {
    plate(
        scene,
        look,
        Rect::new(rect.x + 20.0, y, rect.width - 40.0, 26.0),
        &section.title,
        &section.note,
    );
    let columns = if section.stops.len() <= 8 && section.stops.len() > COLUMNS {
        4
    } else {
        COLUMNS
    };
    let mut y = y + 26.0 + ROW / 2.0 + 4.0;
    for chunk in section.stops.chunks(columns) {
        let w = CELL * chunk.len() as f32;
        let x0 = rect.center().x - w / 2.0 + CELL / 2.0;
        for (i, &n) in chunk.iter().enumerate() {
            let Some(stop) = snapshot.organ.stops().get(n) else {
                continue;
            };
            let face = StopFace::of(
                &stop.name,
                stop.family == TonalFamily::Reed,
                snapshot.drawn.get(n).copied().unwrap_or(false),
            );
            drawstop(scene, look, Point::new(x0 + CELL * i as f32, y), &face);
        }
        y += ROW;
    }
    y - ROW / 2.0 - 4.0
}

/// How tall a run of sections stands.
fn sections_height(sections: &[Section]) -> f32 {
    sections
        .iter()
        .map(|s| {
            let columns = if s.stops.len() <= 8 && s.stops.len() > COLUMNS {
                4
            } else {
                COLUMNS
            };
            26.0 + 8.0 + s.stops.len().div_ceil(columns) as f32 * ROW + 16.0
        })
        .sum()
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

/// Draw `draw` scaled down about the top centre of `rect`, so `height`
/// fits in it.
fn fitted(scene: &mut Scene, rect: Rect, height: f32, draw: impl FnOnce(&mut Scene, Rect)) {
    let k = if height > rect.height {
        rect.height / height
    } else {
        1.0
    };
    if k >= 1.0 {
        draw(scene, rect);
        return;
    }
    let inner = Rect::new(0.0, 0.0, rect.width / k, rect.height / k);
    let by = Transform::scale(k, k).then(Transform::translate(rect.x, rect.y));
    scene.transformed(by, |scene| draw(scene, inner));
}

/// A jamb: its sections, and on the right the couplers and tremulant.
fn jamb_view(source: &Rc<Source>, right: bool) -> View {
    let name = if right { "Right jamb" } else { "Left jamb" };
    live(
        name,
        Size::new(JAMB, 400.0),
        source,
        Snapshot::stops_key,
        move |size, scene, snapshot| {
            let look = snapshot.look;
            let rect = Rect::new(0.0, 0.0, size.width, size.height);
            jamb(scene, &look, rect);
            let layout = &snapshot.layout;
            let sections = if right { &layout.right } else { &layout.left };
            let tablets = right && (!snapshot.organ.couplers().is_empty() || layout.tremulant);
            let needed = 16.0 + sections_height(sections) + if tablets { 42.0 + 60.0 } else { 0.0 };
            let inner = Rect::new(0.0, 0.0, size.width, size.height - 8.0);
            fitted(scene, inner, needed, |scene, rect| {
                let mut y = 16.0;
                for section in sections {
                    y = division(scene, &look, rect, y, section, snapshot) + 16.0;
                }
                if !tablets {
                    return;
                }
                plate(
                    scene,
                    &look,
                    Rect::new(rect.x + 20.0, y, rect.width - 40.0, 26.0),
                    "COUPLERS",
                    "",
                );
                y += 42.0;
                let mut list: Vec<(Vec<String>, bool)> = snapshot
                    .organ
                    .couplers()
                    .iter()
                    .zip(&snapshot.couplers)
                    .map(|(c, on)| {
                        let line = match c.name.split_once('/') {
                            Some((from, to)) => format!("{} / {}", short(from), short(to)),
                            None => c.name.clone(),
                        };
                        (vec![line], *on)
                    })
                    .collect();
                if layout.tremulant {
                    list.push((vec!["Tremulant".into()], snapshot.tremulant));
                }
                let gap = 12.0;
                let tw = ((rect.width - 40.0 - gap * (list.len() as f32 - 1.0))
                    / list.len() as f32)
                    .min(96.0);
                let total = list.len() as f32 * tw + (list.len() as f32 - 1.0) * gap;
                let mut x = rect.center().x - total / 2.0;
                for (lines, on) in &list {
                    let lines: Vec<&str> = lines.iter().map(String::as_str).collect();
                    tablet(scene, &look, Rect::new(x, y, tw, 52.0), &lines, *on);
                    x += tw + gap;
                }
            });
        },
    )
    .describe(if right {
        |s| describe_stops(s, &s.layout.right, true)
    } else {
        |s| describe_stops(s, &s.layout.left, false)
    })
    .width(JAMB)
    .into_view()
}

/// The registration: the selector line, the history, why.
fn registration(source: &Rc<Source>) -> View {
    live(
        "Registration",
        Size::new(540.0, 184.0),
        source,
        Snapshot::registration_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let rect = Rect::new(0.0, 0.0, size.width, size.height);
            panel(scene, &look, rect);
            heading(scene, &look, rect, "Registration", &snapshot.summary);
            // The selector line.
            let field = Rect::new(rect.x + 20.0, rect.y + 44.0, rect.width - 40.0, 32.0);
            let shape = RoundedRect::new(field, 8.0);
            scene.fill(
                shape,
                if look.night {
                    Color::hex_rgba(0x0000_0066)
                } else {
                    Color::hex(0xFFFFFF)
                },
            );
            scene.stroke(shape, Stroke::new(1.2), look.edge);
            let y = field.y + 8.0;
            let x = field.x + 14.0;
            let w = text(
                scene,
                ">",
                style(12.5, 700.0, look.accent),
                Point::new(x, y),
                0.0,
            )
            .width;
            text(
                scene,
                "a selector: div:recit & family:string",
                style(12.5, 450.0, look.muted),
                Point::new(x + w + 10.0, y),
                0.0,
            );
            text(
                scene,
                "Enter draws \u{b7} Shift+Enter retires",
                style(10.5, 450.0, look.muted),
                Point::new(field.right() - 12.0, y + 1.5),
                1.0,
            );
            // The history.
            let y = field.bottom() + 14.0;
            let chip = style(10.5, 600.0, look.text);
            let mut x = rect.x + 20.0;
            let mut previous: Option<f32> = None;
            for (label, current) in &snapshot.history {
                let w = width_of(label, chip) + 18.0;
                if x + w > rect.right() - 20.0 {
                    break;
                }
                let r = Rect::new(x, y, w, 24.0);
                if let Some(px) = previous {
                    let mut p = manguebyte_gfx::Path::new();
                    p.move_to(Point::new(px, r.center().y));
                    p.line_to(Point::new(r.x, r.center().y));
                    scene.stroke(&p, Stroke::new(1.2), look.edge);
                }
                let shape = RoundedRect::new(r, 12.0);
                if *current {
                    scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x70)));
                    scene.fill(shape, look.accent.with_alpha(0x33));
                    scene.stroke(shape, Stroke::new(1.2), look.accent);
                } else {
                    scene.fill(shape, look.edge.with_alpha(0x80));
                }
                text(
                    scene,
                    label,
                    style(10.5, 600.0, if *current { look.text } else { look.text2 }),
                    Point::new(r.x + 9.0, r.y + 5.0),
                    0.0,
                );
                previous = Some(r.right());
                x = r.right() + 12.0;
            }
            // Why.
            let mut y = y + 36.0;
            for why in &snapshot.why {
                let dot = if why.drawn { look.accent } else { look.muted };
                scene.fill(Circle::new(Point::new(rect.x + 24.0, y + 7.0), 3.5), dot);
                let w = text(
                    scene,
                    &why.lead,
                    style(11.5, 650.0, look.text),
                    Point::new(rect.x + 34.0, y),
                    0.0,
                )
                .width;
                text(
                    scene,
                    &why.rest,
                    style(11.5, 450.0, look.text2),
                    Point::new(rect.x + 34.0 + w, y),
                    0.0,
                );
                y += 21.0;
            }
        },
    )
    .describe(|s| {
        let steps: Vec<&str> = s.history.iter().map(|(l, _)| l.as_str()).collect();
        let why: Vec<String> = s
            .why
            .iter()
            .map(|w| format!("{}{}", w.lead, w.rest))
            .collect();
        format!("{}; {}; {}", s.summary, steps.join(" > "), why.join("; "))
    })
    .height(184.0)
    .into_view()
}

/// The wind: a gauge for each chest.
fn wind(source: &Rc<Source>) -> View {
    live(
        "Wind",
        Size::new(540.0, 220.0),
        source,
        Snapshot::wind_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let rect = Rect::new(0.0, 0.0, size.width, size.height);
            panel(scene, &look, rect);
            heading(scene, &look, rect, "Wind", "millimetres of water, live");
            let chests = &snapshot.chests[..snapshot.chests.len().min(4)];
            if chests.is_empty() {
                return;
            }
            let w = rect.width / chests.len() as f32;
            let r = ((rect.height - 128.0) / 2.0)
                .clamp(30.0, 56.0)
                .min(w / 2.0 - 16.0);
            for (i, chest) in chests.iter().enumerate() {
                let c = Point::new(rect.x + w * (i as f32 + 0.5), rect.y + 44.0 + r);
                gauge(
                    scene,
                    &look,
                    c,
                    r,
                    &chest.name,
                    chest.nominal_mm,
                    chest.now_mm,
                    &chest.trace,
                    &chest.note,
                );
            }
        },
    )
    .describe(|s| {
        let chests: Vec<String> = s
            .chests
            .iter()
            .map(|c| format!("{} {:.1} mm, {}", c.name, c.now_mm, c.note))
            .collect();
        chests.join("; ")
    })
    .flex(1.0)
    .into_view()
}

/// The key slip: the general pistons, the sequencer, cancel and set.
fn key_slip(source: &Rc<Source>) -> View {
    live(
        "Generals",
        Size::new(1200.0, 58.0),
        source,
        Snapshot::generals_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let rect = Rect::new(PLATE + 14.0, 0.0, size.width - PLATE - 14.0, size.height);
            let wood = [
                Stop::new(0.0, look.wood_top),
                Stop::new(1.0, look.wood_bottom),
            ];
            let shape = RoundedRect::new(rect, 6.0);
            scene.fill(shape, vertical(rect, &wood));
            scene.fill(Rect::new(rect.x, rect.y, rect.width, 1.0), look.sheen);
            let r = 13.0;
            let step = 37.0;
            let seq_w = 150.0;
            let total = 16.0 * step + 26.0 + 2.0 * step + seq_w + 26.0 + 2.0 * step;
            let mut x = rect.center().x - total / 2.0 + step / 2.0;
            let y = rect.y + 21.0;
            let current = snapshot.general;
            for n in 0..GENERALS {
                let c = Point::new(x, y);
                let kind = if n < 8 {
                    PistonKind::Ivory
                } else {
                    PistonKind::Ebony
                };
                let lit = current == Some(n);
                piston(scene, &look, c, r, &format!("{}", n + 1), kind, lit);
                let tag = if n < 8 {
                    FACTORY_GENERALS.get(n).map_or("", |g| g.tag)
                } else if snapshot.generals_set[n] {
                    "set"
                } else {
                    ""
                };
                let color = if lit { look.accent } else { look.text2 };
                text(
                    scene,
                    tag,
                    style(9.5, 650.0, color),
                    Point::new(c.x, y + 17.0),
                    0.5,
                );
                x += step;
                if n == 7 {
                    x += 12.0;
                }
            }
            x += 14.0;
            // The sequencer: previous, where it is, next.
            piston(
                scene,
                &look,
                Point::new(x, y),
                r,
                "",
                PistonKind::Ebony,
                false,
            );
            arrow(scene, Point::new(x, y), false, look.ivory);
            x += step / 2.0 + 6.0;
            let display = Rect::new(x, y - 15.0, seq_w - 12.0, 30.0);
            let shape = RoundedRect::new(display, 6.0);
            let lcd = Color::hex(0xF2B95A);
            scene.fill(shape, Color::hex(0x0B0806));
            scene.shadow(shape, Shadow::glow(8.0, lcd.with_alpha(0x20)));
            let tag = |n: usize| FACTORY_GENERALS.get(n).map(|g| g.tag);
            let (number, line) = match current {
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
            x = display.right() + 6.0 + step / 2.0;
            piston(
                scene,
                &look,
                Point::new(x, y),
                r,
                "",
                PistonKind::Ebony,
                false,
            );
            arrow(scene, Point::new(x, y), true, look.ivory);
            x += step + 14.0;
            piston(
                scene,
                &look,
                Point::new(x, y),
                r,
                "0",
                PistonKind::Ivory,
                false,
            );
            text(
                scene,
                "cancel",
                style(9.5, 600.0, look.text2),
                Point::new(x, y + 17.0),
                0.5,
            );
            x += step;
            piston(
                scene,
                &look,
                Point::new(x, y),
                r,
                "S",
                PistonKind::Set,
                false,
            );
            text(
                scene,
                "set",
                style(9.5, 600.0, look.text2),
                Point::new(x, y + 17.0),
                0.5,
            );
        },
    )
    .describe(|s| match s.general {
        Some(n) => format!("General {} recalled", n + 1),
        None => "no general recalled".into(),
    })
    .height(58.0)
    .into_view()
}

/// Left of the pedalboard: the swell shoe and the crescendo.
fn shoes(source: &Rc<Source>) -> View {
    live(
        "Shoes",
        Size::new(PLATE, PEDALS),
        source,
        Snapshot::shoes_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let w = 46.0;
            let h = size.height - 42.0;
            let slot = size.width / 2.0;
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
            let swell_title = snapshot.layout.swell.clone();
            let shoes = [
                (swell, swell_title, format!("{:.0} %", swell * 100.0)),
                (step as f32 / 8.0, "Crescendo".to_string(), crescendo),
            ];
            for (i, (position, title, value)) in shoes.iter().enumerate() {
                let x = slot * i as f32 + slot / 2.0 - w / 2.0 - 4.0;
                shoe(
                    scene,
                    &look,
                    Rect::new(x, 4.0, w, h),
                    *position,
                    title,
                    value,
                );
            }
        },
    )
    .width(PLATE)
    .height(PEDALS)
    .into_view()
}

fn is_sharp(note: u8) -> bool {
    matches!(note % 12, 1 | 3 | 6 | 8 | 10)
}

/// The pedalboard as the organist sees it from the bench: long oak naturals
/// running away under the console, narrowing with the distance, their front
/// edges toward the feet, the sharps raised at the far end in the gaps, the
/// notes held lit; its plate on one side, the toe studs on the other.
fn pedalboard(source: &Rc<Source>) -> View {
    live(
        "P\u{e9}dalier",
        Size::new(1200.0, PEDALS),
        source,
        Snapshot::pedal_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let rect = Rect::new(0.0, 0.0, size.width, size.height);
            let Some(pedal) = &snapshot.layout.pedal else {
                return;
            };
            let held = pedal.channel.map_or(0, |c| snapshot.held[c]);
            let lit = |note: u8| held & (1u128 << (note & 0x7F)) != 0;
            // Where a point of the board is: `u` across, 0 to 1; `v` away
            // from the player, 0 at the front edge, 1 under the console.
            let near_y = rect.bottom() - 12.0;
            let far_y = rect.y + 8.0;
            let near_half = rect.width * 0.37;
            let far_half = near_half * 0.92;
            let cx0 = rect.center().x;
            let at = |u: f32, v: f32| {
                let half = near_half + (far_half - near_half) * v;
                Point::new(cx0 + (u - 0.5) * 2.0 * half, near_y + (far_y - near_y) * v)
            };
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
            let (low, high) = (pedal.low, pedal.high.max(pedal.low));
            let naturals: Vec<u8> = (low..=high).filter(|n| !is_sharp(*n)).collect();
            let slot = 1.0 / naturals.len().max(1) as f32;
            let oak = [
                Stop::new(0.0, Color::hex(0x8A6438)),
                Stop::new(0.5, Color::hex(0xB98F5C)),
                Stop::new(1.0, Color::hex(0xD8B47E)),
            ];
            let board = Rect::new(rect.x, far_y, rect.width, near_y - far_y);
            for (i, &note) in naturals.iter().enumerate() {
                let (u0, u1) = (slot * (i as f32 + 0.2), slot * (i as f32 + 0.8));
                let top = quad(u0, u1, 0.0, 1.0);
                scene.shadow(
                    &top,
                    Shadow::new(Color::hex_rgba(0x0000_00A0))
                        .with_offset(0.0, 3.0)
                        .with_blur(5.0),
                );
                scene.fill(&top, vertical(board, &oak));
                scene.stroke(&top, Stroke::new(0.8), Color::hex_rgba(0x3A24_1080));
                let (a, b) = (at(u0, 0.0), at(u1, 0.0));
                let front =
                    polygon(&[a, b, Point::new(b.x, b.y + 6.0), Point::new(a.x, a.y + 6.0)]);
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
                        &format!("C{}", i32::from(note / 12) - 1),
                        style(9.5, 700.0, Color::hex(0x4A3018)),
                        Point::new(p.x, p.y - 12.0),
                        0.5,
                    );
                }
            }
            // A sharp stands in the gap after the natural below it, raised.
            let mut index = 0;
            for note in low..=high {
                if !is_sharp(note) {
                    index += 1;
                    continue;
                }
                let centre = slot * index as f32;
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
            // Toe studs: generals 1 to 8, for the feet.
            let right = at(1.0, 0.0).x + 40.0;
            for n in 0..8 {
                let (row, col) = (n / 4, n % 4);
                let c = Point::new(
                    right + col as f32 * 30.0,
                    rect.center().y - 16.0 + row as f32 * 34.0,
                );
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
    .height(PEDALS)
    .into_view()
}

/// The organ's mark: five pipes of a flat, in brass.
fn logo(look: Look) -> View {
    canvas("Caecilia", Size::new(34.0, 34.0), move |_cx, scene| {
        let bezel = [
            Stop::new(0.0, look.brass_light),
            Stop::new(1.0, look.brass_dark),
        ];
        let heights = [18.0, 25.0, 32.0, 25.0, 18.0];
        for (i, h) in heights.iter().enumerate() {
            let x = 2.0 + i as f32 * 6.4;
            let body = Rect::new(x, 33.0 - h, 5.0, *h);
            scene.fill(
                RoundedRect::new(body, 2.5),
                vertical(Rect::new(0.0, 0.0, 34.0, 34.0), &bezel),
            );
            scene.fill(
                Rect::new(x + 1.2, 33.0 - h * 0.28, 2.6, 1.6),
                look.ground.with_alpha(0xC0),
            );
        }
    })
    .width(34.0)
    .height(34.0)
}

/// A key cheek: the carved block of the case at each end of a manual.
fn cheek(look: Look, right: bool, height: f32) -> View {
    canvas("", Size::new(18.0, height), move |cx, scene| {
        let size = cx.size();
        let rect = Rect::new(0.0, 0.0, size.width, size.height);
        let (outer, inner) = if right {
            (rect.right(), rect.x)
        } else {
            (rect.x, rect.right())
        };
        let mut shape = manguebyte_gfx::Path::new();
        shape.move_to(Point::new(outer, rect.bottom()));
        shape.line_to(Point::new(outer, rect.y + 10.0));
        shape.quad_to(
            Point::new(outer, rect.y),
            Point::new((outer + inner) / 2.0, rect.y),
        );
        shape.quad_to(Point::new(inner, rect.y), Point::new(inner, rect.y + 18.0));
        shape.line_to(Point::new(inner, rect.bottom()));
        shape.close();
        scene.shadow(
            &shape,
            Shadow::new(Color::hex_rgba(0x0000_0090))
                .with_offset(0.0, 3.0)
                .with_blur(5.0),
        );
        let wood = [
            Stop::new(0.0, look.wood_top),
            Stop::new(1.0, look.wood_bottom),
        ];
        scene.fill(&shape, vertical(rect, &wood));
        scene.stroke(&shape, Stroke::new(1.0), look.sheen);
        let c = Point::new(rect.center().x, rect.y + 16.0);
        scene.fill(Circle::new(c, 4.0), look.brass);
        scene.fill(
            Circle::new(Point::new(c.x - 1.0, c.y - 1.0), 1.6),
            look.brass_light,
        );
        let mut groove = manguebyte_gfx::Path::new();
        groove.move_to(Point::new(rect.center().x, rect.y + 28.0));
        groove.line_to(Point::new(rect.center().x, rect.bottom() - 8.0));
        scene.stroke(&groove, Stroke::round(2.0), Color::hex_rgba(0x0000_0055));
        scene.stroke(&groove, Stroke::round(0.8), look.brass.with_alpha(0x60));
    })
    .width(18.0)
    .height(height)
}

/// Behind it all: the room, and a warm light over the music desk.
fn backdrop(look: Look) -> View {
    canvas("", Size::new(1.0, 1.0), move |cx, scene| {
        let size = cx.size();
        scene.fill(Rect::new(0.0, 0.0, size.width, size.height), look.ground);
        let lamp = [
            Stop::new(
                0.0,
                look.accent.with_alpha(if look.night { 0x22 } else { 0x18 }),
            ),
            Stop::new(1.0, look.accent.with_alpha(0)),
        ];
        scene.fill(
            Rect::new(0.0, 0.0, size.width, size.height),
            RadialGradient {
                center: Point::new(size.width / 2.0, size.height * 0.1),
                radius: size.width * 0.5,
                stops: &lamp,
                extend: Extend::Pad,
            },
        );
    })
    .into_view()
}

/// The plate that names a manual, and the divisions coupled to it.
fn keyboard_plate(source: &Rc<Source>, index: usize) -> View {
    live(
        "",
        Size::new(PLATE, 60.0),
        source,
        Snapshot::couplers_key,
        move |size, scene, snapshot| {
            let look = snapshot.look;
            let Some(keyboard) = snapshot.layout.manuals.get(index) else {
                return;
            };
            plate(
                scene,
                &look,
                Rect::new(0.0, size.height / 2.0 - 15.0, size.width, 30.0),
                &keyboard.title,
                "",
            );
            let coupled: Vec<&str> = keyboard
                .coupled_by
                .iter()
                .filter(|&&n| snapshot.couplers.get(n).copied().unwrap_or(false))
                .filter_map(|&n| snapshot.organ.couplers().get(n))
                .filter_map(|c| snapshot.organ.division(c.from))
                .map(|d| d.name.as_str())
                .collect();
            let note = if coupled.is_empty() {
                keyboard.note.clone()
            } else {
                format!("+ {} coupled", coupled.join(", "))
            };
            text(
                scene,
                &note,
                style(10.0, 600.0, look.accent),
                Point::new(size.width / 2.0, size.height / 2.0 + 20.0),
                0.5,
            );
        },
    )
    .width(PLATE)
    .into_view()
}

/// A manual: its plate, its cheeks and its keys.
fn manual(source: &Rc<Source>, index: usize, keys: &KeyboardState, height: f32) -> View {
    let look = source.look();
    let layout = source.layout();
    let keyboard = &layout.manuals[index];
    row((
        keyboard_plate(source, index),
        row((
            cheek(look, false, height),
            piano_keyboard_named(
                keyboard.name.clone(),
                KeySpan::new(keyboard.low, keyboard.high),
                keys.clone(),
            )
            .height(height)
            .flex(1.0),
            cheek(look, true, height),
        ))
        .flex(1.0),
    ))
    .spacing(14.0)
    .align(Align::Center)
    .into_view()
}

/// The status chips: the tuning, the wind, the load.
fn status(source: &Rc<Source>) -> View {
    live(
        "Status",
        Size::new(520.0, 34.0),
        source,
        Snapshot::status_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let wind = if snapshot.wind_steady {
                ("Wind steady", look.good)
            } else {
                ("Wind under load", look.accent)
            };
            let chips = [
                (snapshot.tuning.as_str(), Some(look.accent)),
                (wind.0, Some(wind.1)),
                (snapshot.cpu.as_str(), None),
            ];
            let s = style(12.5, 500.0, look.text);
            let mut x = size.width;
            for (label, dot) in chips.iter().rev() {
                let w = width_of(label, s) + 32.0 + if dot.is_some() { 14.0 } else { 0.0 };
                x -= w;
                let r = Rect::new(x, (size.height - 30.0) / 2.0, w, 30.0);
                let shape = RoundedRect::new(r, 15.0);
                scene.fill(
                    shape,
                    if look.night {
                        Color::hex(0x2A2019)
                    } else {
                        Color::hex(0xFBF6EC)
                    },
                );
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
                x -= 12.0;
            }
        },
    )
    .width(520.0)
    .into_view()
}

/// The console.
pub(crate) fn screen(source: &Rc<Source>) -> View {
    let look = source.look();
    let organ = source.organ();
    let layout = source.layout();
    let manual_height = if layout.manuals.len() > 2 {
        72.0
    } else {
        MANUAL
    };
    let keys: Vec<KeyboardState> = layout
        .manuals
        .iter()
        .map(|_| KeyboardState::new())
        .collect();
    let poller = KeysPoller::new(
        source,
        keys.iter()
            .cloned()
            .zip(layout.manuals.iter().map(|m| m.channel))
            .collect(),
    );
    let top = row((
        logo(look),
        column((
            label("Caecilia").title(),
            label(format!(
                "{} \u{b7} {} divisions \u{b7} {} stops",
                organ.name,
                organ.divisions().len(),
                organ.stops().len()
            ))
            .caption(),
        )),
        poller.flex(1.0),
        status(source),
        pill("Undo"),
        pill("Redo"),
        pill("Organ\u{2026}").primary(),
    ))
    .spacing(12.0)
    .align(Align::Center);
    let middle = column((registration(source), wind(source)))
        .spacing(12.0)
        .flex(1.0);
    let upper = row((jamb_view(source, false), middle, jamb_view(source, true)))
        .spacing(12.0)
        .flex(1.0);
    // The manuals, top first, the key slip above the lowest.
    let mut rows: Vec<View> = Vec::new();
    let last = layout.manuals.len().saturating_sub(1);
    for (i, keys) in keys.iter().enumerate() {
        if i == last {
            rows.push(key_slip(source));
        }
        rows.push(manual(source, i, keys, manual_height));
    }
    if layout.manuals.is_empty() {
        rows.push(key_slip(source));
    }
    rows.push(
        row((shoes(source), pedalboard(source).flex(1.0)))
            .spacing(14.0)
            .align(Align::Center)
            .into_view(),
    );
    let keyboards = column(rows).spacing(4.0);
    stack((
        backdrop(look),
        column((top, upper, keyboards)).spacing(12.0).padding(18.0),
    ))
    .into_view()
}
