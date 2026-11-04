// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The console's parts that show rather than play, and the whole laid out
//! as the approved mockup has it.

use std::rc::Rc;

use manguebyte_gfx::{
    Circle, Extend, Image, Point, RadialGradient, Rect, RoundedRect, Sampling, Scene, Shadow, Size,
    Stop, Stroke,
};
use manguebyte_ui::access::{AccessNode, Role};
use manguebyte_ui::layout::Align;
use manguebyte_ui::{
    AccessCx, Event, Handled, IntoView, MeasureCx, PaintCx, UpdateCx, View, Widget, column, row,
    stack,
};

use super::actions;
use super::bench::{BENCH, bench};
use super::controls::{jamb_view, pedalboard, shoes, status};
use super::demos::{self, tour_row};
use super::live::{Item, live};
use super::paint::{arrow, gauge, heading, panel, polygon, style, text, width_of};
use super::registration::registration;
use super::snapshot::{DemosView, ModelsView, Snapshot, Source};
use super::sound;

thread_local! {
    /// The Caecilia's mark, drawn beside its name.
    static MARK: Option<Image> =
        Image::decode_png(include_bytes!("../../../../assets/brand/caecilia-mark-96.png")).ok();
}

/// A row of the organ library.
const LIBRARY_ROW: f32 = 26.0;

/// Where the library's rows go, as many as fit.
fn library_rows(size: Size, count: usize) -> Vec<Rect> {
    let top = 46.0;
    let room = ((size.height - top - 34.0) / LIBRARY_ROW).max(0.0) as usize;
    (0..count.min(room))
        .map(|i| {
            Rect::new(
                20.0,
                top + i as f32 * LIBRARY_ROW,
                size.width - 40.0,
                LIBRARY_ROW - 4.0,
            )
        })
        .collect()
}

/// Where the demonstrations go: two columns of rows, in pages when there
/// are more than the desk holds.
struct DemoPlan {
    /// The rows of the page shown: the piece's number, and where it goes.
    rows: Vec<(usize, Rect)>,
    /// How many pages there are, and the arrows between them.
    pages: usize,
    page: usize,
    previous: Rect,
    next: Rect,
}

fn demo_plan(size: Size, count: usize, page: usize) -> DemoPlan {
    rows_plan(size, count, page, 44.0)
}

/// The rows from `top` down.
fn rows_plan(size: Size, count: usize, page: usize, top: f32) -> DemoPlan {
    const COLUMNS: usize = 2;
    let across = (((size.height - top - 6.0) / LIBRARY_ROW).max(1.0)) as usize;
    let holds = (across * COLUMNS).max(1);
    let pages = count.div_ceil(holds).max(1);
    let page = page.min(pages - 1);
    let w = (size.width - 40.0) / COLUMNS as f32;
    let rows = (page * holds..count.min((page + 1) * holds))
        .enumerate()
        .map(|(i, n)| {
            let (row, col) = (i / COLUMNS, i % COLUMNS);
            let r = Rect::new(
                20.0 + col as f32 * w,
                top + row as f32 * LIBRARY_ROW,
                w - 5.0,
                LIBRARY_ROW - 4.0,
            );
            (n, r)
        })
        .collect();
    DemoPlan {
        rows,
        pages,
        page,
        previous: Rect::new(150.0, 12.0, 26.0, 22.0),
        next: Rect::new(232.0, 12.0, 26.0, 22.0),
    }
}

/// The button that has models wait for keys.
fn assign_rect(size: Size) -> Rect {
    Rect::new(size.width - 20.0 - 90.0, 10.0, 90.0, 22.0)
}

/// The button that chooses the models the sustain pedal alternates.
fn pedal_rect(size: Size) -> Rect {
    Rect::new(size.width - 20.0 - 90.0 - 6.0 - 106.0, 10.0, 106.0, 22.0)
}

/// What the Models panel asks of the player just now, on a line of its own.
fn models_hint(models: &ModelsView) -> Option<String> {
    if models.choosing_pedal {
        return Some(match models.pedal.0 {
            None => "Choose the first model for the sustain pedal".into(),
            Some(_) => "Now the second: each press alternates between them".into(),
        });
    }
    if !models.assigning {
        return None;
    }
    Some(match &models.waiting {
        Some(what) => format!("Press the key or pedal for {what}"),
        None => "Choose a model, then press the key that will choose it".into(),
    })
}

fn models_plan(size: Size, models: &ModelsView) -> DemoPlan {
    let top = if models_hint(models).is_some() {
        58.0
    } else {
        44.0
    };
    rows_plan(size, models.rows.len(), models.page, top)
}

fn draw_models(size: Size, scene: &mut Scene, snapshot: &Snapshot, models: &ModelsView) {
    let look = snapshot.look;
    text(
        scene,
        "Models",
        style(13.0, 650.0, look.text),
        Point::new(20.0, 13.0),
        0.0,
    );
    let plan = models_plan(size, models);
    if plan.pages > 1 {
        for (r, left) in [(plan.previous, true), (plan.next, false)] {
            scene.fill(RoundedRect::new(r, 8.0), look.edge.with_alpha(0x60));
            let c = r.center();
            let d = if left { -1.0 } else { 1.0 };
            let mark = polygon(&[
                Point::new(c.x - 2.5 * d, c.y - 5.0),
                Point::new(c.x + 3.5 * d, c.y),
                Point::new(c.x - 2.5 * d, c.y + 5.0),
            ]);
            scene.fill(&mark, look.text);
        }
        text(
            scene,
            &format!("{} / {}", plan.page + 1, plan.pages),
            style(11.5, 500.0, look.text2),
            Point::new((plan.previous.right() + plan.next.x) / 2.0, 16.0),
            0.5,
        );
    }
    sound::chip(
        scene,
        snapshot,
        assign_rect(size),
        "Assign keys",
        models.assigning,
    );
    let pedal_on = models.choosing_pedal || models.pedal != (None, None);
    sound::chip(scene, snapshot, pedal_rect(size), "Sustain pedal", pedal_on);
    if let Some(hint) = models_hint(models) {
        let s = style(11.0, 600.0, look.accent);
        text(
            scene,
            &sound::fit(&hint, s, size.width - 40.0),
            s,
            Point::new(20.0, 38.0),
            0.0,
        );
    }
    for (n, r) in &plan.rows {
        let (name, key) = &models.rows[*n];
        let on = models.current == Some(*n);
        let shape = RoundedRect::new(*r, 8.0);
        if on {
            scene.fill(shape, look.accent.with_alpha(0x2A));
            scene.stroke(shape, Stroke::new(1.0), look.accent);
        } else {
            scene.fill(shape, look.edge.with_alpha(0x60));
        }
        let mut room = r.width - 20.0;
        let mut right = r.right() - 10.0;
        let pedal_mark = if models.pedal.0 == Some(*n) {
            Some("Pedal 1")
        } else if models.pedal.1 == Some(*n) {
            Some("Pedal 2")
        } else {
            None
        };
        for mark in [key.as_deref(), pedal_mark].into_iter().flatten() {
            let s = style(11.0, 600.0, look.accent);
            let w = width_of(mark, s) + 10.0;
            room -= w;
            text(scene, mark, s, Point::new(right, r.y + 5.0), 1.0);
            right -= w;
        }
        let s = style(12.0, if on { 650.0 } else { 500.0 }, look.text);
        text(
            scene,
            &sound::fit(name, s, room),
            s,
            Point::new(r.x + 10.0, r.y + 4.0),
            0.0,
        );
    }
}

fn draw_wind(size: Size, scene: &mut Scene, snapshot: &Snapshot) {
    let look = snapshot.look;
    let rect = Rect::new(0.0, 0.0, size.width, size.height);
    panel(scene, &look, rect);
    if let Some(sound) = &snapshot.sound {
        sound::draw(size, scene, snapshot, sound);
        return;
    }
    if let Some(demos) = &snapshot.demos {
        draw_demos(size, scene, snapshot, demos);
        return;
    }
    if let Some(models) = &snapshot.models {
        draw_models(size, scene, snapshot, models);
        return;
    }
    if let Some(library) = &snapshot.library {
        // The music desk shows the organs while the library is open.
        heading(scene, &look, rect, "Organs", "choose one to play it");
        let rows = library_rows(size, library.entries.len());
        for (r, (name, playing)) in rows.iter().zip(&library.entries) {
            let shape = RoundedRect::new(*r, 8.0);
            if *playing {
                scene.fill(shape, look.accent.with_alpha(0x2A));
                scene.stroke(shape, Stroke::new(1.0), look.accent);
                let c = Point::new(r.x + 14.0, r.center().y);
                scene.shadow(
                    Circle::new(c, 3.5),
                    Shadow::glow(6.0, look.accent.with_alpha(0x90)),
                );
                scene.fill(Circle::new(c, 3.5), look.accent);
            } else {
                scene.fill(shape, look.edge.with_alpha(0x60));
            }
            text(
                scene,
                name,
                style(12.0, if *playing { 650.0 } else { 500.0 }, look.text),
                Point::new(r.x + 28.0, r.y + 4.0),
                0.0,
            );
            if *playing {
                text(
                    scene,
                    "playing",
                    style(10.5, 500.0, look.accent),
                    Point::new(r.right() - 12.0, r.y + 5.0),
                    1.0,
                );
            }
        }
        let more = library.entries.len().saturating_sub(rows.len());
        let foot = match (&library.message, more) {
            (Some(message), _) => (message.clone(), look.set_red),
            (None, 0) => (
                format!("Organ files (.organ.json) are found in {}", library.folder),
                look.muted,
            ),
            (None, n) => (format!("and {n} more in {}", library.folder), look.muted),
        };
        let s = style(10.5, 450.0, foot.1);
        let mut line = foot.0;
        while width_of(&line, s) > size.width - 40.0 && line.chars().count() > 4 {
            line = format!("\u{2026}{}", line.chars().skip(8).collect::<String>());
        }
        text(scene, &line, s, Point::new(20.0, size.height - 26.0), 0.0);
        return;
    }
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
}

/// The demonstrations: a row for each piece and one for the tour, the one
/// playing lit.
fn draw_demos(size: Size, scene: &mut Scene, snapshot: &Snapshot, demos: &DemosView) {
    let look = snapshot.look;
    let rect = Rect::new(0.0, 0.0, size.width, size.height);
    let names = demos::names();
    let labels = demos::labels();
    // What plays says so at the top, where nothing is in its way.
    let saying = match (demos.playing, &demos.tour) {
        (Some(n), _) if n == tour_row() => demos.tour.clone().unwrap_or_default(),
        (Some(n), _) => format!("Playing: {}", labels[n]),
        (None, _) => "Choose a piece; the organ plays it".to_string(),
    };
    heading(scene, &look, rect, "Demonstrations", &saying);
    let plan = demo_plan(size, names.len(), demos.page);
    if plan.pages > 1 {
        let ink = look.text;
        for (r, left) in [(plan.previous, true), (plan.next, false)] {
            let shape = RoundedRect::new(r, 8.0);
            scene.fill(shape, look.edge.with_alpha(0x60));
            let c = r.center();
            let d = if left { -1.0 } else { 1.0 };
            let mark = polygon(&[
                Point::new(c.x - 2.5 * d, c.y - 5.0),
                Point::new(c.x + 3.5 * d, c.y),
                Point::new(c.x - 2.5 * d, c.y + 5.0),
            ]);
            scene.fill(&mark, ink);
        }
        text(
            scene,
            &format!("{} / {}", plan.page + 1, plan.pages),
            style(11.5, 500.0, look.text2),
            Point::new((plan.previous.right() + plan.next.x) / 2.0, 16.0),
            0.5,
        );
    }
    for (n, r) in &plan.rows {
        let n = *n;
        let name = labels[n];
        let playing = demos.playing == Some(n);
        let shape = RoundedRect::new(*r, 8.0);
        if playing {
            scene.fill(shape, look.accent.with_alpha(0x2A));
            scene.stroke(shape, Stroke::new(1.0), look.accent);
        } else {
            scene.fill(shape, look.edge.with_alpha(0x60));
        }
        // Play, or stop while it plays.
        let c = Point::new(r.x + 14.0, r.center().y);
        if playing {
            scene.fill(Rect::new(c.x - 4.0, c.y - 4.0, 8.0, 8.0), look.accent);
        } else {
            arrow(scene, c, true, look.text2);
        }
        text(
            scene,
            name,
            style(12.0, if playing { 650.0 } else { 500.0 }, look.text),
            Point::new(r.x + 28.0, r.y + 4.0),
            0.0,
        );
    }
}

/// The wind: a gauge for each chest; or, while the library is open, the
/// organs to choose from.
pub(crate) fn wind(source: &Rc<Source>) -> View {
    let here = Rc::clone(source);
    let mut drag: Option<sound::Drag> = None;
    live(
        "Wind",
        Size::new(540.0, 220.0),
        source,
        Snapshot::wind_key,
        draw_wind,
    )
    .describe(|s| {
        if let Some(sound) = &s.sound {
            return sound::describe(sound);
        }
        if let Some(models) = &s.models {
            let rows: Vec<String> = models
                .rows
                .iter()
                .enumerate()
                .map(|(n, (name, key))| {
                    let mut said = name.clone();
                    if let Some(key) = key {
                        said.push_str(&format!(" on {key}"));
                    }
                    if models.pedal.0 == Some(n) {
                        said.push_str(" (pedal 1)");
                    }
                    if models.pedal.1 == Some(n) {
                        said.push_str(" (pedal 2)");
                    }
                    if models.current == Some(n) {
                        said.push_str(" (drawn)");
                    }
                    said
                })
                .collect();
            let mut said = format!("Models: {}", rows.join(", "));
            if let Some(what) = &models.waiting {
                said.push_str(&format!("; press the key for {what}"));
            }
            return said;
        }
        if let Some(demos) = &s.demos {
            let rows: Vec<String> = demos::names()
                .iter()
                .enumerate()
                .map(|(n, name)| match (demos.playing == Some(n), &demos.tour) {
                    (true, Some(stop)) if n == tour_row() => format!("{name} (playing {stop})"),
                    (true, _) => format!("{name} (playing)"),
                    _ => name.to_string(),
                })
                .collect();
            return format!("Demonstrations: {}", rows.join(", "));
        }
        if let Some(library) = &s.library {
            let names: Vec<String> = library
                .entries
                .iter()
                .map(|(name, playing)| {
                    if *playing {
                        format!("{name} (playing)")
                    } else {
                        name.clone()
                    }
                })
                .collect();
            let mut said = format!("Organs: {}", names.join(", "));
            if let Some(message) = &library.message {
                said.push_str(&format!("; {message}"));
            }
            return said;
        }
        let chests: Vec<String> = s
            .chests
            .iter()
            .map(|c| format!("{} {:.1} mm, {}", c.name, c.now_mm, c.note))
            .collect();
        chests.join("; ")
    })
    .items(|s, size| {
        if let Some(sound) = &s.sound {
            return sound::items(sound, size);
        }
        if let Some(models) = &s.models {
            let plan = models_plan(size, models);
            let mut items: Vec<Item> = plan
                .rows
                .iter()
                .map(|(n, rect)| Item {
                    role: Role::Button,
                    name: format!("Model {}", models.rows[*n].0),
                    checked: Some(models.current == Some(*n)),
                    rect: *rect,
                })
                .collect();
            items.push(Item {
                role: Role::Button,
                name: "Assign keys".into(),
                checked: Some(models.assigning),
                rect: assign_rect(size),
            });
            items.push(Item {
                role: Role::Button,
                name: "Sustain pedal".into(),
                checked: Some(models.choosing_pedal || models.pedal != (None, None)),
                rect: pedal_rect(size),
            });
            if plan.pages > 1 {
                for (name, rect) in [
                    ("Previous models", plan.previous),
                    ("Next models", plan.next),
                ] {
                    items.push(Item {
                        role: Role::Button,
                        name: name.into(),
                        checked: None,
                        rect,
                    });
                }
            }
            return items;
        }
        if let Some(demos) = &s.demos {
            let names = demos::names();
            let plan = demo_plan(size, names.len(), demos.page);
            let mut items: Vec<Item> = plan
                .rows
                .iter()
                .map(|(n, rect)| Item {
                    role: Role::Button,
                    name: format!("Play {}", names[*n]),
                    checked: Some(demos.playing == Some(*n)),
                    rect: *rect,
                })
                .collect();
            if plan.pages > 1 {
                for (name, rect) in [
                    ("Previous pieces", plan.previous),
                    ("Next pieces", plan.next),
                ] {
                    items.push(Item {
                        role: Role::Button,
                        name: name.into(),
                        checked: None,
                        rect,
                    });
                }
            }
            return items;
        }
        let Some(library) = &s.library else {
            return Vec::new();
        };
        library_rows(size, library.entries.len())
            .into_iter()
            .zip(&library.entries)
            .map(|(rect, (name, playing))| Item {
                role: Role::Button,
                name: format!("Organ {name}"),
                checked: Some(*playing),
                rect,
            })
            .collect()
    })
    .input(move |cx, event, at, size, snapshot| {
        if snapshot.sound.is_some() || drag.is_some() {
            return sound::input(
                cx,
                event,
                at,
                size,
                &here,
                snapshot.sound.as_ref(),
                &mut drag,
            );
        }
        if !matches!(event, Event::PointerDown { .. }) {
            return Handled::No;
        }
        if let Some(view) = &snapshot.models {
            let plan = models_plan(size, view);
            if assign_rect(size).contains(at) {
                here.assign_model_keys(!view.assigning);
                return Handled::Yes;
            }
            if pedal_rect(size).contains(at) {
                here.press_pedal_models();
                return Handled::Yes;
            }
            if plan.pages > 1 && plan.previous.contains(at) {
                here.model_page(plan.page.saturating_sub(1));
                return Handled::Yes;
            }
            if plan.pages > 1 && plan.next.contains(at) {
                here.model_page(plan.page + 1);
                return Handled::Yes;
            }
            let Some((n, _)) = plan.rows.iter().find(|(_, r)| r.contains(at)) else {
                return Handled::No;
            };
            here.press_model(view.numbers[*n]);
            return Handled::Yes;
        }
        if let Some(view) = &snapshot.demos {
            let plan = demo_plan(size, demos::names().len(), view.page);
            if plan.pages > 1 && plan.previous.contains(at) {
                here.demo_page(plan.page.saturating_sub(1));
                return Handled::Yes;
            }
            if plan.pages > 1 && plan.next.contains(at) {
                here.demo_page(plan.page + 1);
                return Handled::Yes;
            }
            let Some((n, _)) = plan.rows.iter().find(|(_, r)| r.contains(at)) else {
                return Handled::No;
            };
            here.press_demo(cx.context(), *n);
            return Handled::Yes;
        }
        let Some(library) = &snapshot.library else {
            return Handled::No;
        };
        let rows = library_rows(size, library.entries.len());
        let Some(n) = rows.iter().position(|r| r.contains(at)) else {
            return Handled::No;
        };
        let Some(entry) = here.library_entry(n) else {
            return Handled::No;
        };
        match actions::load_organ(cx.context(), here.shared(), &entry) {
            Ok(()) => here.toggle_library(),
            Err(e) => here.library_message(Some(format!("{}: {e}", entry.name))),
        }
        Handled::Yes
    })
    .flex(1.0)
    .into_view()
}

/// The organ's mark, its name, and the organ playing.
fn title(source: &Rc<Source>) -> View {
    live(
        "Title",
        Size::new(340.0, 40.0),
        source,
        |_| 0,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let top = (size.height - 34.0) / 2.0;
            // The mark: the gold C and its pipes.
            MARK.with(|mark| {
                if let Some(mark) = mark {
                    scene.image(
                        mark,
                        Rect::new(0.0, top - 4.0, 40.0, 40.0),
                        Sampling::Bilinear,
                    );
                }
            });
            text(
                scene,
                "Caecilia",
                style(17.0, 650.0, look.text),
                Point::new(50.0, top - 1.0),
                0.0,
            );
            let organ = &snapshot.organ;
            text(
                scene,
                &format!(
                    "{} \u{b7} {} divisions \u{b7} {} stops",
                    organ.name,
                    organ.divisions().len(),
                    organ.stops().len()
                ),
                style(11.5, 450.0, look.text2),
                Point::new(50.0, top + 21.0),
                0.0,
            );
        },
    )
    .describe(|s| {
        format!(
            "{}, in the {}'s light",
            s.organ.name,
            if s.look.night { "night" } else { "day" }
        )
    })
    .width(420.0)
    .into_view()
}

/// The mark and the organ playing, in the compact console.
pub(crate) fn compact_title(source: &Rc<Source>) -> View {
    live(
        "Title",
        Size::new(190.0, 40.0),
        source,
        |_| 0,
        |size, scene, snapshot| {
            let look = snapshot.look;
            let top = (size.height - 34.0) / 2.0;
            MARK.with(|mark| {
                if let Some(mark) = mark {
                    scene.image(
                        mark,
                        Rect::new(0.0, top - 2.0, 36.0, 36.0),
                        Sampling::Bilinear,
                    );
                }
            });
            text(
                scene,
                "Caecilia",
                style(15.0, 650.0, look.text),
                Point::new(42.0, top - 1.0),
                0.0,
            );
            let s = style(11.0, 450.0, look.text2);
            text(
                scene,
                &sound::fit(&snapshot.organ.name, s, size.width - 42.0),
                s,
                Point::new(42.0, top + 18.0),
                0.0,
            );
        },
    )
    .describe(|s| {
        format!(
            "{}, in the {}'s light",
            s.organ.name,
            if s.look.night { "night" } else { "day" }
        )
    })
    .width(190.0)
    .into_view()
}

/// Behind it all: the room, and a warm light over the music desk.
pub(crate) fn backdrop(source: &Rc<Source>) -> View {
    live(
        "",
        Size::new(1.0, 1.0),
        source,
        |_| 0,
        |size, scene, snapshot| {
            let look = snapshot.look;
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
        },
    )
    .into_view()
}

/// Nothing to see: keeps the frames coming, so the wind moves and the
/// console follows the organ.
pub(crate) struct Ticker;

impl Widget for Ticker {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        AccessNode::new(Role::Group, "")
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        Size::new(0.0, 0.0)
    }

    fn paint(&mut self, _cx: &PaintCx<'_>, _scene: &mut Scene) {}

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        cx.request_frame();
    }
}

/// Something to keep the frames coming, for a screen to hold.
pub(crate) fn ticker() -> Ticker {
    Ticker
}

/// The console.
pub(crate) fn screen(source: &Rc<Source>) -> View {
    let top = row((title(source), Ticker, status(source, true).flex(1.0)))
        .spacing(12.0)
        .align(Align::Center);
    let middle = column((registration(source), wind(source)))
        .spacing(12.0)
        .flex(1.0);
    let upper = row((jamb_view(source, false), middle, jamb_view(source, true)))
        .spacing(12.0)
        .flex(1.0);
    let pedals = row((shoes(source), pedalboard(source).flex(1.0)))
        .spacing(14.0)
        .align(Align::Center);
    let keyboards = column((bench(source).height(BENCH), pedals)).spacing(4.0);
    stack((
        backdrop(source),
        column((top, upper, keyboards)).spacing(12.0).padding(18.0),
    ))
    .into_view()
}
