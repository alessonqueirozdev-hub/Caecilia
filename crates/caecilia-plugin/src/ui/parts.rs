//! The console's parts that show rather than play, and the whole laid out
//! as the approved mockup has it.

use std::rc::Rc;

use manguebyte_gfx::{
    Circle, Extend, Point, RadialGradient, Rect, RoundedRect, Scene, Shadow, Size, Stop, Stroke,
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
use super::demos::{self, TOUR_ROW};
use super::live::{Item, live};
use super::paint::{arrow, gauge, heading, panel, style, text, vertical, width_of};
use super::registration::registration;
use super::snapshot::{DemosView, Snapshot, Source};
use super::sound;

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
    heading(
        scene,
        &look,
        rect,
        "Demonstrations",
        "the organ plays itself",
    );
    let names = demos::names();
    let rows = library_rows(size, names.len());
    for (n, (r, name)) in rows.iter().zip(&names).enumerate() {
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
        if playing {
            let what = match (&demos.tour, n == TOUR_ROW) {
                (Some(stop), true) => stop.clone(),
                _ => "playing".to_string(),
            };
            text(
                scene,
                &what,
                style(10.5, 500.0, look.accent),
                Point::new(r.right() - 12.0, r.y + 5.0),
                1.0,
            );
        }
    }
    text(
        scene,
        "Each piece draws its own stops; the tour plays every stop alone.",
        style(10.5, 450.0, look.muted),
        Point::new(20.0, size.height - 26.0),
        0.0,
    );
}

/// The wind: a gauge for each chest; or, while the library is open, the
/// organs to choose from.
fn wind(source: &Rc<Source>) -> View {
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
        if let Some(demos) = &s.demos {
            let rows: Vec<String> = demos::names()
                .iter()
                .enumerate()
                .map(|(n, name)| match (demos.playing == Some(n), &demos.tour) {
                    (true, Some(stop)) if n == TOUR_ROW => format!("{name} (playing {stop})"),
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
        if let Some(demos) = &s.demos {
            let names = demos::names();
            return library_rows(size, names.len())
                .into_iter()
                .zip(names)
                .enumerate()
                .map(|(n, (rect, name))| Item {
                    role: Role::Button,
                    name: format!("Play {name}"),
                    checked: Some(demos.playing == Some(n)),
                    rect,
                })
                .collect();
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
            return sound::input(cx, event, at, size, here.shared(), &mut drag);
        }
        if !matches!(event, Event::PointerDown { .. }) {
            return Handled::No;
        }
        if snapshot.demos.is_some() {
            let rows = library_rows(size, demos::names().len());
            let Some(n) = rows.iter().position(|r| r.contains(at)) else {
                return Handled::No;
            };
            here.press_demo(cx.context(), n);
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
        Size::new(420.0, 40.0),
        source,
        |_| 0,
        |size, scene, snapshot| {
            let look = snapshot.look;
            // Five pipes of a flat, in brass.
            let bezel = [
                Stop::new(0.0, look.brass_light),
                Stop::new(1.0, look.brass_dark),
            ];
            let top = (size.height - 34.0) / 2.0;
            let heights = [18.0, 25.0, 32.0, 25.0, 18.0];
            for (i, h) in heights.iter().enumerate() {
                let x = 2.0 + i as f32 * 6.4;
                let body = Rect::new(x, top + 33.0 - h, 5.0, *h);
                scene.fill(
                    RoundedRect::new(body, 2.5),
                    vertical(Rect::new(0.0, top, 34.0, 34.0), &bezel),
                );
                scene.fill(
                    Rect::new(x + 1.2, top + 33.0 - h * 0.28, 2.6, 1.6),
                    look.ground.with_alpha(0xC0),
                );
            }
            text(
                scene,
                "Caecilia",
                style(17.0, 650.0, look.text),
                Point::new(46.0, top - 1.0),
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
                Point::new(46.0, top + 21.0),
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

/// Behind it all: the room, and a warm light over the music desk.
fn backdrop(source: &Rc<Source>) -> View {
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
struct Ticker;

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

/// The console.
pub(crate) fn screen(source: &Rc<Source>) -> View {
    let top = row((title(source), Ticker.flex(1.0), status(source)))
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
