// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The console at half a screen, beside a score open in another program. The
//! keyboards are not drawn, though they go on playing from the MIDI keyboard:
//! what's left is what an organist reaches for while reading, every stop of
//! every division as a press, the couplers, the pistons and the expression.
//!
//! It shows the same reading and does the same things as the full console. Only
//! the window chooses between them, at its real size here where the full console
//! is zoomed to fit, so nothing here needs to know of the other.

use std::rc::Rc;

use caecilia_model::TonalFamily;
use manguebyte_gfx::{Color, Point, Rect, RoundedRect, Scene, Size, Stroke, Transform};
use manguebyte_ui::access::{AccessNode, Role};
use manguebyte_ui::layout::{Align, Style};
use manguebyte_ui::{
    AccessCx, Children, Handled, IntoView, MeasureCx, PaintCx, Shown, UpdateCx, View, Widget,
    column, row, stack,
};

use super::controls::{
    JambControl, SlipControl, StatusButton, coupler_label, crescendo_reading, draw_slip_control,
    expression_input, jamb_press, slip_act, slip_name, status_button, status_label, status_open,
    status_press,
};
use super::live::{Item, live};
use super::paint::{panel, style, text, width_of};
use super::parts;
use super::snapshot::{Section, Snapshot, Source, bare_name, describe_stops, hash_of};
use super::sound;

/// Below this window the console gives way to this one, at its real size.
pub(crate) const BELOW: Size = Size::new(1100.0, 700.0);

/// Below this height, as at half of a small screen, the rows around the stops
/// give up what they can.
pub(crate) const SHORT_BELOW: Size = Size::new(0.0, 720.0);

/// Inside a card, either side of its stops.
const INSET: f32 = 10.0;
/// A card's heading, and what's above and below its stops; less of each once
/// the desk has to get smaller.
const HEADING: f32 = 22.0;
const CARD_EDGES: f32 = 8.0;
const TIGHT_HEADING: f32 = 17.0;
const TIGHT_EDGES: f32 = 4.0;
/// Between stops, and at the least between cards.
const GAP: f32 = 5.0;
const CARD_GAP: f32 = 8.0;
/// A stop is at least this wide, which holds its name.
const STOP_WIDTH: f32 = 112.0;
/// How tall a row of stops is: this much in a short window, this in a tall one.
const ROW_LEAST: f32 = 28.0;
const ROW_MOST: f32 = 42.0;
/// How small the desk gets, past what its rows can give up, in a very short
/// window. Small, but better than a division out of reach.
const SMALLEST: f32 = 0.5;

struct Card {
    rect: Rect,
    title: String,
    note: String,
}

struct Press {
    control: JambControl,
    rect: Rect,
    name: String,
    reed: bool,
}

/// Where everything on the desk goes, before it is scaled by `k` to fit.
struct DeskPlan {
    k: f32,
    fits: bool,
    cards: Vec<Card>,
    presses: Vec<Press>,
}

/// A card to lay out: its title, its note, and its controls with their names
/// and whether each is a reed.
type Listed = (String, String, Vec<(JambControl, String, bool)>);

/// The divisions as the organist counts them: from the first manual up, then
/// the pedal.
fn in_order(snapshot: &Snapshot) -> Vec<&Section> {
    let layout = &snapshot.layout;
    let manual_first = usize::from(!layout.manuals.is_empty()).min(layout.left.len());
    let (first, pedal) = layout.left.split_at(manual_first);
    first.iter().chain(&layout.right).chain(pedal).collect()
}

/// The couplers and the tremulant, as the desk's last card has them.
fn couplers_of(snapshot: &Snapshot) -> Vec<(JambControl, String)> {
    let mut list: Vec<(JambControl, String)> = snapshot
        .organ
        .couplers()
        .iter()
        .enumerate()
        .map(|(n, c)| (JambControl::Coupler(n), coupler_label(&c.name)))
        .collect();
    if snapshot.layout.tremulant {
        list.push((JambControl::Tremulant, "Tremulant".into()));
    }
    list
}

fn desk_at(size: Size, snapshot: &Snapshot, k: f32) -> DeskPlan {
    let room = Size::new(size.width / k, size.height / k);
    let (heading, edges) = if k < 1.0 {
        (TIGHT_HEADING, TIGHT_EDGES)
    } else {
        (HEADING, CARD_EDGES)
    };
    let inner = room.width - 2.0 * INSET;
    let columns = (((inner + GAP) / (STOP_WIDTH + GAP)).floor() as usize).max(3);
    let width = (inner - GAP * (columns - 1) as f32) / columns as f32;

    let organ = &snapshot.organ;
    let mut lists: Vec<Listed> = in_order(snapshot)
        .into_iter()
        .map(|section| {
            let stops = section
                .stops
                .iter()
                .filter_map(|&n| {
                    let stop = organ.stops().get(n)?;
                    Some((
                        JambControl::Stop(n),
                        stop.name.clone(),
                        stop.family == TonalFamily::Reed,
                    ))
                })
                .collect();
            (section.name.clone(), section.note.clone(), stops)
        })
        .collect();
    let couplers = couplers_of(snapshot);
    if !couplers.is_empty() {
        lists.push((
            "Couplers".into(),
            snapshot.route.clone().unwrap_or_default(),
            couplers.into_iter().map(|(c, n)| (c, n, false)).collect(),
        ));
    }

    let rows_of = |count: usize| count.div_ceil(columns).max(1);
    let rows: usize = lists.iter().map(|l| rows_of(l.2.len())).sum();
    let around: f32 = lists
        .iter()
        .map(|l| 2.0 * edges + heading + GAP * (rows_of(l.2.len()) - 1) as f32)
        .sum();
    let gaps = CARD_GAP * lists.len().saturating_sub(1) as f32;
    let wanted = (room.height - around - gaps) / rows.max(1) as f32;
    let row = wanted.clamp(ROW_LEAST, ROW_MOST);
    // What a row can't use, in a tall window, goes between the cards.
    let spare = (room.height - around - gaps - row * rows as f32).max(0.0);
    let gap = CARD_GAP + (spare / lists.len().saturating_sub(1).max(1) as f32).min(16.0);

    let mut plan = DeskPlan {
        k,
        fits: wanted >= ROW_LEAST,
        cards: Vec::new(),
        presses: Vec::new(),
    };
    let mut y = 0.0;
    for (title, note, controls) in lists {
        let lines = rows_of(controls.len());
        let height = 2.0 * edges + heading + row * lines as f32 + GAP * (lines - 1) as f32;
        plan.cards.push(Card {
            rect: Rect::new(0.0, y, room.width, height),
            title,
            note,
        });
        for (i, (control, name, reed)) in controls.into_iter().enumerate() {
            let (line, place) = (i / columns, i % columns);
            plan.presses.push(Press {
                control,
                rect: Rect::new(
                    INSET + place as f32 * (width + GAP),
                    y + edges + heading + line as f32 * (row + GAP),
                    width,
                    row,
                ),
                name,
                reed,
            });
        }
        y += height + gap;
    }
    plan
}

fn desk_plan(size: Size, snapshot: &Snapshot) -> DeskPlan {
    let mut k = 1.0;
    loop {
        let plan = desk_at(size, snapshot, k);
        if plan.fits || k <= SMALLEST {
            return plan;
        }
        k -= 0.05;
    }
}

/// A stop's name in `width`: the longest word shortened, never its pitch, and
/// no more than it takes. "Basson-Hautb. 8" for "Basson-Hautbois 8".
fn shortened(name: &str, s: manguebyte_gfx::TextStyle, width: f32) -> String {
    if width_of(name, s) <= width {
        return name.to_string();
    }
    let bare = bare_name(name);
    let pitch = name[bare.len()..].to_string();
    let mut words: Vec<String> = bare.split_whitespace().map(str::to_string).collect();
    loop {
        // The longest word that still has letters to give.
        let Some(longest) = (0..words.len())
            .filter(|&w| words[w].trim_end_matches('.').chars().count() > 3)
            .max_by_key(|&w| words[w].trim_end_matches('.').chars().count())
        else {
            return format!("{}{pitch}", words.join(" "));
        };
        let kept = words[longest].trim_end_matches('.');
        let fewer: String = kept.chars().take(kept.chars().count() - 1).collect();
        words[longest] = format!("{}.", fewer.trim_end_matches('-'));
        let out = format!("{}{pitch}", words.join(" "));
        if width_of(&out, s) <= width {
            return out;
        }
    }
}

fn draw_press(scene: &mut Scene, snapshot: &Snapshot, press: &Press, on: bool) {
    let look = snapshot.look;
    let r = press.rect;
    let shape = RoundedRect::new(r, 6.0);
    let ink = if on {
        scene.fill(shape, look.brass_light);
        if press.reed {
            look.reed_ink
        } else {
            look.plate_ink
        }
    } else {
        let (face, reed) = if look.night {
            (Color::hex(0x2F2315), Color::hex(0xEF8F7E))
        } else {
            (Color::hex(0xFBF6EC), look.reed_ink)
        };
        scene.fill(shape, face);
        scene.stroke(shape, Stroke::new(1.0), look.edge);
        if press.reed { reed } else { look.text }
    };
    let s = style(11.5, if on { 650.0 } else { 500.0 }, ink);
    let name = shortened(&press.name, s, r.width - 8.0);
    text(
        scene,
        &name,
        s,
        Point::new(r.center().x, r.center().y - 7.5),
        0.5,
    );
}

fn is_on(snapshot: &Snapshot, control: JambControl) -> bool {
    match control {
        JambControl::Stop(n) => snapshot.drawn.get(n).copied().unwrap_or(false),
        JambControl::Coupler(n) => snapshot.couplers.get(n).copied().unwrap_or(false),
        JambControl::Tremulant => snapshot.tremulant,
    }
}

fn desk_items(snapshot: &Snapshot, size: Size) -> Vec<Item> {
    let plan = desk_plan(size, snapshot);
    let organ = &snapshot.organ;
    plan.presses
        .iter()
        .map(|press| {
            let name = match press.control {
                JambControl::Stop(n) => {
                    let stop = organ.stops().get(n);
                    let division = stop
                        .and_then(|s| organ.division(s.division))
                        .map_or("", |d| d.name.as_str());
                    format!("{division} {}", press.name)
                }
                JambControl::Coupler(n) => organ
                    .couplers()
                    .get(n)
                    .map_or_else(String::new, |c| c.name.clone()),
                JambControl::Tremulant => press.name.clone(),
            };
            let r = press.rect;
            Item {
                role: Role::Switch,
                name,
                checked: Some(is_on(snapshot, press.control)),
                rect: Rect::new(
                    r.x * plan.k,
                    r.y * plan.k,
                    r.width * plan.k,
                    r.height * plan.k,
                ),
            }
        })
        .collect()
}

/// Every stop of every division, then the couplers. A press draws or retires.
fn stops(source: &Rc<Source>) -> View {
    let here = Rc::clone(source);
    live(
        "Stops",
        Size::new(400.0, 200.0),
        source,
        |s| s.stops_key() ^ hash_of(|h| std::hash::Hash::hash(&s.route, h)),
        |size, scene, snapshot| {
            let look = snapshot.look;
            let plan = desk_plan(size, snapshot);
            scene.transformed(Transform::scale(plan.k, plan.k), |scene| {
                for card in &plan.cards {
                    // Tight, with no room for the panel's inner line.
                    let y = if plan.k < 1.0 {
                        let shape = RoundedRect::new(card.rect, 8.0);
                        scene.fill(shape, look.panel_top);
                        scene.stroke(shape, Stroke::new(1.0), look.edge);
                        card.rect.y + 3.0
                    } else {
                        panel(scene, &look, card.rect);
                        card.rect.y + CARD_EDGES
                    };
                    let title = style(12.5, 650.0, look.accent);
                    text(
                        scene,
                        &card.title,
                        title,
                        Point::new(card.rect.x + INSET + 2.0, y),
                        0.0,
                    );
                    text(
                        scene,
                        &card.note,
                        style(11.0, 450.0, look.text2),
                        Point::new(card.rect.right() - INSET - 2.0, y + 1.0),
                        1.0,
                    );
                }
                for press in &plan.presses {
                    draw_press(scene, snapshot, press, is_on(snapshot, press.control));
                }
            });
        },
    )
    .describe(|s| {
        let mut said = describe_stops(s, &s.layout.left, false);
        said.push_str("; ");
        said.push_str(&describe_stops(s, &s.layout.right, true));
        said
    })
    .items(desk_items)
    .input(move |cx, event, at, size, snapshot| {
        if !super::controls::pressed(event) {
            return Handled::No;
        }
        let plan = desk_plan(size, snapshot);
        let local = Point::new(at.x / plan.k, at.y / plan.k);
        let Some(press) = plan.presses.iter().find(|p| p.rect.contains(local)) else {
            return Handled::No;
        };
        jamb_press(cx, &here, press.control);
        Handled::Yes
    })
    .flex(1.0)
    .into_view()
}

/// Shows the stops, or the panel a status button opened in their place.
struct Desk(Rc<Source>);

impl Widget for Desk {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        AccessNode::new(Role::Group, "")
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        Size::default()
    }

    fn paint(&mut self, _cx: &PaintCx<'_>, _scene: &mut Scene) {}

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        let s = self.0.get(cx.now());
        let open =
            s.library.is_some() || s.demos.is_some() || s.models.is_some() || s.sound.is_some();
        cx.show(Shown::Only(usize::from(open)));
    }
}

fn desk(source: &Rc<Source>) -> View {
    let children: Vec<View> = vec![stops(source), parts::wind(source)];
    View::with_children(
        Desk(Rc::clone(source)),
        Style::stack(),
        Children::into_children(children),
    )
    .flex(1.0)
    .into_view()
}

// ---------------------------------------------------------------- now

/// The buttons the full console keeps in its status bar and this one in
/// Settings, shown while Settings or what they open is showing.
fn now_buttons(size: Size, snapshot: &Snapshot) -> Vec<(StatusButton, Rect)> {
    if snapshot.sound.is_none() && snapshot.demos.is_none() {
        return Vec::new();
    }
    let s = style(12.5, 600.0, snapshot.look.text);
    let y = (size.height - 30.0) / 2.0;
    let mut x = size.width - INSET;
    let mut buttons = Vec::new();
    for button in [StatusButton::Demos, StatusButton::Light] {
        let w = width_of(status_label(button, snapshot), s) + 32.0;
        x -= w;
        buttons.push((button, Rect::new(x, y, w, 30.0)));
        x -= 10.0;
    }
    buttons
}

/// What's drawn now, and the two models the sustain pedal alternates.
fn now(source: &Rc<Source>, height: f32) -> View {
    let here = Rc::clone(source);
    live(
        "Now",
        Size::new(400.0, height),
        source,
        |s| {
            s.registration_key()
                ^ hash_of(|h| {
                    use std::hash::Hash;
                    (&s.pedal, s.sound.is_some(), s.demos.is_some()).hash(h);
                })
        },
        |size, scene, snapshot| {
            let look = snapshot.look;
            panel(scene, &look, Rect::new(0.0, 0.0, size.width, size.height));
            let top = (size.height - 32.0) / 2.0;
            let buttons = now_buttons(size, snapshot);
            let mut room = size.width - 2.0 * INSET - 2.0;
            for (button, r) in &buttons {
                room -= r.width + 10.0;
                status_button(
                    scene,
                    &look,
                    *r,
                    status_label(*button, snapshot),
                    status_open(*button, snapshot) == Some(true),
                );
            }
            if let (Some(pedal), true) = (&snapshot.pedal, buttons.is_empty()) {
                let right = size.width - INSET - 2.0;
                let muted = style(11.0, 450.0, look.text2);
                text(scene, "Sustain pedal", muted, Point::new(right, top), 1.0);
                let line = |on: bool| {
                    style(
                        12.0,
                        if on { 650.0 } else { 450.0 },
                        if on { look.accent } else { look.text },
                    )
                };
                let second = format!("2 {}", pedal.second);
                let first = format!("1 {}", pedal.first);
                let (s2, s1) = (line(pedal.on == Some(1)), line(pedal.on == Some(0)));
                let width =
                    width_of(&first, s1) + width_of("  \u{b7}  ", s2) + width_of(&second, s2);
                if width < room * 0.62 {
                    let mut x = right - width;
                    text(scene, &first, s1, Point::new(x, top + 16.0), 0.0);
                    x += width_of(&first, s1);
                    text(scene, "  \u{b7}  ", muted, Point::new(x, top + 16.0), 0.0);
                    x += width_of("  \u{b7}  ", s2);
                    text(scene, &second, s2, Point::new(x, top + 16.0), 0.0);
                    room -= width + 16.0;
                }
            }
            text(
                scene,
                "Now",
                style(11.0, 450.0, look.text2),
                Point::new(INSET + 2.0, top),
                0.0,
            );
            let here = snapshot
                .history
                .iter()
                .find(|step| step.distance == 0)
                .map(|step| step.label.as_str());
            let line = match here {
                Some(here) => format!("{here} \u{b7} {}", snapshot.summary),
                None => snapshot.summary.clone(),
            };
            let s = style(13.5, 600.0, look.text);
            let shown = if width_of(&line, s) <= room {
                line
            } else {
                sound::fit(&snapshot.summary, s, room)
            };
            text(scene, &shown, s, Point::new(INSET + 2.0, top + 14.0), 0.0);
        },
    )
    .describe(|s| match &s.pedal {
        Some(pedal) => format!(
            "{}; the sustain pedal alternates {} and {}",
            s.summary, pedal.first, pedal.second
        ),
        None => s.summary.clone(),
    })
    .items(|s, size| {
        now_buttons(size, s)
            .into_iter()
            .map(|(button, rect)| Item {
                role: Role::Button,
                name: status_label(button, s).to_string(),
                checked: status_open(button, s),
                rect,
            })
            .collect()
    })
    .input(move |cx, event, at, size, snapshot| {
        if !super::controls::pressed(event) {
            return Handled::No;
        }
        let buttons = now_buttons(size, snapshot);
        let Some((button, _)) = buttons.iter().find(|(_, r)| r.contains(at)) else {
            return Handled::No;
        };
        status_press(cx, &here, *button);
        Handled::Yes
    })
    .height(height)
    .into_view()
}

// ---------------------------------------------------------------- pistons

/// How tall the pistons are, in their two rows or in one.
const PISTONS: f32 = 104.0;
const PISTONS_SHORT: f32 = 58.0;
/// The ivory generals' size, and everything else's.
const LARGE: f32 = 14.0;
const SMALL: f32 = 11.0;
const LARGE_STEP: f32 = 44.0;
const SMALL_STEP: f32 = 33.0;
/// One row holds the upper generals too from this wide.
const ONE_ROW_OF_ALL: f32 = 660.0;

/// One row of pistons, centred at `y`, with a space after each `after`.
fn one_row(size: Size, y: f32, controls: &[SlipControl]) -> Vec<(SlipControl, Point, f32)> {
    let space = |c: SlipControl| match c {
        SlipControl::General(n) if n % 8 == 7 => 14.0,
        SlipControl::Next => 14.0,
        _ => 0.0,
    };
    let total: f32 = controls.iter().map(|c| SMALL_STEP + space(*c)).sum();
    let mut x = (size.width - total) / 2.0 + SMALL_STEP / 2.0;
    controls
        .iter()
        .map(|c| {
            let at = (*c, Point::new(x, y), SMALL);
            x += SMALL_STEP + space(*c);
            at
        })
        .collect()
}

/// Where the pistons go, each with its size: the first eight generals in a row
/// of their own, the rest with the sequencer, cancel and set under them; or, in
/// a short window, all in one row, as many of the generals as it holds.
fn piston_plan(size: Size) -> Vec<(SlipControl, Point, f32)> {
    const REST: [SlipControl; 4] = [
        SlipControl::Previous,
        SlipControl::Next,
        SlipControl::Cancel,
        SlipControl::Set,
    ];
    if size.height < PISTONS {
        let generals = if size.width >= ONE_ROW_OF_ALL {
            crate::console::GENERALS
        } else {
            8
        };
        let list: Vec<SlipControl> = (0..generals)
            .map(SlipControl::General)
            .chain(REST)
            .collect();
        return one_row(size, 21.0, &list);
    }
    let mut plan = Vec::new();
    let middle = size.width / 2.0;
    let mut x = middle - 3.5 * LARGE_STEP;
    for n in 0..8 {
        plan.push((SlipControl::General(n), Point::new(x, 23.0), LARGE));
        x += LARGE_STEP;
    }
    let list: Vec<SlipControl> = (8..crate::console::GENERALS)
        .map(SlipControl::General)
        .chain(REST)
        .collect();
    plan.extend(one_row(size, 72.0, &list));
    plan
}

fn pistons(source: &Rc<Source>, height: f32) -> View {
    let here = Rc::clone(source);
    live(
        "Pistons",
        Size::new(400.0, height),
        source,
        |s| hash_of(|h| std::hash::Hash::hash(&(s.general, s.set_armed, s.generals_set), h)),
        |size, scene, snapshot| {
            panel(
                scene,
                &snapshot.look,
                Rect::new(0.0, 0.0, size.width, size.height),
            );
            for (control, c, r) in piston_plan(size) {
                draw_slip_control(scene, snapshot, control, c, r);
            }
        },
    )
    .describe(super::controls::describe_slip)
    .items(|_, size| {
        piston_plan(size)
            .into_iter()
            .map(|(control, c, r)| Item {
                role: Role::Button,
                name: slip_name(control),
                checked: None,
                rect: Rect::new(c.x - r - 3.0, c.y - r - 3.0, 2.0 * r + 6.0, 2.0 * r + 6.0),
            })
            .collect()
    })
    .input(move |cx, event, at, size, _snapshot| {
        if !super::controls::pressed(event) {
            return Handled::No;
        }
        let plan = piston_plan(size);
        let Some((control, _, _)) = plan.iter().find(|(_, c, r)| {
            let (dx, dy) = (c.x - at.x, c.y - at.y);
            dx * dx + dy * dy <= (r + 3.0) * (r + 3.0)
        }) else {
            return Handled::No;
        };
        slip_act(cx, &here, *control);
        Handled::Yes
    })
    .height(height)
    .into_view()
}

// ---------------------------------------------------------------- expression

/// How tall the expression is, and in a short window.
const EXPRESSION: f32 = 46.0;
const EXPRESSION_SHORT: f32 = 40.0;

/// Where the swell and the crescendo are moved along.
fn expression_rects(size: Size) -> [Rect; 2] {
    let w = (size.width - 2.0 * INSET - 2.0 - 24.0) / 2.0;
    [0.0, 1.0].map(|i| Rect::new(INSET + 1.0 + i * (w + 24.0), 6.0, w, size.height - 10.0))
}

/// The swell and the crescendo, as bars rather than shoes.
fn expression(source: &Rc<Source>, height: f32) -> View {
    let shared = source.shared().clone();
    live(
        "Expression",
        Size::new(400.0, height),
        source,
        Snapshot::shoes_key,
        |size, scene, snapshot| {
            let look = snapshot.look;
            panel(scene, &look, Rect::new(0.0, 0.0, size.width, size.height));
            let swell = snapshot.swell.clamp(0.0, 1.0);
            let (step, reading) = crescendo_reading(snapshot);
            let name = if snapshot.layout.swell == "Swell" {
                "Swell".to_string()
            } else {
                format!("{} swell", snapshot.layout.swell)
            };
            let bars = [
                (name, format!("{:.0} %", swell * 100.0), swell),
                ("Crescendo".to_string(), reading, step as f32 / 8.0),
            ];
            let muted = style(11.0, 450.0, look.text2);
            for (r, (name, value, at)) in expression_rects(size).iter().zip(&bars) {
                text(scene, name, muted, Point::new(r.x, r.y), 0.0);
                text(scene, value, muted, Point::new(r.right(), r.y), 1.0);
                let track = Rect::new(r.x, r.bottom() - 11.0, r.width, 8.0);
                let shape = RoundedRect::new(track, 4.0);
                scene.fill(shape, look.edge.with_alpha(0x80));
                if *at > 0.0 {
                    let filled = Rect::new(track.x, track.y, track.width * at, track.height);
                    scene.fill(RoundedRect::new(filled, 4.0), look.accent);
                }
            }
        },
    )
    .items(|s, size| {
        let [a, b] = expression_rects(size);
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
    .input(expression_input(
        shared,
        expression_rects,
        |r| r,
        |r, at| ((at.x - r.x) / r.width).clamp(0.0, 1.0),
    ))
    .height(height)
    .into_view()
}

// ---------------------------------------------------------------- screen

/// The console at half a screen; in a window short of even that, `short`.
pub(crate) fn screen(source: &Rc<Source>, short: bool) -> View {
    let (now_height, pistons_height, expression_height) = if short {
        (40.0, PISTONS_SHORT, EXPRESSION_SHORT)
    } else {
        (46.0, PISTONS, EXPRESSION)
    };
    let top = row((
        parts::compact_title(source),
        parts::ticker(),
        super::controls::status(source, false).flex(1.0),
    ))
    .spacing(8.0)
    .align(Align::Center);
    stack((
        parts::backdrop(source),
        column((
            top,
            now(source, now_height),
            desk(source),
            pistons(source, pistons_height),
            expression(source, expression_height),
        ))
        .spacing(8.0)
        .padding(12.0),
    ))
    .into_view()
}

#[cfg(test)]
mod tests {
    use super::*;
    use caecilia_model::BUILT_IN_ORGANS;
    use manguebyte_gfx::Color;

    #[test]
    fn a_stop_keeps_its_pitch_and_never_trails_off() {
        let s = style(11.5, 650.0, Color::BLACK);
        for (name, make) in BUILT_IN_ORGANS {
            for stop in make().stops() {
                let short = shortened(&stop.name, s, STOP_WIDTH - 8.0);
                assert!(
                    width_of(&short, s) <= STOP_WIDTH - 8.0,
                    "{name}: {short} is too long for a stop"
                );
                assert!(!short.contains('\u{2026}'), "{short}");
                let bare = bare_name(&stop.name);
                assert!(short.ends_with(&stop.name[bare.len()..]), "{short}");
            }
        }
    }

    #[test]
    fn a_long_name_gives_up_only_what_it_must() {
        let s = style(11.5, 500.0, Color::BLACK);
        assert_eq!(shortened("Montre 8", s, 100.0), "Montre 8");
        let short = shortened("Basson-Hautbois 8", s, 70.0);
        assert!(short.starts_with("Basson"), "{short}");
        assert!(short.ends_with(". 8"), "{short}");
    }
}
