//! How the installer's parts are drawn, in the console's manner: a turned
//! wooden jamb with gilded beading, a brass name plate, ivory tablets,
//! brass and ivory buttons, small drawknobs, a wind gauge.

use manguebyte_gfx::{
    Arc, Circle, Color, Extend, LinearGradient, Path, Point, RadialGradient, Rect, RoundedRect,
    Scene, Shadow, Size, Stop, Stroke, TextLayout, TextStyle,
};

use super::look::Look;

/// A style of `size` and `weight` in `color`.
pub fn style(size: f32, weight: f32, color: Color) -> TextStyle {
    TextStyle::new(size).with_weight(weight).with_color(color)
}

/// One line of text, its top at `at.y`, its `anchor` (0 start, 0.5 centre,
/// 1 end) at `at.x`, cut with an ellipsis to `max` wide. Returns its size.
pub fn text(
    scene: &mut Scene,
    s: &str,
    style: TextStyle,
    at: Point,
    anchor: f32,
    max: Option<f32>,
) -> Size {
    let mut style = style;
    if max.is_some() {
        style.max_lines = Some(1);
    }
    let layout = TextLayout::new(s, &style, max);
    let size = layout.size();
    scene.text(&layout, Point::new(at.x - size.width * anchor, at.y));
    size
}

/// A paragraph wrapped to `width`, its top left at `at`. Returns its
/// height.
pub fn paragraph(scene: &mut Scene, s: &str, style: TextStyle, at: Point, width: f32) -> f32 {
    let layout = TextLayout::new(s, &style, Some(width));
    scene.text(&layout, at);
    layout.size().height
}

/// The height `s` takes wrapped to `width`.
pub fn height_of(s: &str, style: TextStyle, width: f32) -> f32 {
    TextLayout::new(s, &style, Some(width)).size().height
}

/// The width of one line of `s`.
pub fn width_of(s: &str, style: TextStyle) -> f32 {
    TextLayout::new(s, &style, None).size().width
}

fn vertical(rect: Rect, stops: &[Stop]) -> LinearGradient<'_> {
    LinearGradient {
        start: Point::new(rect.x, rect.y),
        end: Point::new(rect.x, rect.bottom()),
        stops,
        extend: Extend::Pad,
    }
}

fn inset(rect: Rect, by: f32) -> Rect {
    Rect::new(
        rect.x + by,
        rect.y + by,
        rect.width - 2.0 * by,
        rect.height - 2.0 * by,
    )
}

/// The turned wooden jamb: the wood's gradient, its grain, the gilded
/// beading a case maker runs round a panel.
pub fn jamb(scene: &mut Scene, look: &Look, rect: Rect) {
    let shape = RoundedRect::new(rect, 14.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(if look.night {
            0x0000_0088
        } else {
            0x5A3A_1A44
        }))
        .with_offset(0.0, 8.0)
        .with_blur(24.0)
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
            let mut path = Path::new();
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
        RoundedRect::new(inset(rect, 0.5), 13.5),
        Stroke::new(1.0),
        look.sheen,
    );
    let bead = RoundedRect::new(inset(rect, 7.0), 9.0);
    scene.stroke(bead, Stroke::new(2.4), Color::hex_rgba(0x0000_0050));
    scene.stroke(bead, Stroke::new(1.2), look.brass.with_alpha(0xB0));
}

/// The panel: leather by night, paper by day.
pub fn panel(scene: &mut Scene, look: &Look, rect: Rect) {
    let shape = RoundedRect::new(rect, 12.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(if look.night {
            0x0000_0070
        } else {
            0x5A3A_1A22
        }))
        .with_offset(0.0, 6.0)
        .with_blur(18.0),
    );
    let stops = [
        Stop::new(0.0, look.panel_top),
        Stop::new(1.0, look.panel_bottom),
    ];
    scene.fill(shape, vertical(rect, &stops));
    scene.stroke(
        RoundedRect::new(inset(rect, 0.5), 11.5),
        Stroke::new(1.0),
        look.edge,
    );
}

/// The engraved brass name plate, with its screws.
pub fn plate(scene: &mut Scene, look: &Look, rect: Rect, title: &str, note: &str) {
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
    // Spaced capitals, as engraved.
    let spaced: String = title.chars().flat_map(|c| [c, '\u{2009}']).collect();
    let s = style(17.0, 760.0, look.plate_ink);
    text(
        scene,
        spaced.trim_end(),
        s,
        Point::new(rect.center().x, rect.y + 8.0),
        0.5,
        None,
    );
    text(
        scene,
        note,
        style(8.5, 650.0, look.plate_ink.with_alpha(0xB8)),
        Point::new(rect.center().x, rect.bottom() - 13.0),
        0.5,
        None,
    );
}

/// A step on the jamb: an ivory tablet with its number, lit when it is the
/// step under way.
pub fn step(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    number: &str,
    label: &str,
    state: StepState,
) {
    let shape = RoundedRect::new(rect, 6.0);
    let on = state == StepState::Now;
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(0x0000_0070))
            .with_offset(0.0, if on { 1.0 } else { 3.0 })
            .with_blur(if on { 2.0 } else { 5.0 }),
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
    } else if state == StepState::Later {
        scene.fill(
            shape,
            Color::hex_rgba(if look.night { 0x0000_0060 } else { 0x0000_0018 }),
        );
    }
    let c = Point::new(rect.x + 20.0, rect.center().y);
    let disc = match state {
        StepState::Done => look.good,
        StepState::Now => look.brass_dark,
        StepState::Later => look.ebony,
    };
    scene.fill(Circle::new(c, 9.0), disc);
    if state == StepState::Done {
        tick(
            scene,
            Rect::new(c.x - 5.0, c.y - 5.0, 10.0, 10.0),
            look.ivory_light,
            1.8,
        );
    } else {
        let s = style(10.0, 700.0, look.ivory_light);
        let h = TextLayout::new(number, &s, None).size().height;
        text(scene, number, s, Point::new(c.x, c.y - h / 2.0), 0.5, None);
    }
    let s = style(
        12.0,
        680.0,
        look.ink.with_alpha(if state == StepState::Later {
            0x99
        } else {
            0xFF
        }),
    );
    let h = TextLayout::new(label, &s, None).size().height;
    text(
        scene,
        label,
        s,
        Point::new(rect.x + 38.0, rect.center().y - h / 2.0),
        0.0,
        Some(rect.width - 44.0),
    );
}

/// Where a step stands.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StepState {
    /// Behind.
    Done,
    /// Under way.
    Now,
    /// Ahead.
    Later,
}

/// What a button looks like.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Button {
    /// Brass: the way forward.
    Primary,
    /// Ivory.
    Secondary,
    /// Only an outline.
    Quiet,
    /// Red, for removal.
    Danger,
}

/// A button: an ivory or brass tablet, the administrator's shield before
/// its label if `shield`, dimmed when not `enabled`.
#[allow(clippy::too_many_arguments)]
pub fn button(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    label: &str,
    kind: Button,
    shield: bool,
    enabled: bool,
    pressed: bool,
) {
    let shape = RoundedRect::new(rect, 6.0);
    let (light, mid, dark, ink, edge) = match kind {
        Button::Primary => (
            look.brass_light,
            look.brass,
            look.brass_dark,
            look.plate_ink,
            look.brass_dark,
        ),
        Button::Secondary => (
            look.ivory_light,
            look.ivory,
            look.ivory_shade,
            look.ink,
            look.ivory_shade,
        ),
        Button::Danger => (
            Color::hex(0xE0705E),
            look.set_red,
            Color::hex(0x6E1C14),
            Color::hex(0xFFF3E6),
            Color::hex(0x6E1C14),
        ),
        Button::Quiet => (
            Color::TRANSPARENT,
            Color::TRANSPARENT,
            Color::TRANSPARENT,
            look.text2,
            look.edge,
        ),
    };
    if kind != Button::Quiet {
        scene.shadow(
            shape,
            Shadow::new(Color::hex_rgba(0x0000_0060))
                .with_offset(0.0, if pressed { 1.0 } else { 3.0 })
                .with_blur(if pressed { 2.0 } else { 5.0 }),
        );
        let stops = [
            Stop::new(0.0, light),
            Stop::new(0.55, mid),
            Stop::new(1.0, dark),
        ];
        scene.fill(shape, vertical(rect, &stops));
    }
    scene.stroke(
        RoundedRect::new(inset(rect, 0.5), 5.5),
        Stroke::new(1.0),
        edge,
    );
    let s = style(13.0, 620.0, ink);
    let w = width_of(label, s) + if shield { 20.0 } else { 0.0 };
    let h = TextLayout::new(label, &s, None).size().height;
    let x = rect.center().x - w / 2.0;
    if shield {
        uac_shield(scene, Rect::new(x, rect.center().y - 7.5, 13.0, 15.0));
    }
    text(
        scene,
        label,
        s,
        Point::new(
            x + if shield { 20.0 } else { 0.0 },
            rect.center().y - h / 2.0,
        ),
        0.0,
        None,
    );
    if !enabled {
        let veil = if look.night { 0x0000_0088 } else { 0xFFFF_FF88 };
        scene.fill(shape, Color::hex_rgba(veil));
    }
}

/// The administrator's shield, as Windows draws it on a button that asks.
pub fn uac_shield(scene: &mut Scene, rect: Rect) {
    let p = |x: f32, y: f32| Point::new(rect.x + rect.width * x, rect.y + rect.height * y);
    let mut outline = Path::new();
    outline.move_to(p(0.5, 0.0));
    outline.line_to(p(1.0, 0.15));
    outline.cubic_to(p(1.0, 0.55), p(0.85, 0.8), p(0.5, 1.0));
    outline.cubic_to(p(0.15, 0.8), p(0.0, 0.55), p(0.0, 0.15));
    outline.close();
    let blue = Color::hex(0x2F6FD6);
    let gold = Color::hex(0xF2C230);
    scene.fill(&outline, blue);
    scene.clipped(&outline, |scene| {
        scene.fill(
            Rect::new(rect.center().x, rect.y, rect.width / 2.0, rect.height / 2.0),
            gold,
        );
        scene.fill(
            Rect::new(rect.x, rect.center().y, rect.width / 2.0, rect.height / 2.0),
            gold,
        );
    });
    scene.stroke(&outline, Stroke::new(0.8), Color::hex_rgba(0x0000_0060));
}

/// A small drawknob: ebony with an ivory face, brass and glowing when
/// drawn.
pub fn knob(scene: &mut Scene, look: &Look, c: Point, on: bool) {
    if on {
        scene.shadow(
            Circle::new(c, 11.0),
            Shadow::glow(8.0, look.accent.with_alpha(0x90)),
        );
    }
    scene.shadow(
        Circle::new(c, 11.0),
        Shadow::new(Color::hex_rgba(0x0000_0080))
            .with_offset(0.0, 2.0)
            .with_blur(3.0),
    );
    let stops = [Stop::new(0.0, look.ebony_light), Stop::new(0.7, look.ebony)];
    scene.fill(
        Circle::new(c, 11.0),
        RadialGradient {
            center: Point::new(c.x - 3.0, c.y - 4.0),
            radius: 16.0,
            stops: &stops,
            extend: Extend::Pad,
        },
    );
    if on {
        scene.stroke(Circle::new(c, 11.6), Stroke::new(1.4), look.brass_light);
    }
    let face = if on {
        [
            Stop::new(0.0, look.brass_light),
            Stop::new(0.6, look.brass),
            Stop::new(1.0, look.brass_dark),
        ]
    } else {
        [
            Stop::new(0.0, look.ivory_light),
            Stop::new(0.6, look.ivory),
            Stop::new(1.0, look.ivory_shade),
        ]
    };
    scene.fill(
        Circle::new(c, 6.0),
        RadialGradient {
            center: Point::new(c.x - 2.0, c.y - 2.5),
            radius: 9.0,
            stops: &face,
            extend: Extend::Pad,
        },
    );
}

/// A tick inside `rect`.
pub fn tick(scene: &mut Scene, rect: Rect, color: Color, width: f32) {
    let mut path = Path::new();
    path.move_to(Point::new(
        rect.x + rect.width * 0.12,
        rect.y + rect.height * 0.52,
    ));
    path.line_to(Point::new(
        rect.x + rect.width * 0.4,
        rect.y + rect.height * 0.8,
    ));
    path.line_to(Point::new(
        rect.x + rect.width * 0.9,
        rect.y + rect.height * 0.2,
    ));
    scene.stroke(&path, Stroke::round(width), color);
}

/// A small triangle pointing down (`open`) or right, as a fold shows.
pub fn fold(scene: &mut Scene, c: Point, open: bool, color: Color) {
    let mut path = Path::new();
    if open {
        path.move_to(Point::new(c.x - 4.0, c.y - 2.0));
        path.line_to(Point::new(c.x + 4.0, c.y - 2.0));
        path.line_to(Point::new(c.x, c.y + 3.0));
    } else {
        path.move_to(Point::new(c.x - 2.0, c.y - 4.0));
        path.line_to(Point::new(c.x + 3.0, c.y));
        path.line_to(Point::new(c.x - 2.0, c.y + 4.0));
    }
    path.close();
    scene.fill(&path, color);
}

/// A check box.
pub fn check(scene: &mut Scene, look: &Look, rect: Rect, on: bool) {
    let shape = RoundedRect::new(rect, 4.0);
    if on {
        scene.fill(shape, look.accent);
        tick(scene, inset(rect, 3.0), look.plate_ink, 2.0);
    } else {
        scene.fill(shape, look.field);
        scene.stroke(
            RoundedRect::new(inset(rect, 0.5), 3.5),
            Stroke::new(1.0),
            look.muted,
        );
    }
}

/// A card raised on the panel, outlined in brass and glowing when chosen.
pub fn card(scene: &mut Scene, look: &Look, rect: Rect, chosen: bool) {
    let shape = RoundedRect::new(rect, 10.0);
    scene.fill(shape, look.raised);
    if chosen {
        scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x50)));
        scene.stroke(
            RoundedRect::new(inset(rect, 0.75), 9.25),
            Stroke::new(1.5),
            look.brass,
        );
    } else {
        scene.stroke(
            RoundedRect::new(inset(rect, 0.5), 9.5),
            Stroke::new(1.0),
            look.edge,
        );
    }
}

/// A field sunk into the panel.
pub fn field(scene: &mut Scene, look: &Look, rect: Rect) {
    let shape = RoundedRect::new(rect, 6.0);
    scene.fill(shape, look.field);
    scene.stroke(
        RoundedRect::new(inset(rect, 0.5), 5.5),
        Stroke::new(1.0),
        look.edge,
    );
}

/// The ring a control with the keyboard focus wears.
pub fn focus_ring(scene: &mut Scene, look: &Look, rect: Rect, radius: f32) {
    let r = Rect::new(
        rect.x - 3.0,
        rect.y - 3.0,
        rect.width + 6.0,
        rect.height + 6.0,
    );
    scene.stroke(
        RoundedRect::new(r, radius + 3.0),
        Stroke::new(2.0),
        look.accent,
    );
}

/// The wind gauge: a dial in a brass rim, its scale, the share done as an
/// arc that fills and a needle; `fraction` from 0 to 1.
pub fn gauge(scene: &mut Scene, look: &Look, c: Point, r: f32, fraction: f32, word: &str) {
    let rim = [
        Stop::new(0.0, look.brass_light),
        Stop::new(0.5, look.brass),
        Stop::new(1.0, look.brass_dark),
    ];
    let outer = Rect::new(c.x - r, c.y - r, 2.0 * r, 2.0 * r);
    scene.shadow(
        Circle::new(c, r),
        Shadow::new(Color::hex_rgba(0x0000_0070))
            .with_offset(0.0, 4.0)
            .with_blur(10.0),
    );
    scene.fill(Circle::new(c, r), vertical(outer, &rim));
    scene.fill(Circle::new(c, r - 6.0), look.dial);
    // From the bottom left, round over the top, to the bottom right.
    let start = 135f32.to_radians();
    let sweep = 270f32.to_radians();
    for i in 0..=10 {
        let a = start + sweep * i as f32 / 10.0;
        let (r1, r2) = (r - 12.0, if i % 5 == 0 { r - 22.0 } else { r - 17.0 });
        let mut tick = Path::new();
        tick.move_to(Point::new(c.x + r1 * a.cos(), c.y + r1 * a.sin()));
        tick.line_to(Point::new(c.x + r2 * a.cos(), c.y + r2 * a.sin()));
        scene.stroke(
            &tick,
            Stroke::new(if i % 5 == 0 { 2.0 } else { 1.0 }),
            look.dial_ink.with_alpha(0xB0),
        );
    }
    let track = r - 30.0;
    scene.stroke(
        &Path::arc(Arc::new(c, track, start, sweep)),
        Stroke::round(5.0),
        look.edge,
    );
    let f = fraction.clamp(0.0, 1.0);
    if f > 0.0 {
        scene.stroke(
            &Path::arc(Arc::new(c, track, start, sweep * f)),
            Stroke::round(5.0),
            look.accent,
        );
    }
    let a = start + sweep * f;
    let mut needle = Path::new();
    needle.move_to(c);
    needle.line_to(Point::new(
        c.x + (r - 16.0) * a.cos(),
        c.y + (r - 16.0) * a.sin(),
    ));
    scene.stroke(&needle, Stroke::round(2.4), look.set_red);
    scene.fill(
        Circle::new(c, 6.0),
        vertical(Rect::new(c.x - 6.0, c.y - 6.0, 12.0, 12.0), &rim),
    );
    text(
        scene,
        word,
        style(8.5, 650.0, look.dial_ink.with_alpha(0xA0)),
        Point::new(c.x, c.y - r * 0.45),
        0.5,
        None,
    );
    let pct = format!("{}%", (f * 100.0).round() as u32);
    text(
        scene,
        &pct,
        style(16.0, 720.0, look.dial_ink),
        Point::new(c.x, c.y + r * 0.38),
        0.5,
        None,
    );
}
