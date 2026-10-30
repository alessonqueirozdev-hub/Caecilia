//! Caecilia's console as a picture: the mockup to agree on before the
//! console itself is written. Drawn headless with the toolkit the console
//! will be written in, from the demonstration organ as it is.
//!
//! ```text
//! cargo run -p caecilia-tools --example console_mockup -- <folder>
//! ```
//!
//! It writes `console-night@1x.png`, `console-night@2x.png` and the same
//! for `day` into the folder given, or the system's temporary folder.
//!
//! The console is a French one, as the demonstration organ is: the
//! Grand-Orgue and the Pédale on the left jamb, the Récit, its couplers and
//! its shoes on the right, reeds engraved in red. Between the jambs, where
//! the music desk would be: the registration (a selector line, the
//! history, why a stop is drawn), the wind of each chest on its gauge, and
//! the general pistons. The keyboard below lights what sounds.

use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::time::Duration;

use caecilia_model::{Organ, TonalFamily, demo_organ};
use caecilia_registration::{FACTORY_GENERALS, Piston, factory_generals};
use manguebyte_gfx::{
    Circle, Color, Extend, LinearGradient, Point, RadialGradient, Rect, RoundedRect, Scene, Shadow,
    Size, Stop, Stroke, TextLayout, TextStyle,
};
use manguebyte_plugin::editor::EditorContext;
use manguebyte_plugin::editor::testing::RecordingEditorHost;
use manguebyte_ui::layout::Align;
use manguebyte_ui::{
    IntoView, KeySpan, KeyboardState, Root, Theme, View, canvas, column, label, piano_keyboard,
    pill, row, stack,
};

/// The window, in logical pixels.
const SIZE: Size = Size::new(1440.0, 900.0);

/// A jamb's width.
const JAMB: f32 = 400.0;

/// The materials of the console, in one light.
#[derive(Clone, Copy)]
struct Look {
    night: bool,
    ground: Color,
    wood_top: Color,
    wood_bottom: Color,
    grain: Color,
    panel_top: Color,
    panel_bottom: Color,
    edge: Color,
    sheen: Color,
    ebony: Color,
    ebony_light: Color,
    ivory_light: Color,
    ivory: Color,
    ivory_shade: Color,
    ink: Color,
    reed_ink: Color,
    brass_light: Color,
    brass: Color,
    brass_dark: Color,
    plate_ink: Color,
    text: Color,
    text2: Color,
    muted: Color,
    accent: Color,
    dial: Color,
    dial_ink: Color,
    good: Color,
    set_red: Color,
    /// The accent where it is written on the wood.
    wood_accent: Color,
}

const NIGHT: Look = Look {
    night: true,
    ground: Color::hex(0x0D0A08),
    wood_top: Color::hex(0x2B1B13),
    wood_bottom: Color::hex(0x1A100B),
    grain: Color::hex_rgba(0x0000_0030),
    panel_top: Color::hex(0x211A15),
    panel_bottom: Color::hex(0x17120F),
    edge: Color::hex(0x3A2D23),
    sheen: Color::hex_rgba(0xFFF0_D814),
    ebony: Color::hex(0x120D0A),
    ebony_light: Color::hex(0x3A2E26),
    ivory_light: Color::hex(0xFFFBF0),
    ivory: Color::hex(0xEFE5CF),
    ivory_shade: Color::hex(0xC9B994),
    ink: Color::hex(0x1C1510),
    reed_ink: Color::hex(0xA3261B),
    brass_light: Color::hex(0xF6DA95),
    brass: Color::hex(0xCFA24E),
    brass_dark: Color::hex(0x8A6326),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0xF1E6D2),
    text2: Color::hex(0xC4B294),
    muted: Color::hex(0x8C7B65),
    accent: Color::hex(0xE0AE55),
    dial: Color::hex(0x16110D),
    dial_ink: Color::hex(0xE9DDC6),
    good: Color::hex(0x7FB77E),
    set_red: Color::hex(0xB33A2C),
    wood_accent: Color::hex(0xE0AE55),
};

const DAY: Look = Look {
    night: false,
    ground: Color::hex(0xEDE4D3),
    wood_top: Color::hex(0xD7BE95),
    wood_bottom: Color::hex(0xC3A273),
    grain: Color::hex_rgba(0x5A3A_1A22),
    panel_top: Color::hex(0xFBF6EC),
    panel_bottom: Color::hex(0xF3EBDC),
    edge: Color::hex(0xD9CAB0),
    sheen: Color::hex_rgba(0xFFFF_FF99),
    ebony: Color::hex(0x1A130F),
    ebony_light: Color::hex(0x4A3B30),
    ivory_light: Color::hex(0xFFFDF6),
    ivory: Color::hex(0xF6EEDC),
    ivory_shade: Color::hex(0xD4C29E),
    ink: Color::hex(0x1C1510),
    reed_ink: Color::hex(0xA3261B),
    brass_light: Color::hex(0xF4D58C),
    brass: Color::hex(0xC0913E),
    brass_dark: Color::hex(0x7E5A20),
    plate_ink: Color::hex(0x2B1D0C),
    text: Color::hex(0x2A1E14),
    text2: Color::hex(0x5E4B36),
    muted: Color::hex(0x8E7B63),
    accent: Color::hex(0xA8742A),
    dial: Color::hex(0xFBF6EA),
    dial_ink: Color::hex(0x2A1E14),
    good: Color::hex(0x4E8A4D),
    set_red: Color::hex(0xB33A2C),
    wood_accent: Color::hex(0x5E3A10),
};

/// The toolkit's theme in the console's materials: pills, labels and the
/// keyboard take their colours from it.
fn theme(look: &Look) -> Theme {
    let mut theme = if look.night {
        Theme::dark()
    } else {
        Theme::light()
    };
    let p = &mut theme.palette;
    p.background = look.ground;
    p.surface = look.panel_bottom;
    p.raised = look.panel_top;
    p.hairline = look.edge;
    p.text = look.text;
    p.text_secondary = look.text2;
    p.text_muted = look.muted;
    p.accent = look.accent;
    p.accent_bright = look.brass_light;
    p.accent_soft = look.accent.with_alpha(0x26);
    p.on_accent = look.plate_ink;
    p.focus = look.accent;
    p.key_white = look.ivory;
    p.key_white_shade = look.ivory_shade;
    p.key_black = look.ebony;
    p.key_black_top = look.ebony_light;
    if look.night {
        p.raised = Color::hex(0x2A2019);
        p.overlay = Color::hex(0x372A20);
        p.hairline = Color::hex(0x3E3026);
    }
    theme
}

/// The console mid-piece: General 4 (mf) recalled, then the Récit's
/// Hautbois and strings drawn by hand, the Récit coupled to the Grand-Orgue,
/// its tremulant on, a chord held.
struct Console {
    organ: Organ,
    drawn: Vec<bool>,
    couplers: Vec<bool>,
    tremulant: bool,
    general: usize,
    look: Look,
}

impl Console {
    fn new(look: Look) -> Rc<Console> {
        let organ = demo_organ();
        let mut drawn = vec![false; organ.stops().len()];
        let generals = factory_generals(&organ);
        if let Some((_, Piston::Snapshot(state))) = generals.get(3) {
            let mask = state.stops.to_mask();
            for (n, d) in drawn.iter_mut().enumerate() {
                *d = mask & (1 << n) != 0;
            }
        }
        for name in ["Basson-Hautbois 8", "Gambe 8", "Voix Céleste 8"] {
            if let Some(s) = organ.stops().iter().position(|s| s.name == name) {
                drawn[s] = true;
            }
        }
        let couplers = organ
            .couplers()
            .iter()
            .map(|c| c.name == "Récit/Grand-Orgue")
            .collect();
        Rc::new(Console {
            organ,
            drawn,
            couplers,
            tremulant: true,
            general: 3,
            look,
        })
    }

    /// The stops of the division named `division`, with their indices.
    fn stops_of(&self, division: &str) -> Vec<(usize, StopFace)> {
        let id = self
            .organ
            .divisions()
            .iter()
            .find(|d| d.name == division)
            .map(|d| d.id);
        self.organ
            .stops()
            .iter()
            .enumerate()
            .filter(|(_, s)| Some(s.division) == id)
            .map(|(n, s)| {
                (
                    n,
                    StopFace::of(&s.name, s.family == TonalFamily::Reed, self.drawn[n]),
                )
            })
            .collect()
    }
}

// ---------------------------------------------------------------- drawing

/// Text, its top at `at.y`, its `anchor` (0 start, 0.5 centre, 1 end) at
/// `at.x`. Returns its size.
fn text(scene: &mut Scene, s: &str, style: TextStyle, at: Point, anchor: f32) -> Size {
    let layout = TextLayout::new(s, &style, None);
    let size = layout.size();
    scene.text(&layout, Point::new(at.x - size.width * anchor, at.y));
    size
}

fn style(size: f32, weight: f32, color: Color) -> TextStyle {
    TextStyle::new(size).with_weight(weight).with_color(color)
}

fn width_of(s: &str, style: TextStyle) -> f32 {
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

/// A turned wooden jamb: the wood's gradient, its grain, a bevelled edge.
fn jamb(scene: &mut Scene, look: &Look, rect: Rect) {
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
}

/// A panel between the jambs: leather in the night, paper in the day.
fn panel(scene: &mut Scene, look: &Look, rect: Rect) {
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
}

/// A card's heading inside a panel.
fn heading(scene: &mut Scene, look: &Look, rect: Rect, title: &str, meta: &str) {
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
fn plate(scene: &mut Scene, look: &Look, rect: Rect, title: &str, note: &str) {
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
struct StopFace {
    lines: Vec<String>,
    /// The name's size: smaller for a long word.
    size: f32,
    pitch: String,
    reed: bool,
    drawn: bool,
}

impl StopFace {
    fn of(name: &str, reed: bool, drawn: bool) -> StopFace {
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
        let face = style(9.6, 640.0, Color::BLACK);
        let mut pieces = Vec::new();
        for w in &words[..split] {
            match w.split_once('-') {
                Some((a, b)) if width_of(w, face) > 50.0 => {
                    pieces.push(format!("{a}-"));
                    pieces.push(b.to_string());
                }
                _ => pieces.push((*w).to_string()),
            }
        }
        let mut lines: Vec<String> = Vec::new();
        for p in pieces {
            match lines.last_mut() {
                Some(l) if !l.ends_with('-') && width_of(&format!("{l} {p}"), face) <= 50.0 => {
                    l.push(' ');
                    l.push_str(&p);
                }
                _ => lines.push(p),
            }
        }
        let widest = lines.iter().map(|l| width_of(l, face)).fold(0.0, f32::max);
        let size = if widest > 50.0 {
            9.6 * 50.0 / widest
        } else {
            9.6
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
fn drawstop(scene: &mut Scene, look: &Look, c: Point, face: &StopFace) {
    let r = 31.0;
    let collar = 39.0;
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
    let pitch = style(12.0, 760.0, ink);
    let line_h = 11.0;
    let pitch_h = if face.pitch.is_empty() { 0.0 } else { 14.0 };
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
fn tablet(scene: &mut Scene, look: &Look, rect: Rect, lines: &[&str], on: bool) {
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
enum PistonKind {
    Ivory,
    Ebony,
    Set,
}

fn piston(scene: &mut Scene, look: &Look, c: Point, label: &str, kind: PistonKind, lit: bool) {
    let r = 16.0;
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
    let s = style(11.0, 760.0, ink);
    let h = TextLayout::new(label, &s, None).size().height;
    text(scene, label, s, Point::new(c.x, c.y - h / 2.0), 0.5);
}

/// A triangle pointing left or right, for the sequencer.
fn arrow(scene: &mut Scene, c: Point, right: bool, color: Color) {
    let d = if right { 1.0 } else { -1.0 };
    let mut p = manguebyte_gfx::Path::new();
    p.move_to(Point::new(c.x + 5.0 * d, c.y));
    p.line_to(Point::new(c.x - 4.0 * d, c.y - 6.0));
    p.line_to(Point::new(c.x - 4.0 * d, c.y + 6.0));
    p.close();
    scene.fill(&p, color);
}

/// A wind gauge: a brass bezel, a dial in millimetres of water, the
/// nominal pressure's band, the needle, and below the last two seconds.
#[allow(clippy::too_many_arguments)]
fn gauge(
    scene: &mut Scene,
    look: &Look,
    c: Point,
    name: &str,
    nominal_mm: f32,
    now_mm: f32,
    trace: &[f32],
    note: &str,
) {
    let r = 56.0;
    let start = 0.75 * std::f32::consts::PI;
    let sweep = 1.5 * std::f32::consts::PI;
    let max = 120.0;
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
    // Ticks every 5 mm, figures every 20.
    for k in 0..=24 {
        let mm = k as f32 * 5.0;
        let a = angle(mm);
        let major = k % 4 == 0;
        let inner = if major { r - 12.0 } else { r - 8.0 };
        let mut p = manguebyte_gfx::Path::new();
        p.move_to(at(a, inner));
        p.line_to(at(a, r - 3.0));
        scene.stroke(
            &p,
            Stroke::new(if major { 1.4 } else { 0.8 }),
            look.dial_ink.with_alpha(if major { 0xE0 } else { 0x90 }),
        );
        if major && k != 0 && k != 24 {
            let s = style(8.5, 600.0, look.dial_ink.with_alpha(0xC0));
            let figure = format!("{}", mm as u32);
            let h = TextLayout::new(&figure, &s, None).size().height;
            let p = at(a, r - 21.0);
            text(scene, &figure, s, Point::new(p.x, p.y - h / 2.0), 0.5);
        }
    }
    // The reading.
    let s = style(17.0, 700.0, look.dial_ink);
    let reading = format!("{now_mm:.1}");
    text(scene, &reading, s, Point::new(c.x, c.y + 17.0), 0.5);
    text(
        scene,
        "mm",
        style(8.5, 600.0, look.dial_ink.with_alpha(0xA0)),
        Point::new(c.x, c.y + 37.0),
        0.5,
    );
    // The needle.
    let a = angle(now_mm);
    let mut needle = manguebyte_gfx::Path::new();
    let tip = at(a, r - 6.0);
    let back = at(a + std::f32::consts::PI, 10.0);
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
        Point::new(c.x, c.y + r + 16.0),
        0.5,
    );
    text(
        scene,
        note,
        style(10.5, 450.0, look.muted),
        Point::new(c.x, c.y + r + 33.0),
        0.5,
    );
    // The last two seconds.
    let w = 150.0;
    let h = 26.0;
    let x0 = c.x - w / 2.0;
    let y0 = c.y + r + 54.0;
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

/// The wind of a chest over two seconds: `sag` taken when the chord began,
/// recovering, and the tremulant's swing if it is on.
fn wind_trace(nominal: f32, sag: f32, tremulant: Option<f32>) -> Vec<f32> {
    (0..120)
        .map(|i| {
            let t = i as f32 / 60.0;
            let attack = if t > 0.35 {
                let s = t - 0.35;
                -sag * nominal * (1.0 - (-s * 30.0).exp()) * (0.55 + 0.45 * (-s * 2.5).exp())
            } else {
                0.0
            };
            let trem = tremulant.map_or(0.0, |depth| {
                depth * nominal * (std::f32::consts::TAU * 5.5 * t).sin()
            });
            nominal + attack + trem
        })
        .collect()
}

/// A shoe: a balanced pedal in its slot, the scale beside it.
fn shoe(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    position: f32,
    marks: &[&str],
    title: &str,
    value: &str,
) {
    let slot = Rect::new(
        rect.center().x - 14.0,
        rect.y + 6.0,
        28.0,
        rect.height - 54.0,
    );
    let slot_shape = RoundedRect::new(slot, 6.0);
    scene.fill(slot_shape, Color::hex_rgba(0x0000_0070));
    scene.stroke(slot_shape, Stroke::new(1.0), Color::hex_rgba(0xFFFF_FF14));
    // The scale, open at the top.
    let n = marks.len().max(2);
    for (i, m) in marks.iter().enumerate() {
        let y = slot.bottom() - 8.0 - (slot.height - 16.0) * i as f32 / (n - 1) as f32;
        let mut p = manguebyte_gfx::Path::new();
        p.move_to(Point::new(slot.x - 10.0, y));
        p.line_to(Point::new(slot.x - 4.0, y));
        scene.stroke(&p, Stroke::new(1.0), look.text2.with_alpha(0x90));
        let s = style(9.0, 560.0, look.text2);
        let h = TextLayout::new(m, &s, None).size().height;
        text(scene, m, s, Point::new(slot.x - 13.0, y - h / 2.0), 1.0);
    }
    // The pedal, where it is.
    let y = slot.bottom() - 8.0 - (slot.height - 16.0) * position;
    let pedal = Rect::new(rect.center().x - 24.0, y - 13.0, 48.0, 26.0);
    let shape = RoundedRect::new(pedal, 7.0);
    scene.shadow(
        shape,
        Shadow::new(Color::hex_rgba(0x0000_00B0))
            .with_offset(0.0, 4.0)
            .with_blur(7.0),
    );
    let metal = [
        Stop::new(0.0, look.brass_light),
        Stop::new(0.5, look.brass),
        Stop::new(1.0, look.brass_dark),
    ];
    scene.fill(shape, vertical(pedal, &metal));
    for k in 0..4 {
        let yy = pedal.y + 6.0 + k as f32 * 4.6;
        let mut p = manguebyte_gfx::Path::new();
        p.move_to(Point::new(pedal.x + 9.0, yy));
        p.line_to(Point::new(pedal.right() - 9.0, yy));
        scene.stroke(&p, Stroke::new(1.0), look.brass_dark.with_alpha(0xA0));
    }
    scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x50)));
    text(
        scene,
        title,
        style(12.0, 680.0, look.text),
        Point::new(rect.center().x, rect.bottom() - 40.0),
        0.5,
    );
    text(
        scene,
        value,
        style(10.5, 600.0, look.wood_accent),
        Point::new(rect.center().x, rect.bottom() - 22.0),
        0.5,
    );
}

// ---------------------------------------------------------------- the parts

/// The left jamb: the Grand-Orgue above, the Pédale below.
fn left_jamb(console: Rc<Console>) -> View {
    canvas("Left jamb", Size::new(JAMB, 600.0), move |cx, scene| {
        let look = console.look;
        let rect = Rect::new(0.0, 0.0, cx.size().width, cx.size().height);
        jamb(scene, &look, rect);
        // Five rows of stops share the height.
        let row_h = ((rect.height - 18.0 * 2.0 - 26.0 * 2.0 - 30.0) / 5.0).clamp(96.0, 116.0);
        let mut y = 18.0;
        y = division(
            scene,
            &look,
            rect,
            y,
            "GRAND-ORGUE",
            "I  \u{b7}  ch 1",
            &console.stops_of("Grand-Orgue"),
            4,
            row_h,
        );
        y += 18.0;
        division(
            scene,
            &look,
            rect,
            y,
            "P\u{c9}DALE",
            "ch 3",
            &console.stops_of("Pédale"),
            3,
            row_h,
        );
    })
    .width(JAMB)
    .into_view()
}

/// A division's plate and its stops in rows of `columns`; returns where the
/// next thing goes.
#[allow(clippy::too_many_arguments)]
fn division(
    scene: &mut Scene,
    look: &Look,
    rect: Rect,
    y: f32,
    title: &str,
    note: &str,
    stops: &[(usize, StopFace)],
    columns: usize,
    row_h: f32,
) -> f32 {
    plate(
        scene,
        look,
        Rect::new(rect.x + 22.0, y, rect.width - 44.0, 26.0),
        title,
        note,
    );
    let cell = 88.0;
    let mut y = y + 26.0 + row_h / 2.0 + 6.0;
    for chunk in stops.chunks(columns) {
        let w = cell * chunk.len() as f32;
        let x0 = rect.center().x - w / 2.0 + cell / 2.0;
        for (i, (_, face)) in chunk.iter().enumerate() {
            drawstop(scene, look, Point::new(x0 + cell * i as f32, y), face);
        }
        y += row_h;
    }
    y - row_h / 2.0 - 6.0
}

/// A division's name as a coupler tablet has room for it.
fn short(name: &str) -> &str {
    match name {
        "Grand-Orgue" => "G.-O.",
        "Pédale" => "Péd.",
        "Récit" => "Réc.",
        other => other,
    }
}

/// The right jamb: the Récit, its couplers and tremulant, and the shoes.
fn right_jamb(console: Rc<Console>) -> View {
    canvas("Right jamb", Size::new(JAMB, 600.0), move |cx, scene| {
        let look = console.look;
        let rect = Rect::new(0.0, 0.0, cx.size().width, cx.size().height);
        jamb(scene, &look, rect);
        let mut y = 18.0;
        y = division(
            scene,
            &look,
            rect,
            y,
            "R\u{c9}CIT EXPRESSIF",
            "II  \u{b7}  ch 2",
            &console.stops_of("Récit"),
            4,
            96.0,
        );
        y += 12.0;
        plate(
            scene,
            &look,
            Rect::new(rect.x + 22.0, y, rect.width - 44.0, 26.0),
            "ACCOUPLEMENTS",
            "",
        );
        y += 40.0;
        let mut tablets: Vec<(Vec<String>, bool)> = console
            .organ
            .couplers()
            .iter()
            .zip(&console.couplers)
            .map(|(c, on)| {
                let (from, to) = c.name.split_once('/').unwrap_or((&c.name, ""));
                (vec![format!("{} / {}", short(from), short(to))], *on)
            })
            .collect();
        tablets.push((
            vec!["Tremblant".into(), "R\u{e9}cit".into()],
            console.tremulant,
        ));
        let tw = 78.0;
        let gap = 10.0;
        let total = tablets.len() as f32 * tw + (tablets.len() - 1) as f32 * gap;
        let mut x = rect.center().x - total / 2.0;
        for (lines, on) in &tablets {
            let lines: Vec<&str> = lines.iter().map(String::as_str).collect();
            tablet(scene, &look, Rect::new(x, y, tw, 40.0), &lines, *on);
            x += tw + gap;
        }
        y += 58.0;
        plate(
            scene,
            &look,
            Rect::new(rect.x + 22.0, y, rect.width - 44.0, 26.0),
            "EXPRESSION",
            "",
        );
        y += 34.0;
        let shoes_h = rect.bottom() - y - 10.0;
        shoe(
            scene,
            &look,
            Rect::new(rect.x + 60.0, y, 120.0, shoes_h),
            0.62,
            &["0", "", "", "", "8"],
            "R\u{e9}cit",
            "box 62 % open",
        );
        let tags: Vec<&str> = FACTORY_GENERALS.iter().map(|g| g.tag).collect();
        let mut marks = vec![""];
        marks.extend(tags.iter().copied());
        shoe(
            scene,
            &look,
            Rect::new(rect.right() - 180.0, y, 120.0, shoes_h),
            4.0 / 8.0,
            &marks,
            "Crescendo",
            "step 4 \u{b7} mf",
        );
    })
    .width(JAMB)
    .into_view()
}

/// The registration: a selector line, the history, why.
fn registration(console: Rc<Console>) -> View {
    canvas("Registration", Size::new(560.0, 222.0), move |cx, scene| {
        let look = console.look;
        let rect = Rect::new(0.0, 0.0, cx.size().width, cx.size().height);
        panel(scene, &look, rect);
        let drawn = console.drawn.iter().filter(|d| **d).count();
        heading(
            scene,
            &look,
            rect,
            "Registration",
            &format!("{drawn} stops drawn \u{b7} General 4 (mf) + 3 by hand"),
        );
        // The selector line.
        let field = Rect::new(rect.x + 20.0, rect.y + 46.0, rect.width - 40.0, 34.0);
        let shape = RoundedRect::new(field, 8.0);
        scene.fill(
            shape,
            if look.night {
                Color::hex_rgba(0x0000_0066)
            } else {
                Color::hex(0xFFFFFF)
            },
        );
        scene.stroke(shape, Stroke::new(1.2), look.accent.with_alpha(0xB0));
        let y = field.y + 9.0;
        let mut x = field.x + 14.0;
        x += text(
            scene,
            ">",
            style(12.5, 700.0, look.accent),
            Point::new(x, y),
            0.0,
        )
        .width
            + 10.0;
        for (piece, color, gap) in [
            ("div:", look.muted, 0.0),
            ("recit", look.text, 6.0),
            ("&", look.accent, 6.0),
            ("family:", look.muted, 0.0),
            ("string", look.text, 0.0),
        ] {
            x += text(
                scene,
                piece,
                style(12.5, 520.0, color),
                Point::new(x, y),
                0.0,
            )
            .width
                + gap;
        }
        scene.fill(Rect::new(x + 2.0, y - 1.0, 1.5, 17.0), look.accent);
        text(
            scene,
            "Enter draws \u{b7} Shift+Enter retires",
            style(10.5, 450.0, look.muted),
            Point::new(field.right() - 12.0, y + 1.5),
            1.0,
        );
        // The history.
        let y = field.bottom() + 18.0;
        text(
            scene,
            "History",
            style(10.5, 600.0, look.muted),
            Point::new(rect.x + 20.0, y + 5.0),
            0.0,
        );
        let nodes = [
            ("Opening", false),
            ("General 3 \u{b7} mp", false),
            ("General 4 \u{b7} mf", false),
            ("+ Hautbois", false),
            ("+ Gambe, C\u{e9}leste", true),
        ];
        let chip = style(10.5, 600.0, look.text);
        let mut x = rect.x + 80.0;
        let mut previous: Option<f32> = None;
        for (label, current) in nodes {
            let w = width_of(label, chip) + 18.0;
            let r = Rect::new(x, y, w, 24.0);
            if let Some(px) = previous {
                let mut p = manguebyte_gfx::Path::new();
                p.move_to(Point::new(px, r.center().y));
                p.line_to(Point::new(r.x, r.center().y));
                scene.stroke(&p, Stroke::new(1.2), look.edge);
            }
            let shape = RoundedRect::new(r, 12.0);
            if current {
                scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x70)));
                scene.fill(shape, look.accent.with_alpha(0x33));
                scene.stroke(shape, Stroke::new(1.2), look.accent);
            } else {
                scene.fill(shape, look.edge.with_alpha(0x80));
            }
            text(
                scene,
                label,
                style(10.5, 600.0, if current { look.text } else { look.text2 }),
                Point::new(r.x + 9.0, r.y + 5.0),
                0.0,
            );
            previous = Some(r.right());
            x = r.right() + 12.0;
        }
        // Why.
        let mut y = y + 40.0;
        text(
            scene,
            "Why",
            style(10.5, 600.0, look.muted),
            Point::new(rect.x + 20.0, y),
            0.0,
        );
        for (dot, lead, rest) in [
            (
                look.accent,
                "Voix C\u{e9}leste 8",
                " drawn by div:recit & family:string, after General 4 (mf)",
            ),
            (
                look.muted,
                "Tierce 1 3/5",
                " not drawn: General 4 (mf) leaves it to General 5 (f)",
            ),
        ] {
            scene.fill(Circle::new(Point::new(rect.x + 86.0, y + 7.0), 3.5), dot);
            let w = text(
                scene,
                lead,
                style(11.5, 650.0, look.text),
                Point::new(rect.x + 96.0, y),
                0.0,
            )
            .width;
            text(
                scene,
                rest,
                style(11.5, 450.0, look.text2),
                Point::new(rect.x + 96.0 + w, y),
                0.0,
            );
            y += 22.0;
        }
    })
    .height(222.0)
    .into_view()
}

/// The wind: a gauge for each chest.
fn wind(console: Rc<Console>) -> View {
    canvas("Wind", Size::new(560.0, 240.0), move |cx, scene| {
        let look = console.look;
        let rect = Rect::new(0.0, 0.0, cx.size().width, cx.size().height);
        panel(scene, &look, rect);
        heading(
            scene,
            &look,
            rect,
            "Wind",
            "three chests \u{b7} millimetres of water, live",
        );
        let mm = |pa: f32| pa / 9.806_65;
        let chests = [
            (
                "P\u{e9}dale",
                mm(980.0),
                wind_trace(mm(980.0), 0.0, None),
                "steady",
            ),
            (
                "Grand-Orgue",
                mm(812.0),
                wind_trace(mm(812.0), 0.029, None),
                "\u{2212}2.4 % under the chord",
            ),
            (
                "R\u{e9}cit",
                mm(735.0),
                wind_trace(mm(735.0), 0.012, Some(0.022)),
                "tremblant 5.5 Hz \u{b7} \u{b1}2.2 %",
            ),
        ];
        let w = rect.width / 3.0;
        for (i, (name, nominal, trace, note)) in chests.iter().enumerate() {
            let c = Point::new(rect.x + w * (i as f32 + 0.5), rect.y + 112.0);
            let now = trace[trace.len() - 9];
            gauge(scene, &look, c, name, *nominal, now, trace, note);
        }
    })
    .flex(1.0)
    .into_view()
}

/// The general pistons, the sequencer, set and cancel.
fn pistons(console: Rc<Console>) -> View {
    canvas("Generals", Size::new(560.0, 190.0), move |cx, scene| {
        let look = console.look;
        let rect = Rect::new(0.0, 0.0, cx.size().width, cx.size().height);
        panel(scene, &look, rect);
        heading(
            scene,
            &look,
            rect,
            "Generals",
            "program change 1\u{2013}16 \u{b7} Set + a piston captures",
        );
        let x0 = rect.x + 40.0;
        let step = 42.0;
        let organists_set = [8usize, 9, 11];
        for row in 0..2 {
            let y = rect.y + 74.0 + row as f32 * 62.0;
            for k in 0..8 {
                let n = row * 8 + k;
                let c = Point::new(x0 + k as f32 * step, y);
                let kind = if row == 0 {
                    PistonKind::Ivory
                } else {
                    PistonKind::Ebony
                };
                piston(
                    scene,
                    &look,
                    c,
                    &format!("{}", n + 1),
                    kind,
                    n == console.general,
                );
                let tag = if row == 0 {
                    FACTORY_GENERALS
                        .get(k)
                        .map_or_else(String::new, |g| g.tag.to_string())
                } else if organists_set.contains(&n) {
                    "set".into()
                } else {
                    "empty".into()
                };
                let color = if n == console.general {
                    look.accent
                } else if row == 0 || organists_set.contains(&n) {
                    look.text2
                } else {
                    look.muted.with_alpha(0x90)
                };
                text(
                    scene,
                    &tag,
                    style(10.0, 600.0, color),
                    Point::new(c.x, y + 23.0),
                    0.5,
                );
            }
        }
        // The sequencer: where it is, and its two pistons.
        let sx = x0 + 8.0 * step + 6.0;
        let display = Rect::new(sx, rect.y + 52.0, rect.right() - sx - 20.0, 52.0);
        let shape = RoundedRect::new(display, 8.0);
        scene.fill(shape, Color::hex(0x0B0806));
        scene.stroke(shape, Stroke::new(1.0), look.edge);
        let lcd = Color::hex(0xF2B95A);
        scene.shadow(shape, Shadow::glow(8.0, lcd.with_alpha(0x18)));
        text(
            scene,
            "4",
            style(24.0, 700.0, lcd),
            Point::new(display.x + 16.0, display.y + 10.0),
            0.0,
        );
        text(
            scene,
            "mf",
            style(12.0, 650.0, lcd),
            Point::new(display.x + 44.0, display.y + 11.0),
            0.0,
        );
        text(
            scene,
            "of 16  \u{b7}  next: f",
            style(10.0, 500.0, lcd.with_alpha(0xA0)),
            Point::new(display.x + 44.0, display.y + 28.0),
            0.0,
        );
        let y = rect.y + 136.0;
        let cx0 = display.x + 22.0;
        let gap = (display.width - 44.0) / 3.0;
        for (i, (label, kind)) in [
            ("", PistonKind::Ebony),
            ("", PistonKind::Ebony),
            ("0", PistonKind::Ivory),
            ("Set", PistonKind::Set),
        ]
        .iter()
        .enumerate()
        {
            let c = Point::new(cx0 + gap * i as f32, y);
            piston(scene, &look, c, label, *kind, false);
            if i < 2 {
                arrow(scene, c, i == 1, look.ivory);
            }
            let under = ["previous", "next", "cancel", "capture"][i];
            text(
                scene,
                under,
                style(9.5, 550.0, look.muted),
                Point::new(c.x, y + 23.0),
                0.5,
            );
        }
    })
    .height(190.0)
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
            // The mouth.
            scene.fill(
                Rect::new(x + 1.2, 33.0 - h * 0.28, 2.6, 1.6),
                look.ground.with_alpha(0xC0),
            );
        }
    })
    .width(34.0)
    .height(34.0)
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

/// Under the keyboard: the division it plays.
fn manual_plate(look: Look) -> View {
    canvas("Manual", Size::new(150.0, 86.0), move |cx, scene| {
        let size = cx.size();
        plate(
            scene,
            &look,
            Rect::new(0.0, size.height / 2.0 - 15.0, size.width, 30.0),
            "I  G.-O.",
            "",
        );
        text(
            scene,
            "+ R\u{e9}cit coupled",
            style(10.0, 550.0, look.accent),
            Point::new(size.width / 2.0, size.height / 2.0 + 20.0),
            0.5,
        );
    })
    .width(150.0)
}

/// The console.
fn screen(console: &Rc<Console>, keys: &KeyboardState) -> View {
    let look = console.look;
    let top = row((
        logo(look),
        column((
            label("Caecilia").title(),
            label(format!(
                "{} \u{b7} {} divisions \u{b7} {} stops",
                console.organ.name,
                console.organ.divisions().len(),
                console.organ.stops().len()
            ))
            .caption(),
        )),
        canvas("", Size::new(0.0, 0.0), |_, _| {}).flex(1.0),
        pill("Werckmeister III \u{b7} A 415").dot(look.accent),
        pill("Wind steady").dot(look.good),
        pill("CPU 11 % \u{b7} 214 voices"),
        pill("Undo"),
        pill("Redo"),
        pill("Organ\u{2026}").primary(),
    ))
    .spacing(12.0)
    .align(Align::Center);
    let middle = column((
        registration(Rc::clone(console)),
        wind(Rc::clone(console)),
        pistons(Rc::clone(console)),
    ))
    .spacing(14.0)
    .flex(1.0);
    let body = row((
        left_jamb(Rc::clone(console)),
        middle,
        right_jamb(Rc::clone(console)),
    ))
    .spacing(14.0)
    .flex(1.0);
    let keyboard = row((
        manual_plate(look),
        piano_keyboard("Grand-Orgue", KeySpan::new(36, 96), keys.clone())
            .height(86.0)
            .flex(1.0),
    ))
    .spacing(14.0)
    .align(Align::Center);
    stack((
        backdrop(look),
        column((top, body, keyboard)).spacing(14.0).padding(20.0),
    ))
}

fn save(root: &Root, folder: &Path, name: &str) -> std::io::Result<PathBuf> {
    let path = folder.join(name);
    let png = root
        .pixels()
        .encode_png()
        .map_err(|e| std::io::Error::other(format!("{e:?}")))?;
    std::fs::write(&path, png)?;
    Ok(path)
}

fn main() -> std::io::Result<()> {
    let folder = std::env::args_os()
        .nth(1)
        .map_or_else(std::env::temp_dir, PathBuf::from);
    for (look, name) in [(NIGHT, "night"), (DAY, "day")] {
        for (scale, suffix) in [(1.0, "@1x"), (2.0, "@2x")] {
            let console = Console::new(look);
            let keys = KeyboardState::new();
            keys.set_sounding([48u8, 55, 60, 64, 67]);
            let host = Rc::new(RecordingEditorHost::new());
            let (context, _link) = EditorContext::attach(host);
            let mut root =
                Root::new(screen(&console, &keys), SIZE, scale, context).with_theme(theme(&look));
            let mut clock = Duration::ZERO;
            for _ in 0..60 {
                clock += Duration::from_millis(16);
                root.frame(clock);
                if !root.is_animating() {
                    break;
                }
            }
            let path = save(&root, &folder, &format!("console-{name}{suffix}.png"))?;
            println!("wrote {}", path.display());
        }
    }
    Ok(())
}
