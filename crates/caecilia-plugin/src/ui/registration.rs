//! The registration panel: the selector line, the history, and why.
//!
//! The selector line takes the registration grammar (`div:recit &
//! family:string`, `reed | mixture`, `!engaged`), colours it as it is
//! typed, and shows which stops it would take before Enter draws them
//! (Shift+Enter retires them). Up and Down bring back what was typed
//! before. The history shows the way here and the way redo would go on;
//! a step pressed is gone back or forward to. Why shows the stop under
//! the pointer, drawn or not, else the last step's.

use std::rc::Rc;

use caecilia_registration::{Selector, parse};
use manguebyte_gfx::{Circle, Color, Point, Rect, RoundedRect, Scene, Shadow, Size, Stroke};
use manguebyte_ui::access::{AccessNode, Actions, Role};
use manguebyte_ui::{
    AccessCx, Event, EventCx, Handled, IntoView, Key, KeyEvent, MeasureCx, PaintCx, UpdateCx, View,
    Widget, canvas, column, row, stack,
};

use super::actions;
use super::live::{Item, live};
use super::paint::{heading, panel, style, text, width_of};
use super::snapshot::{Snapshot, Source, Step};
use crate::controller::state_of;

/// The panel's height.
const HEIGHT: f32 = 184.0;

/// Where the selector line sits in the panel.
const FIELD_TOP: f32 = 44.0;
const FIELD_HEIGHT: f32 = 32.0;

/// The history's steps as chips, left to right, as many as fit, in the
/// coordinates of the panel's body (below the selector line).
fn chips(size: Size, snapshot: &Snapshot) -> Vec<(Rect, Step)> {
    let chip = style(10.5, 600.0, snapshot.look.text);
    let y = 14.0;
    let mut x = 20.0;
    let mut out = Vec::new();
    for step in &snapshot.history {
        let w = width_of(&step.label, chip) + 18.0;
        if x + w > size.width - 20.0 {
            break;
        }
        out.push((Rect::new(x, y, w, 24.0), step.clone()));
        x += w + 12.0;
    }
    out
}

/// The panel's heading: its name and the summary.
fn draw_head(size: Size, scene: &mut Scene, snapshot: &Snapshot) {
    let rect = Rect::new(0.0, 0.0, size.width, FIELD_TOP);
    heading(
        scene,
        &snapshot.look,
        rect,
        "Registration",
        &snapshot.summary,
    );
}

/// Below the selector line: the history, and why.
fn draw_body(size: Size, scene: &mut Scene, snapshot: &Snapshot) {
    let look = snapshot.look;
    let mut previous: Option<(f32, bool)> = None;
    let mut bottom = 14.0;
    for (r, step) in chips(size, snapshot) {
        let ahead = step.distance > 0;
        if let Some((px, _)) = previous {
            let mut p = manguebyte_gfx::Path::new();
            p.move_to(Point::new(px, r.center().y));
            p.line_to(Point::new(r.x, r.center().y));
            scene.stroke(
                &p,
                Stroke::new(1.2),
                look.edge.with_alpha(if ahead { 0x80 } else { 0xFF }),
            );
        }
        let shape = RoundedRect::new(r, 12.0);
        let ink = if step.distance == 0 {
            scene.shadow(shape, Shadow::glow(10.0, look.accent.with_alpha(0x70)));
            scene.fill(shape, look.accent.with_alpha(0x33));
            scene.stroke(shape, Stroke::new(1.2), look.accent);
            look.text
        } else if ahead {
            // Where redo would go: an outline only.
            scene.stroke(shape, Stroke::new(1.0), look.edge);
            look.muted
        } else {
            scene.fill(shape, look.edge.with_alpha(0x80));
            look.text2
        };
        text(
            scene,
            &step.label,
            style(10.5, 600.0, ink),
            Point::new(r.x + 9.0, r.y + 5.0),
            0.0,
        );
        previous = Some((r.right(), ahead));
        bottom = r.bottom();
    }
    let mut y = bottom + 12.0;
    for why in &snapshot.why {
        let dot = if why.drawn { look.accent } else { look.muted };
        scene.fill(Circle::new(Point::new(24.0, y + 7.0), 3.5), dot);
        let w = text(
            scene,
            &why.lead,
            style(11.5, 650.0, look.text),
            Point::new(34.0, y),
            0.0,
        )
        .width;
        text(
            scene,
            &why.rest,
            style(11.5, 450.0, look.text2),
            Point::new(34.0 + w, y),
            0.0,
        );
        y += 21.0;
    }
}

/// The registration panel.
pub(crate) fn registration(source: &Rc<Source>) -> View {
    let shared = source.shared().clone();
    let back = live(
        "",
        Size::new(540.0, HEIGHT),
        source,
        |_| 0,
        |size, scene, snapshot| {
            panel(
                scene,
                &snapshot.look,
                Rect::new(0.0, 0.0, size.width, size.height),
            );
        },
    );
    let head = live(
        "",
        Size::new(540.0, FIELD_TOP),
        source,
        Snapshot::registration_key,
        draw_head,
    );
    let body = live(
        "Registration",
        Size::new(540.0, HEIGHT - FIELD_TOP - FIELD_HEIGHT),
        source,
        Snapshot::registration_key,
        draw_body,
    )
    .describe(|s| {
        let steps: Vec<&str> = s.history.iter().map(|step| step.label.as_str()).collect();
        let why: Vec<String> = s
            .why
            .iter()
            .map(|w| format!("{}{}", w.lead, w.rest))
            .collect();
        format!("{}; {}; {}", s.summary, steps.join(" > "), why.join("; "))
    })
    .items(|s, size| {
        chips(size, s)
            .into_iter()
            .map(|(rect, step)| Item {
                role: Role::Button,
                name: format!("Step {}", step.label),
                checked: Some(step.distance == 0),
                rect,
            })
            .collect()
    })
    .input(move |cx, event, at, size, snapshot| {
        if !matches!(event, Event::PointerDown { .. }) {
            return Handled::No;
        }
        let Some((_, step)) = chips(size, snapshot)
            .into_iter()
            .find(|(r, _)| r.contains(at))
        else {
            return Handled::No;
        };
        actions::travel(cx.context(), &shared, step.distance);
        Handled::Yes
    });
    let gap = |w: f32, h: f32| canvas("", Size::new(w, h), |_, _| {});
    let line = SelectorLine::new(source);
    stack((
        back,
        column((
            head.height(FIELD_TOP),
            row((gap(20.0, 0.0), line.flex(1.0), gap(20.0, 0.0))).height(FIELD_HEIGHT),
            body.flex(1.0),
        )),
    ))
    .height(HEIGHT)
    .into_view()
}

/// What the line shows on its right.
#[derive(Clone, PartialEq)]
enum Note {
    /// The grammar's reminder.
    Hint,
    /// What the selector would take.
    Matches(Vec<String>),
    /// What is wrong with it.
    Error(String),
    /// What the last Enter did.
    Done(String),
}

/// The selector line.
pub(crate) struct SelectorLine {
    source: Rc<Source>,
    text: String,
    /// The caret, in characters.
    caret: usize,
    focused: bool,
    /// What was typed before, the newest last, and which of it is shown.
    typed: Vec<String>,
    recalled: Option<usize>,
    note: Note,
    /// The light it was last drawn in.
    night: Option<bool>,
}

impl SelectorLine {
    fn new(source: &Rc<Source>) -> SelectorLine {
        SelectorLine {
            source: Rc::clone(source),
            text: String::new(),
            caret: 0,
            focused: false,
            typed: Vec::new(),
            recalled: None,
            note: Note::Hint,
            night: None,
        }
    }

    fn byte(&self, chars: usize) -> usize {
        self.text
            .char_indices()
            .nth(chars)
            .map_or(self.text.len(), |(i, _)| i)
    }

    fn len(&self) -> usize {
        self.text.chars().count()
    }

    /// What the text selects now, or why it does not parse.
    fn examine(&self) -> Note {
        if self.text.trim().is_empty() {
            return Note::Hint;
        }
        match parse(&self.text) {
            Ok(selector) => Note::Matches(self.names(&selector)),
            Err(e) => Note::Error(e.message),
        }
    }

    fn names(&self, selector: &Selector) -> Vec<String> {
        let shared = self.source.shared();
        let organ = self.source.organ();
        let state = state_of(shared.params.stop_mask(), shared.params.coupler_mask());
        selector
            .resolve(&organ, &state)
            .ids()
            .iter()
            .filter_map(|s| organ.stops().get(s.index()).map(|s| s.name.clone()))
            .collect()
    }

    fn edited(&mut self) {
        self.recalled = None;
        self.note = self.examine();
    }

    /// Enter: draw what the selector takes, or retire it with Shift.
    fn commit(&mut self, cx: &mut EventCx<'_>, retire: bool) {
        let selector = match parse(&self.text) {
            Ok(s) => s,
            Err(e) => {
                self.note = Note::Error(e.message);
                return;
            }
        };
        let changed = actions::selector(cx.context(), self.source.shared(), selector, !retire);
        self.note = Note::Done(match (changed, retire) {
            (0, false) => "nothing more to draw".into(),
            (0, true) => "nothing to retire".into(),
            (1, false) => "drew 1 stop".into(),
            (1, true) => "retired 1 stop".into(),
            (n, false) => format!("drew {n} stops"),
            (n, true) => format!("retired {n} stops"),
        });
        let typed = std::mem::take(&mut self.text);
        self.typed.retain(|t| *t != typed);
        self.typed.push(typed);
        self.caret = 0;
        self.recalled = None;
    }

    /// Up and Down: what was typed before.
    fn recall(&mut self, back: bool) {
        if self.typed.is_empty() {
            return;
        }
        let last = self.typed.len() - 1;
        let next = match (self.recalled, back) {
            (None, true) => Some(last),
            (None, false) => None,
            (Some(i), true) => Some(i.saturating_sub(1)),
            (Some(i), false) if i < last => Some(i + 1),
            (Some(_), false) => None,
        };
        self.recalled = next;
        self.text = next.map_or_else(String::new, |i| self.typed[i].clone());
        self.caret = self.len();
        self.note = self.examine();
    }

    /// What the line says on its right, and in what colour.
    fn note_text(&self, look: &super::look::Look) -> (String, Color) {
        match &self.note {
            Note::Hint => (
                "Enter draws \u{b7} Shift+Enter retires".to_string(),
                look.muted,
            ),
            Note::Matches(names) if names.is_empty() => ("takes nothing".to_string(), look.muted),
            Note::Matches(names) => {
                let mut shown = names.iter().take(3).cloned().collect::<Vec<_>>().join(", ");
                if names.len() > 3 {
                    shown.push_str(&format!(" +{}", names.len() - 3));
                }
                (format!("takes {shown}"), look.text2)
            }
            Note::Error(e) => (e.clone(), look.set_red),
            Note::Done(d) => (d.clone(), look.accent),
        }
    }

    fn key(&mut self, cx: &mut EventCx<'_>, event: KeyEvent) -> Handled {
        if !event.pressed {
            // Keys that type stay away from the host while the line has
            // the focus.
            return match event.key {
                Key::Character(_) | Key::Backspace | Key::Delete | Key::Enter => Handled::Yes,
                _ => Handled::No,
            };
        }
        match event.key {
            Key::Character(_) => return Handled::Yes,
            Key::Backspace if self.caret > 0 => {
                let (a, b) = (self.byte(self.caret - 1), self.byte(self.caret));
                self.text.replace_range(a..b, "");
                self.caret -= 1;
                self.edited();
            }
            Key::Delete if self.caret < self.len() => {
                let (a, b) = (self.byte(self.caret), self.byte(self.caret + 1));
                self.text.replace_range(a..b, "");
                self.edited();
            }
            Key::Backspace | Key::Delete => {}
            Key::Left => self.caret = self.caret.saturating_sub(1),
            Key::Right => self.caret = (self.caret + 1).min(self.len()),
            Key::Home => self.caret = 0,
            Key::End => self.caret = self.len(),
            Key::Up => self.recall(true),
            Key::Down => self.recall(false),
            Key::Enter => self.commit(cx, event.modifiers.shift),
            Key::Escape if !self.text.is_empty() => {
                self.text.clear();
                self.caret = 0;
                self.edited();
            }
            _ => return Handled::No,
        }
        Handled::Yes
    }
}

/// The text in the grammar's colours: a field's name muted, its value
/// plain, the operators in the accent.
fn coloured(text: &str, look: &super::look::Look) -> Vec<(String, Color)> {
    let mut out: Vec<(String, Color)> = Vec::new();
    let mut word = String::new();
    let flush = |word: &mut String, out: &mut Vec<(String, Color)>| {
        if word.is_empty() {
            return;
        }
        match word.split_once(':') {
            Some((field, value)) => {
                out.push((format!("{field}:"), look.muted));
                if !value.is_empty() {
                    out.push((value.to_string(), look.text));
                }
            }
            None => out.push((word.clone(), look.text)),
        }
        word.clear();
    };
    for c in text.chars() {
        if matches!(c, '&' | '|' | '!' | '(' | ')' | ' ') {
            flush(&mut word, &mut out);
            let color = if c == ' ' { look.text } else { look.accent };
            out.push((c.to_string(), color));
        } else {
            word.push(c);
        }
    }
    flush(&mut word, &mut out);
    out
}

/// The width of `s` set in the line's type, trailing spaces counted.
fn measured(s: &str) -> f32 {
    let probe = style(12.5, 520.0, Color::BLACK);
    width_of(&format!("{s}|"), probe) - width_of("|", probe)
}

impl Widget for SelectorLine {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        AccessNode::new(Role::TextInput, "Selector")
            .with_actions(Actions {
                focus: true,
                ..Actions::NONE
            })
            .with_value(self.text.clone())
            .with_description(self.note_text(&self.source.look()).0)
            .editable()
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        Size::new(400.0, FIELD_HEIGHT)
    }

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        let night = self.source.look().night;
        if self.night != Some(night) {
            self.night = Some(night);
            cx.request_paint();
        }
        // What the selector takes follows what is drawn.
        if !self.text.is_empty() && !matches!(self.note, Note::Done(_)) {
            let note = self.examine();
            if note != self.note {
                self.note = note;
                cx.request_paint();
            }
        }
    }

    fn paint(&mut self, cx: &PaintCx<'_>, scene: &mut Scene) {
        let look = self.source.look();
        let field = cx.rect();
        let shape = RoundedRect::new(field, 8.0);
        scene.fill(
            shape,
            if look.night {
                Color::hex_rgba(0x0000_0066)
            } else {
                Color::hex(0xFFFFFF)
            },
        );
        let border = match (&self.note, self.focused) {
            (Note::Error(_), _) => look.set_red,
            (_, true) => look.accent,
            _ => look.edge,
        };
        scene.stroke(shape, Stroke::new(1.2), border);
        let y = field.y + 8.0;
        let x0 = field.x + 14.0;
        let prompt = text(
            scene,
            ">",
            style(12.5, 700.0, look.accent),
            Point::new(x0, y),
            0.0,
        )
        .width;
        let x = x0 + prompt + 10.0;
        if self.text.is_empty() && !self.focused {
            text(
                scene,
                "a selector: div:recit & family:string",
                style(12.5, 450.0, look.muted),
                Point::new(x, y),
                0.0,
            );
        } else {
            let mut at = x;
            for (piece, color) in coloured(&self.text, &look) {
                text(
                    scene,
                    &piece,
                    style(12.5, 520.0, color),
                    Point::new(at, y),
                    0.0,
                );
                at += measured(&piece);
            }
            if self.focused {
                let caret = x + measured(&self.text[..self.byte(self.caret)]);
                scene.fill(Rect::new(caret + 1.0, y - 1.0, 1.5, 17.0), look.accent);
            }
        }
        // On the right: the hint, what it takes, what went wrong or what
        // it did, if there is room.
        let (note, color) = self.note_text(&look);
        let note_style = style(10.5, 500.0, color);
        let room = field.right() - 12.0 - (x + measured(&self.text) + 16.0);
        if width_of(&note, note_style) <= room {
            text(
                scene,
                &note,
                note_style,
                Point::new(field.right() - 12.0, y + 1.5),
                1.0,
            );
        }
    }

    fn event(&mut self, cx: &mut EventCx<'_>, event: &Event) -> Handled {
        let handled = match *event {
            Event::PointerDown { .. } => {
                cx.request_focus();
                self.caret = self.len();
                Handled::Yes
            }
            Event::Text(c) if self.focused && !c.is_control() => {
                let at = self.byte(self.caret);
                self.text.insert(at, c);
                self.caret += 1;
                self.edited();
                Handled::Yes
            }
            Event::Key(key) if self.focused => self.key(cx, key),
            _ => Handled::No,
        };
        if handled == Handled::Yes {
            cx.request_paint();
        }
        handled
    }

    fn focus_changed(&mut self, cx: &mut EventCx<'_>, focused: bool) {
        self.focused = focused;
        if !focused && matches!(self.note, Note::Done(_)) {
            self.note = Note::Hint;
        }
        cx.request_paint();
    }
}
