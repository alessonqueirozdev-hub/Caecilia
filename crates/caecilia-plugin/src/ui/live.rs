//! A part of the console drawn from the frame's reading, and drawn again
//! only when what it shows has changed; it may take the pointer, and name
//! its controls to assistive technology.

use std::rc::Rc;

use manguebyte_gfx::{Point, Rect, Scene, Size, Transform};
use manguebyte_ui::access::{AccessItem, AccessNode, Action, Actions, Role};
use manguebyte_ui::{
    AccessCx, Button, Event, EventCx, Handled, KeyboardState, MeasureCx, Modifiers, NoteEvent,
    PaintCx, UpdateCx, Widget,
};

use super::actions;
use super::snapshot::{Snapshot, Source};

type Draw = dyn Fn(Size, &mut Scene, &Snapshot);

/// What a part does with the pointer: the event, where it is in the part's
/// own coordinates, the part's size, and the reading it shows.
pub(crate) type Input = dyn FnMut(&mut EventCx<'_>, &Event, Point, Size, &Snapshot) -> Handled;

/// A control inside a part, for assistive technology and tests.
pub(crate) struct Item {
    pub(crate) role: Role,
    pub(crate) name: String,
    /// On or off, for a switch.
    pub(crate) checked: Option<bool>,
    /// Where it is, in the part's coordinates.
    pub(crate) rect: Rect,
}

type Items = fn(&Snapshot, Size) -> Vec<Item>;

/// What a part does with the pointer over it: where, in its coordinates,
/// or `None` when it has gone.
type Hover = dyn FnMut(Option<Point>, Size, &Snapshot);

/// A part: its name, its natural size, what of the reading it shows, and
/// how it draws it in its own coordinates, its top left at the origin.
pub(crate) struct Live {
    name: String,
    natural: Size,
    source: Rc<Source>,
    key: fn(&Snapshot) -> u64,
    draw: Box<Draw>,
    /// What it shows, in words, for assistive technology and tests.
    describe: Option<fn(&Snapshot) -> String>,
    items: Option<Items>,
    input: Option<Box<Input>>,
    hover: Option<Box<Hover>>,
    seen: Option<u64>,
    shown: Option<Rc<Snapshot>>,
}

/// A part of the console.
pub(crate) fn live(
    name: impl Into<String>,
    natural: Size,
    source: &Rc<Source>,
    key: fn(&Snapshot) -> u64,
    draw: impl Fn(Size, &mut Scene, &Snapshot) + 'static,
) -> Live {
    Live {
        name: name.into(),
        natural,
        source: Rc::clone(source),
        key,
        draw: Box::new(draw),
        describe: None,
        items: None,
        input: None,
        hover: None,
        seen: None,
        shown: None,
    }
}

impl Live {
    /// The same part, saying what it shows in words.
    pub(crate) fn describe(mut self, describe: fn(&Snapshot) -> String) -> Self {
        self.describe = Some(describe);
        self
    }

    /// The same part, naming the controls in it.
    pub(crate) fn items(mut self, items: Items) -> Self {
        self.items = Some(items);
        self
    }

    /// The same part, taking the pointer.
    pub(crate) fn input(
        mut self,
        input: impl FnMut(&mut EventCx<'_>, &Event, Point, Size, &Snapshot) -> Handled + 'static,
    ) -> Self {
        self.input = Some(Box::new(input));
        self
    }

    /// The same part, following the pointer over it.
    pub(crate) fn hover(
        mut self,
        hover: impl FnMut(Option<Point>, Size, &Snapshot) + 'static,
    ) -> Self {
        self.hover = Some(Box::new(hover));
        self
    }

    /// The reading to act on: the one shown, else this frame's.
    fn reading(&self, cx: &EventCx<'_>) -> Rc<Snapshot> {
        self.shown
            .clone()
            .unwrap_or_else(|| self.source.get(cx.now()))
    }
}

impl Widget for Live {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        let node = AccessNode::new(Role::Group, self.name.as_str());
        match (self.describe, &self.shown) {
            (Some(describe), Some(snapshot)) => node.with_value(describe(snapshot)),
            _ => node,
        }
    }

    fn access_items(&self, cx: &AccessCx<'_>, out: &mut Vec<AccessItem>) {
        let (Some(items), Some(snapshot)) = (self.items, &self.shown) else {
            return;
        };
        let rect = cx.rect();
        let size = Size::new(rect.width, rect.height);
        for (key, item) in items(snapshot, size).into_iter().enumerate() {
            let mut node = AccessNode::new(item.role, item.name).with_actions(Actions::PRESS);
            if let Some(on) = item.checked {
                node = node.with_checked(on);
            }
            out.push(AccessItem::new(
                key,
                node,
                item.rect.translate(Point::new(rect.x, rect.y)),
            ));
        }
    }

    fn act_item(&mut self, cx: &mut EventCx<'_>, key: usize, action: Action) -> bool {
        if action != Action::Press {
            return false;
        }
        let snapshot = self.reading(cx);
        let rect = cx.rect();
        let size = Size::new(rect.width, rect.height);
        let Some(item) = self
            .items
            .and_then(|items| items(&snapshot, size).into_iter().nth(key))
        else {
            return false;
        };
        let Some(input) = self.input.as_mut() else {
            return false;
        };
        // A press and a release at its middle.
        let local = item.rect.center();
        let at = Point::new(local.x + rect.x, local.y + rect.y);
        let down = Event::PointerDown {
            at,
            button: Button::Primary,
            clicks: 1,
            modifiers: Modifiers::NONE,
        };
        let up = Event::PointerUp {
            at,
            button: Button::Primary,
            modifiers: Modifiers::NONE,
        };
        input(cx, &down, local, size, &snapshot);
        input(cx, &up, local, size, &snapshot);
        true
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        self.natural
    }

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        let snapshot = self.source.get(cx.now());
        let key = (self.key)(&snapshot);
        if self.seen != Some(key) {
            self.seen = Some(key);
            self.shown = Some(snapshot);
            cx.request_paint();
        }
    }

    fn paint(&mut self, cx: &PaintCx<'_>, scene: &mut Scene) {
        let Some(snapshot) = &self.shown else {
            return;
        };
        let rect = cx.rect();
        let size = Size::new(rect.width, rect.height);
        scene.transformed(Transform::translate(rect.x, rect.y), |scene| {
            (self.draw)(size, scene, snapshot);
        });
    }

    fn hover_moved(&mut self, cx: &mut EventCx<'_>, at: Point) {
        let snapshot = self.reading(cx);
        let rect = cx.rect();
        if let Some(hover) = self.hover.as_mut() {
            let local = Point::new(at.x - rect.x, at.y - rect.y);
            hover(Some(local), Size::new(rect.width, rect.height), &snapshot);
        }
    }

    fn hover_changed(&mut self, cx: &mut EventCx<'_>, hovered: bool) {
        if hovered {
            return;
        }
        let snapshot = self.reading(cx);
        let rect = cx.rect();
        if let Some(hover) = self.hover.as_mut() {
            hover(None, Size::new(rect.width, rect.height), &snapshot);
        }
    }

    fn event(&mut self, cx: &mut EventCx<'_>, event: &Event) -> Handled {
        if self.input.is_none() {
            return Handled::No;
        }
        let rect = cx.rect();
        let local = |at: Point| Point::new(at.x - rect.x, at.y - rect.y);
        let at = match *event {
            Event::PointerDown { at, .. }
            | Event::PointerMove { at, .. }
            | Event::PointerUp { at, .. } => local(at),
            Event::PointerCancel => Point::new(f32::NAN, f32::NAN),
            _ => return Handled::No,
        };
        let snapshot = self.reading(cx);
        let size = Size::new(rect.width, rect.height);
        let handled = match self.input.as_mut() {
            Some(input) => input(cx, event, at, size, &snapshot),
            None => Handled::No,
        };
        if handled == Handled::Yes {
            cx.request_paint();
        }
        handled
    }
}

/// Nothing to see: lights the keys held on each manual, sends the keys
/// played on them to the organ, and keeps the frames coming so the wind
/// moves.
pub(crate) struct KeysPoller {
    source: Rc<Source>,
    keyboards: Vec<(KeyboardState, Option<usize>)>,
    seen: Vec<u128>,
}

impl KeysPoller {
    pub(crate) fn new(source: &Rc<Source>, keyboards: Vec<(KeyboardState, Option<usize>)>) -> Self {
        let seen = vec![0; keyboards.len()];
        Self {
            source: Rc::clone(source),
            keyboards,
            seen,
        }
    }
}

impl Widget for KeysPoller {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        AccessNode::new(Role::Group, "")
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        Size::new(0.0, 0.0)
    }

    fn paint(&mut self, _cx: &PaintCx<'_>, _scene: &mut Scene) {}

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        let snapshot = self.source.get(cx.now());
        for ((keys, channel), seen) in self.keyboards.iter().zip(&mut self.seen) {
            if let Some(channel) = channel {
                for event in keys.take_events() {
                    match event {
                        NoteEvent::On { note, .. } => {
                            actions::play(self.source.shared(), *channel, note, true);
                        }
                        NoteEvent::Off { note } => {
                            actions::play(self.source.shared(), *channel, note, false);
                        }
                        _ => {}
                    }
                }
            }
            let held = channel.map_or(0, |c| snapshot.held[c]);
            if held != *seen {
                *seen = held;
                keys.set_sounding((0..128u8).filter(|k| held & (1 << k) != 0));
            }
        }
        cx.request_frame();
    }
}
