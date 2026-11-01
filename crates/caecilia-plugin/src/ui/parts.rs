//! The console's parts, laid out as the approved mockup has them, each
//! drawn from the frame's reading.

use std::rc::Rc;

use manguebyte_gfx::{
    Circle, Color, Extend, Point, RadialGradient, Rect, RoundedRect, Shadow, Size, Stop, Stroke,
};
use manguebyte_ui::layout::Align;
use manguebyte_ui::{
    IntoView, KeySpan, KeyboardState, View, canvas, column, label, piano_keyboard_named, row, stack,
};

use super::controls::{PLATE, jamb_view, key_slip, pedalboard, shoes, status};
use super::live::{KeysPoller, live};
use super::look::Look;
use super::paint::{gauge, heading, panel, plate, style, text, vertical};
use super::registration::registration;
use super::snapshot::{Snapshot, Source};

/// A manual's height.
const MANUAL: f32 = 96.0;

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
