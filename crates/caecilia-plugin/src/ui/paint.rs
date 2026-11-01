// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! How the console's parts are drawn: wood, plates, drawstops, tablets,
//! pistons, gauges, shoes. Plain functions of a scene, so the widgets that
//! use them stay about state and input.

use manguebyte_gfx::{
    Circle, Color, Extend, LinearGradient, Point, RadialGradient, Rect, RoundedRect, Scene, Shadow,
    Size, Stop, Stroke, TextLayout, TextStyle,
};

use super::look::Look;

/// Text, its top at `at.y`, its `anchor` (0 start, 0.5 centre, 1 end) at
/// `at.x`. Returns its size.
pub(crate) fn text(scene: &mut Scene, s: &str, style: TextStyle, at: Point, anchor: f32) -> Size {
    let layout = TextLayout::new(s, &style, None);
    let size = layout.size();
    scene.text(&layout, Point::new(at.x - size.width * anchor, at.y));
    size
}

pub(crate) fn style(size: f32, weight: f32, color: Color) -> TextStyle {
    TextStyle::new(size).with_weight(weight).with_color(color)
}

pub(crate) fn width_of(s: &str, style: TextStyle) -> f32 {
    TextLayout::new(s, &style, None).size().width
}

pub(crate) fn vertical(rect: Rect, stops: &[Stop]) -> LinearGradient<'_> {
    LinearGradient {
        start: Point::new(rect.x, rect.y),
        end: Point::new(rect.x, rect.bottom()),
        stops,
        extend: Extend::Pad,
    }
}

/// A turned wooden jamb: the wood's gradient, its grain, a bevelled edge.
pub(crate) fn jamb(scene: &mut Scene, look: &Look, rect: Rect) {
    let shape = RoundedRect::new(rect, 14.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(0x0000_0088))
            .with_offset(0.0, 10.0)
            .with_blur(30.0)
            .with_spread(-6.0),
    );
    let stops = [
        Stop::new(0.0, look.wood_top),
        Stop::new(1.0, look.wood_bottom),
    ];
    scene.fill(shape, vertical(rect, &stops));
    // The grain: long, slow waves, the same on every run.
    scene.clipped(shape, |scene| {
        let mut seed = 0x2545_F491_u32;
        let mut next = move || {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            (seed % 10_000) as f32 / 10_000.0
        };
        let mut y = rect.y + 3.0;
        while y < rect.bottom() {
            let mut path = manguebyte_gfx::Path::new();
            let amp = 2.0 + 5.0 * next();
            let phase = next() * 400.0;
            path.move_to(Point::new(rect.x - 10.0, y));
            let mut x = rect.x - 10.0;
            while x < rect.right() + 10.0 {
                let x1 = x + 60.0;
                let wave = |x: f32| ((x + phase) / 110.0).sin() * amp;
                path.cubic_to(
                    Point::new(x + 20.0, y + wave(x + 20.0)),
                    Point::new(x + 40.0, y + wave(x + 40.0)),
                    Point::new(x1, y + wave(x1)),
                );
                x = x1;
            }
            scene.stroke(&path, Stroke::new(0.6 + next() * 1.4), look.grain);
            y += 3.0 + next() * 7.0;
        }
    });
    scene.stroke(
        RoundedRect::new(
            Rect::new(
                rect.x + 0.5,
                rect.y + 0.5,
                rect.width - 1.0,
                rect.height - 1.0,
            ),
            13.5,
        ),
        Stroke::new(1.0),
        look.sheen,
    );
    // The gilded beading a case maker runs round a panel, and its shadow.
    let bead = RoundedRect::new(
        Rect::new(
            rect.x + 7.0,
            rect.y + 7.0,
            rect.width - 14.0,
            rect.height - 14.0,
        ),
        9.0,
    );
    scene.stroke(bead, Stroke::new(2.4), Color::hex_rgba(0x0000_0060));
    scene.stroke(bead, Stroke::new(1.2), look.brass.with_alpha(0xB0));
}

/// A panel between the jambs: leather in the night, paper in the day.
pub(crate) fn panel(scene: &mut Scene, look: &Look, rect: Rect) {
    let shape = RoundedRect::new(rect, 12.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(if look.night {
            0x0000_0070
        } else {
            0x5A3A_1A22
        }))
        .with_offset(0.0, 6.0)
        .with_blur(18.0)
        .with_spread(-4.0),
    );
    let stops = [
        Stop::new(0.0, look.panel_top),
        Stop::new(1.0, look.panel_bottom),
    ];
    scene.fill(shape, vertical(rect, &stops));
    scene.stroke(shape, Stroke::new(1.0), look.edge);
    scene.fill(
        Rect::new(rect.x + 12.0, rect.y + 1.0, rect.width - 24.0, 1.0),
        look.sheen,
    );
    scene.stroke(
        RoundedRect::new(
            Rect::new(
                rect.x + 5.0,
                rect.y + 5.0,
                rect.width - 10.0,
                rect.height - 10.0,
            ),
            8.0,
        ),
        Stroke::new(1.0),
        look.brass.with_alpha(0x55),
    );
}

/// A card's heading inside a panel.
pub(crate) fn heading(scene: &mut Scene, look: &Look, rect: Rect, title: &str, meta: &str) {
    text(
        scene,
        title,
        style(13.0, 650.0, look.text),
        Point::new(rect.x + 20.0, rect.y + 16.0),
        0.0,
    );
    text(
        scene,
        meta,
        style(11.5, 450.0, look.muted),
        Point::new(rect.right() - 20.0, rect.y + 18.0),
        1.0,
    );
}

/// An engraved brass name plate.
pub(crate) fn plate(scene: &mut Scene, look: &Look, rect: Rect, title: &str, note: &str) {
    let shape = RoundedRect::new(rect, 4.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(0x0000_0080))
            .with_offset(0.0, 2.0)
            .with_blur(4.0),
    );
    let stops = [
        Stop::new(0.0, look.brass_light),
        Stop::new(0.45, look.brass),
        Stop::new(1.0, look.brass_dark),
    ];
    scene.fill(shape, vertical(rect, &stops));
    scene.stroke(shape, Stroke::new(1.0), look.brass_dark.with_alpha(0xC0));
    for x in [rect.x + 9.0, rect.right() - 9.0] {
        let c = Point::new(x, rect.center().y);
        scene.fill(Circle::new(c, 2.6), look.brass_dark);
        scene.fill(
            Circle::new(Point::new(c.x - 0.6, c.y - 0.6), 1.3),
            look.brass_light,
        );
    }
    let s = style(11.5, 760.0, look.plate_ink);
    let size = TextLayout::new(title, &s, None).size();
    text(
        scene,
        title,
        s,
        Point::new(rect.center().x, rect.center().y - size.height / 2.0),
        0.5,
    );
    if !note.is_empty() {
        let n = style(9.5, 600.0, look.plate_ink.with_alpha(0xB0));
        let size = TextLayout::new(note, &n, None).size();
        text(
            scene,
            note,
            n,
            Point::new(rect.right() - 18.0, rect.center().y - size.height / 2.0),
            1.0,
        );
    }
}

/// What a drawstop's face says: its name in lines, its pitch, its ink.
pub(crate) struct StopFace {
    lines: Vec<String>,
    /// The name's size: smaller for a long word.
    size: f32,
    pitch: String,
    reed: bool,
    /// Whether it is drawn.
    pub(crate) drawn: bool,
}

impl StopFace {
    pub(crate) fn of(name: &str, reed: bool, drawn: bool) -> StopFace {
        // The pitch is the tail of numbers and fractions, or a mixture's
        // ranks in roman numerals.
        let words: Vec<&str> = name.split_whitespace().collect();
        let is_pitch = |w: &str| {
            w.chars().all(|c| c.is_ascii_digit() || c == '/')
                || w.chars().all(|c| matches!(c, 'I' | 'V' | 'X'))
        };
        let split = words
            .iter()
            .rposition(|w| !is_pitch(w))
            .map_or(0, |i| i + 1);
        let tail = words[split..].join(" ");
        let roman = tail.chars().all(|c| matches!(c, 'I' | 'V' | 'X' | ' '));
        let pitch = if tail.is_empty() || roman {
            tail
        } else {
            format!("{tail}'")
        };
        // The name in lines that fit the face, a hyphen breaking too.
        let face = style(9.2, 640.0, Color::BLACK);
        let mut pieces = Vec::new();
        for w in &words[..split] {
            match w.split_once('-') {
                Some((a, b)) if width_of(w, face) > 45.0 => {
                    pieces.push(format!("{a}-"));
                    pieces.push(b.to_string());
                }
                _ => pieces.push((*w).to_string()),
            }
        }
        let mut lines: Vec<String> = Vec::new();
        for p in pieces {
            match lines.last_mut() {
                Some(l) if !l.ends_with('-') && width_of(&format!("{l} {p}"), face) <= 45.0 => {
                    l.push(' ');
                    l.push_str(&p);
                }
                _ => lines.push(p),
            }
        }
        let widest = lines.iter().map(|l| width_of(l, face)).fold(0.0, f32::max);
        let size = if widest > 45.0 {
            9.2 * 45.0 / widest
        } else {
            9.2
        };
        StopFace {
            lines,
            size,
            pitch,
            reed,
            drawn,
        }
    }
}

/// A drawstop seen from the bench: an ebony collar, an ivory face with the
/// name engraved, drawn out toward the player when it is on, with a ring of
/// light so a drawn stop reads from across the room.
pub(crate) fn drawstop(scene: &mut Scene, look: &Look, c: Point, face: &StopFace) {
    let r = 28.0;
    let collar = 35.0;
    scene.shadow(
        Circle::new(c, collar),
        Shadow::new(Color::hex_rgba(0x0000_0090))
            .with_offset(0.0, 3.0)
            .with_blur(7.0),
    );
    let ring = [Stop::new(0.0, look.ebony_light), Stop::new(1.0, look.ebony)];
    scene.fill(
        Circle::new(c, collar),
        RadialGradient {
            center: Point::new(c.x - 8.0, c.y - 14.0),
            radius: collar * 1.5,
            stops: &ring,
            extend: Extend::Pad,
        },
    );
    scene.stroke(
        Circle::new(c, collar - 0.5),
        Stroke::new(1.0),
        Color::hex_rgba(0xFFFF_FF1C),
    );
    scene.fill(Circle::new(c, r + 2.5), Color::hex(0x060403));
    if face.drawn {
        scene.shadow(
            Circle::new(c, r + 3.0),
            Shadow::glow(12.0, look.accent.with_alpha(0xA0)),
        );
        scene.stroke(Circle::new(c, r + 3.2), Stroke::new(1.8), look.brass_light);
    }
    let (fc, fr) = if face.drawn {
        (Point::new(c.x, c.y - 2.0), r * 1.05)
    } else {
        (c, r * 0.95)
    };
    scene.shadow(
        Circle::new(fc, fr),
        Shadow::new(Color::hex_rgba(0x0000_00B0))
            .with_offset(0.0, if face.drawn { 6.0 } else { 1.5 })
            .with_blur(if face.drawn { 9.0 } else { 3.0 }),
    );
    let porcelain = [
        Stop::new(0.0, look.ivory_light),
        Stop::new(0.55, look.ivory),
        Stop::new(1.0, look.ivory_shade),
    ];
    scene.fill(
        Circle::new(fc, fr),
        RadialGradient {
            center: Point::new(fc.x - fr * 0.35, fc.y - fr * 0.45),
            radius: fr * 1.7,
            stops: &porcelain,
            extend: Extend::Pad,
        },
    );
    if !face.drawn {
        // Pushed in, it sits back in the collar's shade.
        scene.fill(
            Circle::new(fc, fr),
            Color::hex_rgba(if look.night { 0x0000_0050 } else { 0x2010_0030 }),
        );
    }
    scene.stroke(
        Circle::new(fc, fr - 1.6),
        Stroke::new(1.0),
        look.ivory_shade.with_alpha(0x90),
    );
    // The engraving.
    let ink = if face.reed { look.reed_ink } else { look.ink };
    let name = style(face.size, 640.0, ink);
    let pitch = style(11.0, 760.0, ink);
    let line_h = 10.5;
    let pitch_h = if face.pitch.is_empty() { 0.0 } else { 13.0 };
    let total = face.lines.len() as f32 * line_h + pitch_h;
    let mut y = fc.y - total / 2.0 - 1.0;
    for l in &face.lines {
        text(scene, l, name, Point::new(fc.x, y), 0.5);
        y += line_h;
    }
    if !face.pitch.is_empty() {
        text(scene, &face.pitch, pitch, Point::new(fc.x, y + 1.0), 0.5);
    }
}

/// A coupler or tremulant tablet: ivory, tipped down when on.
pub(crate) fn tablet(scene: &mut Scene, look: &Look, rect: Rect, lines: &[&str], on: bool) {
    let shape = RoundedRect::new(rect, 6.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(0x0000_00A0))
            .with_offset(0.0, if on { 1.0 } else { 4.0 })
            .with_blur(if on { 2.0 } else { 6.0 }),
    );
    let stops = if on {
        [
            Stop::new(0.0, look.ivory_shade),
            Stop::new(0.5, look.ivory),
            Stop::new(1.0, look.ivory_light),
        ]
    } else {
        [
            Stop::new(0.0, look.ivory_light),
            Stop::new(0.6, look.ivory),
            Stop::new(1.0, look.ivory_shade),
        ]
    };
    scene.fill(shape, vertical(rect, &stops));
    if on {
        scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x80)));
        scene.stroke(shape, Stroke::new(1.6), look.brass_light);
    } else {
        scene.fill(
            shape,
            Color::hex_rgba(if look.night { 0x0000_0040 } else { 0x0000_0010 }),
        );
    }
    let s = style(9.8, 680.0, look.ink);
    let h = lines.len() as f32 * 12.0;
    for (i, l) in lines.iter().enumerate() {
        text(
            scene,
            l,
            s,
            Point::new(rect.center().x, rect.center().y - h / 2.0 + i as f32 * 12.0),
            0.5,
        );
    }
}

/// A round piston: ivory for the generals, red for set, ebony for the rest.
#[derive(Clone, Copy, PartialEq)]
pub(crate) enum PistonKind {
    Ivory,
    Ebony,
    Set,
}

pub(crate) fn piston(
    scene: &mut Scene,
    look: &Look,
    c: Point,
    r: f32,
    label: &str,
    kind: PistonKind,
    lit: bool,
) {
    scene.fill(Circle::new(c, r + 4.0), look.ebony);
    scene.stroke(
        Circle::new(c, r + 4.0),
        Stroke::new(1.0),
        Color::hex_rgba(0xFFFF_FF18),
    );
    if lit {
        scene.shadow(
            Circle::new(c, r + 2.0),
            Shadow::glow(12.0, look.accent.with_alpha(0xB0)),
        );
        scene.stroke(Circle::new(c, r + 2.2), Stroke::new(1.8), look.brass_light);
    }
    let (light, mid, dark, ink) = match kind {
        PistonKind::Ivory => (look.ivory_light, look.ivory, look.ivory_shade, look.ink),
        PistonKind::Ebony => (
            Color::hex(0x4A3C32),
            Color::hex(0x2A211B),
            Color::hex(0x120D0A),
            look.ivory,
        ),
        PistonKind::Set => (
            Color::hex(0xE0705E),
            look.set_red,
            Color::hex(0x6E1C14),
            Color::hex(0xFFF3E6),
        ),
    };
    scene.shadow(
        Circle::new(c, r),
        Shadow::new(Color::hex_rgba(0x0000_00B0))
            .with_offset(0.0, if lit { 1.0 } else { 3.0 })
            .with_blur(4.0),
    );
    let stops = [
        Stop::new(0.0, light),
        Stop::new(0.6, mid),
        Stop::new(1.0, dark),
    ];
    scene.fill(
        Circle::new(c, r),
        RadialGradient {
            center: Point::new(c.x - 5.0, c.y - 7.0),
            radius: r * 1.8,
            stops: &stops,
            extend: Extend::Pad,
        },
    );
    let s = style(r * 0.7, 760.0, ink);
    let h = TextLayout::new(label, &s, None).size().height;
    text(scene, label, s, Point::new(c.x, c.y - h / 2.0), 0.5);
}

/// A triangle pointing left or right, for the sequencer.
pub(crate) fn arrow(scene: &mut Scene, c: Point, right: bool, color: Color) {
    let d = if right { 1.0 } else { -1.0 };
    let mut p = manguebyte_gfx::Path::new();
    p.move_to(Point::new(c.x + 4.5 * d, c.y));
    p.line_to(Point::new(c.x - 3.5 * d, c.y - 5.0));
    p.line_to(Point::new(c.x - 3.5 * d, c.y + 5.0));
    p.close();
    scene.fill(&p, color);
}

/// A wind gauge: a brass bezel, a dial in millimetres of water, the
/// nominal pressure's band, the needle, and below the last two seconds.
#[allow(clippy::too_many_arguments)]
pub(crate) fn gauge(
    scene: &mut Scene,
    look: &Look,
    c: Point,
    r: f32,
    name: &str,
    nominal_mm: f32,
    now_mm: f32,
    trace: &[f32],
    note: &str,
) {
    let start = 0.75 * std::f32::consts::PI;
    let sweep = 1.5 * std::f32::consts::PI;
    let max: f32 = if nominal_mm > 95.0 { 160.0 } else { 120.0 };
    let ticks = (max / 5.0) as u32;
    let angle = |mm: f32| start + sweep * (mm / max).clamp(0.0, 1.0);
    let at = |a: f32, radius: f32| Point::new(c.x + radius * a.cos(), c.y + radius * a.sin());

    scene.shadow(
        Circle::new(c, r + 7.0),
        Shadow::new(Color::hex_rgba(0x0000_00A0))
            .with_offset(0.0, 4.0)
            .with_blur(10.0),
    );
    let bezel = [
        Stop::new(0.0, look.brass_light),
        Stop::new(0.5, look.brass),
        Stop::new(1.0, look.brass_dark),
    ];
    scene.fill(
        Circle::new(c, r + 7.0),
        LinearGradient {
            start: Point::new(c.x - r, c.y - r),
            end: Point::new(c.x + r, c.y + r),
            stops: &bezel,
            extend: Extend::Pad,
        },
    );
    let dial = [
        Stop::new(0.0, look.dial),
        Stop::new(
            1.0,
            if look.night {
                Color::hex(0x0A0705)
            } else {
                look.ivory_shade
            },
        ),
    ];
    scene.fill(
        Circle::new(c, r),
        RadialGradient {
            center: Point::new(c.x, c.y - r * 0.4),
            radius: r * 1.6,
            stops: &dial,
            extend: Extend::Pad,
        },
    );
    // The band around the nominal pressure, ±5 %.
    let band = manguebyte_gfx::Arc::new(
        c,
        r - 7.0,
        angle(nominal_mm * 0.95),
        angle(nominal_mm * 1.05) - angle(nominal_mm * 0.95),
    );
    scene.stroke(band, Stroke::new(5.0), look.good.with_alpha(0xC0));
    // Ticks every 5 mm; figures every 20, or every 40 on a small dial or a
    // long scale, sized to the dial and kept clear of the hub.
    let small = r < 44.0;
    let major_every = if ticks > 24 || small { 8 } else { 4 };
    let tick = if small { 7.0 } else { 12.0 };
    let figure_size = (r * 0.2).clamp(7.0, 8.5);
    for k in 0..=ticks {
        let mm = k as f32 * 5.0;
        let a = angle(mm);
        let major = k % major_every == 0;
        let inner = if major { r - tick } else { r - tick * 0.6 };
        let mut p = manguebyte_gfx::Path::new();
        p.move_to(at(a, inner));
        p.line_to(at(a, r - 3.0));
        scene.stroke(
            &p,
            Stroke::new(if major { 1.4 } else { 0.8 }),
            look.dial_ink.with_alpha(if major { 0xE0 } else { 0x90 }),
        );
        if major && k != 0 && k != ticks {
            let s = style(figure_size, 600.0, look.dial_ink.with_alpha(0xC0));
            let figure = format!("{}", mm as u32);
            let size = TextLayout::new(&figure, &s, None).size();
            // The figure's centre, as far in as its own half-width needs.
            let p = at(a, r - tick - 2.0 - size.width.max(size.height) / 2.0);
            text(
                scene,
                &figure,
                s,
                Point::new(p.x, p.y - size.height / 2.0),
                0.5,
            );
        }
    }
    // The reading, in the dial's lower half where the needle seldom goes.
    let reading_size = (r * 0.34).clamp(10.0, 17.0);
    let s = style(reading_size, 700.0, look.dial_ink);
    let reading = format!("{now_mm:.1}");
    let top = c.y + r * 0.3;
    text(scene, &reading, s, Point::new(c.x, top), 0.5);
    if !small {
        text(
            scene,
            "mm",
            style(8.5, 600.0, look.dial_ink.with_alpha(0xA0)),
            Point::new(c.x, top + reading_size + 3.0),
            0.5,
        );
    }
    // The needle.
    let a = angle(now_mm);
    let mut needle = manguebyte_gfx::Path::new();
    let tip = at(a, r - 6.0);
    let back = at(a + std::f32::consts::PI, (r * 0.2).min(10.0));
    let side = |s: f32| at(a + std::f32::consts::FRAC_PI_2 * s, 2.2);
    needle.move_to(tip);
    needle.line_to(side(1.0));
    needle.line_to(back);
    needle.line_to(side(-1.0));
    needle.close();
    scene.shadow(
        &needle,
        Shadow::new(Color::hex_rgba(0x0000_0080))
            .with_offset(0.0, 2.0)
            .with_blur(3.0),
    );
    scene.fill(&needle, look.set_red);
    scene.fill(Circle::new(c, 5.0), look.brass);
    scene.fill(
        Circle::new(Point::new(c.x - 1.0, c.y - 1.0), 2.0),
        look.brass_light,
    );

    // Its name and what it says.
    text(
        scene,
        name,
        style(12.5, 680.0, look.text),
        Point::new(c.x, c.y + r + 12.0),
        0.5,
    );
    text(
        scene,
        note,
        style(10.5, 450.0, look.muted),
        Point::new(c.x, c.y + r + 28.0),
        0.5,
    );
    // The last two seconds.
    let w = 140.0;
    let h = 22.0;
    let x0 = c.x - w / 2.0;
    let y0 = c.y + r + 46.0;
    let well = RoundedRect::new(Rect::new(x0, y0, w, h), 5.0);
    scene.fill(
        well,
        if look.night {
            Color::hex_rgba(0x0000_0055)
        } else {
            Color::hex_rgba(0x5A3A_1A12)
        },
    );
    let lo = nominal_mm * 0.9;
    let hi = nominal_mm * 1.06;
    let y_of = |mm: f32| y0 + h - 3.0 - (mm - lo) / (hi - lo) * (h - 6.0);
    let mut nominal = manguebyte_gfx::Path::new();
    nominal.move_to(Point::new(x0 + 4.0, y_of(nominal_mm)));
    nominal.line_to(Point::new(x0 + w - 4.0, y_of(nominal_mm)));
    scene.stroke(&nominal, Stroke::new(0.8), look.muted.with_alpha(0x80));
    let mut line = manguebyte_gfx::Path::new();
    for (i, v) in trace.iter().enumerate() {
        let p = Point::new(
            x0 + 4.0 + (w - 8.0) * i as f32 / (trace.len() - 1) as f32,
            y_of(*v),
        );
        if i == 0 {
            line.move_to(p);
        } else {
            line.line_to(p);
        }
    }
    scene.stroke(&line, Stroke::round(1.6), look.accent);
}

/// A closed outline through `points`.
pub(crate) fn polygon(points: &[Point]) -> manguebyte_gfx::Path {
    let mut p = manguebyte_gfx::Path::new();
    for (i, at) in points.iter().enumerate() {
        if i == 0 {
            p.move_to(*at);
        } else {
            p.line_to(*at);
        }
    }
    p.close();
    p
}

/// A balanced shoe seen from the bench, its toe away from the player and
/// narrower for it; beside it how far it is open, below it its name.
pub(crate) fn shoe(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    position: f32,
    title: &str,
    value: &str,
) {
    let inset = rect.width * 0.12;
    let outline = polygon(&[
        Point::new(rect.x + inset, rect.y),
        Point::new(rect.right() - inset, rect.y),
        Point::new(rect.right(), rect.bottom()),
        Point::new(rect.x, rect.bottom()),
    ]);
    scene.shadow(
        &outline,
        Shadow::new(Color::hex_rgba(0x0000_00B0))
            .with_offset(0.0, 6.0)
            .with_blur(9.0),
    );
    let metal = [
        Stop::new(0.0, look.brass_dark),
        Stop::new(0.35, look.brass),
        Stop::new(1.0, look.brass_light),
    ];
    scene.fill(&outline, vertical(rect, &metal));
    scene.stroke(&outline, Stroke::new(1.0), look.brass_dark);
    // The tread, closer together toward the toe.
    for k in 1..9 {
        let t = k as f32 / 9.0;
        let v = t * t;
        let y = rect.bottom() - 6.0 - (rect.height - 12.0) * (1.0 - v);
        let half = rect.width / 2.0 - inset * (1.0 - (y - rect.y) / rect.height) - 7.0;
        let mut p = manguebyte_gfx::Path::new();
        p.move_to(Point::new(rect.center().x - half, y));
        p.line_to(Point::new(rect.center().x + half, y));
        scene.stroke(&p, Stroke::round(1.6), look.brass_dark.with_alpha(0xB0));
    }
    // The heel's edge, toward the player.
    scene.fill(
        Rect::new(rect.x, rect.bottom() - 3.0, rect.width, 5.0),
        look.brass_dark,
    );
    // How far open.
    let bar = Rect::new(rect.right() + 6.0, rect.y + 4.0, 5.0, rect.height - 4.0);
    scene.fill(RoundedRect::new(bar, 2.5), Color::hex_rgba(0x0000_0070));
    let lit = Rect::new(
        bar.x,
        bar.bottom() - bar.height * position,
        bar.width,
        bar.height * position,
    );
    scene.shadow(
        RoundedRect::new(lit, 2.5),
        Shadow::glow(8.0, look.accent.with_alpha(0x90)),
    );
    scene.fill(RoundedRect::new(lit, 2.5), look.accent);
    let x = rect.center().x + 4.0;
    text(
        scene,
        title,
        style(11.0, 700.0, look.text),
        Point::new(x, rect.bottom() + 10.0),
        0.5,
    );
    text(
        scene,
        value,
        style(10.0, 600.0, look.wood_accent),
        Point::new(x, rect.bottom() + 25.0),
        0.5,
    );
}

#[cfg(test)]
mod tests {
    use manguebyte_gfx::{Pixmap, Point, Rect, Renderer, Scene};

    use super::gauge;
    use crate::ui::look::{DAY, NIGHT};

    /// Gauges small and large, by night and by day, drawn for the eye:
    /// `tmp/console/gauges.png` in the target folder.
    #[test]
    fn gauges_of_every_size_are_drawn() {
        let radii = [28.0, 36.0, 44.0, 56.0];
        let mut scene = Scene::new();
        for (row, look) in [&NIGHT, &DAY].into_iter().enumerate() {
            let y = row as f32 * 230.0;
            scene.fill(Rect::new(0.0, y, 760.0, 230.0), look.panel_bottom);
            for (i, r) in radii.iter().enumerate() {
                let c = Point::new(95.0 + i as f32 * 190.0, y + 20.0 + r);
                let trace = [99.0, 100.2, 99.4, 98.8, 99.9];
                let (nominal, now) = if i % 2 == 0 {
                    (102.0, 99.9)
                } else {
                    (84.5, 82.8)
                };
                gauge(
                    &mut scene, look, c, *r, "Pédale", nominal, now, &trace, "steady",
                );
            }
        }
        let mut pixels = Pixmap::new(760, 460).unwrap();
        Renderer::new().render(&scene, &mut pixels, 1.0);
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target-tmp-console");
        let dir = std::env::var_os("CARGO_TARGET_DIR")
            .map(|t| std::path::PathBuf::from(t).join("tmp/console"))
            .unwrap_or(dir);
        let _ = std::fs::create_dir_all(&dir);
        std::fs::write(dir.join("gauges.png"), pixels.encode_png().unwrap()).unwrap();
    }
}
