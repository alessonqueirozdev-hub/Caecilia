// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The manuals and the key slip between them, as the organist sees them
//! from the bench: each manual with its plate and its carved cheeks, the
//! key slip above the lowest. Drawn by the console, not the toolkit's
//! keyboard, so that another organ, with more manuals or another compass,
//! lays itself out here as it plays.

use std::hash::Hash;
use std::rc::Rc;

use manguebyte_gfx::{Circle, Color, Point, Rect, RoundedRect, Scene, Shadow, Size, Stop, Stroke};
use manguebyte_ui::access::Role;
use manguebyte_ui::{Handled, IntoView, View};

use super::actions;
use super::controls::{PLATE, SLIP, describe_slip, draw_slip, slip_items, slip_press};
use super::live::{Item, live};
use super::look::Look;
use super::paint::{plate, style, text, vertical};
use super::snapshot::{Keyboard, Snapshot, Source, hash_of, note_name};

/// The bench's height: two manuals and the key slip.
pub(crate) const BENCH: f32 = 96.0 * 2.0 + SLIP + 8.0;

/// A cheek's width.
const CHEEK: f32 = 18.0;

/// The tallest a manual is drawn.
const MANUAL_MAX: f32 = 110.0;

fn is_sharp(note: u8) -> bool {
    matches!(note % 12, 1 | 3 | 6 | 8 | 10)
}

/// Where each manual's row and the key slip go, top to bottom.
fn plan(size: Size, manuals: usize) -> (Vec<Rect>, Rect) {
    let gap = 4.0;
    if manuals == 0 {
        return (Vec::new(), Rect::new(0.0, 0.0, size.width, SLIP));
    }
    let n = manuals as f32;
    let h = ((size.height - SLIP - gap * n) / n).min(MANUAL_MAX);
    let mut rows = Vec::new();
    let mut slip = Rect::new(0.0, 0.0, size.width, SLIP);
    let mut y = 0.0;
    for i in 0..manuals {
        if i + 1 == manuals {
            slip = Rect::new(0.0, y, size.width, SLIP);
            y += SLIP + gap;
        }
        rows.push(Rect::new(0.0, y, size.width, h));
        y += h + gap;
    }
    (rows, slip)
}

/// A manual's keys in its row.
struct Keys {
    rect: Rect,
    low: u8,
    high: u8,
    whites: Vec<u8>,
    w: f32,
}

impl Keys {
    fn of(row: Rect, keyboard: &Keyboard) -> Keys {
        let x0 = PLATE + 14.0 + CHEEK;
        let rect = Rect::new(x0, row.y, (row.width - CHEEK - x0).max(1.0), row.height);
        let (low, high) = (keyboard.low, keyboard.high.max(keyboard.low));
        let whites: Vec<u8> = (low..=high).filter(|n| !is_sharp(*n)).collect();
        let w = rect.width / whites.len().max(1) as f32;
        Keys {
            rect,
            low,
            high,
            whites,
            w,
        }
    }

    fn white(&self, i: usize) -> Rect {
        Rect::new(
            self.rect.x + self.w * i as f32,
            self.rect.y,
            self.w,
            self.rect.height,
        )
    }

    /// A sharp's key: over the gap after the white below it.
    fn black(&self, note: u8) -> Rect {
        let below = self.whites.iter().filter(|w| **w < note).count();
        let bw = self.w * 0.6;
        Rect::new(
            self.rect.x + self.w * below as f32 - bw / 2.0,
            self.rect.y,
            bw,
            self.rect.height * 0.62,
        )
    }

    fn blacks(&self) -> impl Iterator<Item = u8> + '_ {
        (self.low..=self.high).filter(|n| is_sharp(*n))
    }

    fn key_at(&self, p: Point) -> Option<u8> {
        if !self.rect.contains(p) {
            return None;
        }
        if let Some(note) = self.blacks().find(|n| self.black(*n).contains(p)) {
            return Some(note);
        }
        let i = ((p.x - self.rect.x) / self.w) as usize;
        self.whites.get(i).copied()
    }
}

/// A key cheek: the carved block of the case at each end of a manual.
fn draw_cheek(scene: &mut Scene, look: &Look, rect: Rect, right: bool) {
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
}

/// A manual's plate, its cheeks and its keys, the keys held lit.
fn draw_manual(scene: &mut Scene, snapshot: &Snapshot, row: Rect, keyboard: &Keyboard) {
    let look = snapshot.look;
    // The plate, and what is coupled to it.
    plate(
        scene,
        &look,
        Rect::new(0.0, row.center().y - 15.0, PLATE, 30.0),
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
        Point::new(PLATE / 2.0, row.center().y + 20.0),
        0.5,
    );
    draw_cheek(
        scene,
        &look,
        Rect::new(PLATE + 14.0, row.y, CHEEK, row.height),
        false,
    );
    draw_cheek(
        scene,
        &look,
        Rect::new(row.right() - CHEEK, row.y, CHEEK, row.height),
        true,
    );

    let keys = Keys::of(row, keyboard);
    let held = keyboard.channel.map_or(0, |c| snapshot.held[c]);
    let lit = |note: u8| held & (1u128 << (note & 0x7F)) != 0;
    scene.fill(keys.rect, look.ebony);
    let ivory = [
        Stop::new(0.0, look.ivory_shade),
        Stop::new(0.12, look.ivory_light),
        Stop::new(0.8, look.ivory),
        Stop::new(1.0, look.ivory_shade),
    ];
    for (i, &note) in keys.whites.iter().enumerate() {
        let r = keys.white(i);
        let key = Rect::new(r.x + 0.6, r.y, r.width - 1.2, r.height - 1.0);
        let shape = RoundedRect::new(key, 2.5);
        scene.fill(shape, vertical(key, &ivory));
        if lit(note) {
            scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x90)));
            scene.fill(shape, look.accent.with_alpha(0xB0));
        }
        if note % 12 == 0 {
            text(
                scene,
                &note_name(note),
                style(9.0, 600.0, look.ink.with_alpha(0x90)),
                Point::new(key.x + 5.0, key.bottom() - 15.0),
                0.0,
            );
        }
    }
    let ebony = [
        Stop::new(0.0, Color::hex(0x0E0A08)),
        Stop::new(0.7, Color::hex(0x2A211B)),
        Stop::new(1.0, Color::hex(0x4A3C32)),
    ];
    for note in keys.blacks() {
        let r = keys.black(note);
        let shape = RoundedRect::new(r, 2.5);
        scene.shadow(
            shape,
            Shadow::new(Color::hex_rgba(0x0000_00A0))
                .with_offset(1.0, 3.0)
                .with_blur(4.0),
        );
        scene.fill(shape, vertical(r, &ebony));
        // The front of the key, catching the light.
        scene.fill(
            Rect::new(r.x + 2.0, r.bottom() - 8.0, r.width - 4.0, 5.0),
            Color::hex_rgba(0xFFF0_D822),
        );
        if lit(note) {
            scene.fill(shape, look.accent.with_alpha(0xC0));
        }
    }
    // The key bed's shadow under the slip or the manual above.
    scene.fill(
        Rect::new(keys.rect.x, keys.rect.y, keys.rect.width, 3.0),
        Color::hex_rgba(0x0000_0060),
    );
}

fn bench_key(s: &Snapshot) -> u64 {
    let held: Vec<u128> = s
        .layout
        .manuals
        .iter()
        .map(|m| m.channel.map_or(0, |c| s.held[c]))
        .collect();
    hash_of(|h| {
        (held, &s.couplers, s.general, s.generals_set, s.set_armed).hash(h);
    })
}

fn describe_bench(s: &Snapshot) -> String {
    let manuals: Vec<String> = s
        .layout
        .manuals
        .iter()
        .map(|m| {
            let held = m.channel.map_or(0, |c| s.held[c]);
            let notes: Vec<String> = (m.low..=m.high)
                .filter(|n| held & (1u128 << (n & 0x7F)) != 0)
                .map(note_name)
                .collect();
            if notes.is_empty() {
                format!("{}: nothing held", m.name)
            } else {
                format!("{}: {} held", m.name, notes.join(", "))
            }
        })
        .collect();
    format!("{}; {}", describe_slip(s), manuals.join("; "))
}

fn bench_items(s: &Snapshot, size: Size) -> Vec<Item> {
    let manuals = &s.layout.manuals;
    let (rows, slip) = plan(size, manuals.len());
    let mut items: Vec<Item> = slip_items(Size::new(slip.width, slip.height))
        .into_iter()
        .map(|mut item| {
            item.rect = item.rect.translate(Point::new(slip.x, slip.y));
            item
        })
        .collect();
    for (row, keyboard) in rows.iter().zip(manuals) {
        let keys = Keys::of(*row, keyboard);
        for (i, &note) in keys.whites.iter().enumerate() {
            let r = keys.white(i);
            items.push(Item {
                role: Role::Button,
                name: format!("{} {}", keyboard.name, note_name(note)),
                checked: None,
                rect: Rect::new(r.x, r.y + r.height * 0.66, r.width, r.height * 0.3),
            });
        }
        for note in keys.blacks() {
            items.push(Item {
                role: Role::Button,
                name: format!("{} {}", keyboard.name, note_name(note)),
                checked: None,
                rect: keys.black(note),
            });
        }
    }
    items
}

/// The manuals and the key slip. A key pressed sounds, a finger slides
/// along a manual, and the key slip's pistons do what they do.
pub(crate) fn bench(source: &Rc<Source>) -> View {
    let here = Rc::clone(source);
    let mut sounding: Option<(usize, usize, u8)> = None;
    live(
        "Manuals",
        Size::new(1200.0, BENCH),
        source,
        bench_key,
        |size, scene, snapshot| {
            let manuals = &snapshot.layout.manuals;
            let (rows, slip) = plan(size, manuals.len());
            for (row, keyboard) in rows.iter().zip(manuals) {
                draw_manual(scene, snapshot, *row, keyboard);
            }
            scene.transformed(
                manguebyte_gfx::Transform::translate(slip.x, slip.y),
                |scene| draw_slip(Size::new(slip.width, slip.height), scene, snapshot),
            );
        },
    )
    .describe(describe_bench)
    .items(bench_items)
    .input(move |cx, event, at, size, snapshot| {
        let manuals = &snapshot.layout.manuals;
        let (rows, slip) = plan(size, manuals.len());
        let shared = here.shared();
        let key_at = |at: Point| {
            rows.iter()
                .zip(manuals)
                .enumerate()
                .find_map(|(i, (row, k))| {
                    let channel = k.channel?;
                    Keys::of(*row, k).key_at(at).map(|note| (i, channel, note))
                })
        };
        match event {
            manguebyte_ui::Event::PointerDown { .. } => {
                if slip.contains(at) {
                    let local = Point::new(at.x - slip.x, at.y - slip.y);
                    return slip_press(cx, &here, local, Size::new(slip.width, slip.height));
                }
                let Some((row, channel, note)) = key_at(at) else {
                    return Handled::No;
                };
                actions::play(shared, channel, note, true);
                sounding = Some((row, channel, note));
                cx.capture_pointer();
                Handled::Yes
            }
            manguebyte_ui::Event::PointerMove { .. } => {
                let Some((row, channel, note)) = sounding else {
                    return Handled::No;
                };
                // A finger sliding along the same manual.
                if let Some((r, _, next)) = key_at(at) {
                    if r == row && next != note {
                        actions::play(shared, channel, note, false);
                        actions::play(shared, channel, next, true);
                        sounding = Some((row, channel, next));
                    }
                }
                Handled::Yes
            }
            manguebyte_ui::Event::PointerUp { .. } | manguebyte_ui::Event::PointerCancel => {
                let Some((_, channel, note)) = sounding.take() else {
                    return Handled::No;
                };
                actions::play(shared, channel, note, false);
                cx.release_pointer();
                Handled::Yes
            }
            _ => Handled::No,
        }
    })
    .height(BENCH)
    .into_view()
}
