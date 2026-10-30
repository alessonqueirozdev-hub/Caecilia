//! A part of the console drawn from the frame's reading, and drawn again
//! only when what it shows has changed.

use std::rc::Rc;

use manguebyte_gfx::{Scene, Size, Transform};
use manguebyte_ui::access::{AccessNode, Role};
use manguebyte_ui::{AccessCx, KeyboardState, MeasureCx, PaintCx, UpdateCx, Widget};

use super::snapshot::{Snapshot, Source};

type Draw = dyn Fn(Size, &mut Scene, &Snapshot);

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
}

impl Widget for Live {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        let node = AccessNode::new(Role::Group, self.name.as_str());
        match (self.describe, &self.shown) {
            (Some(describe), Some(snapshot)) => node.with_value(describe(snapshot)),
            _ => node,
        }
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
}

/// Nothing to see: lights the keys held on each manual, and keeps the
/// frames coming so the wind moves.
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
            let held = channel.map_or(0, |c| snapshot.held[c]);
            if held != *seen {
                *seen = held;
                keys.set_sounding((0..128u8).filter(|k| held & (1 << k) != 0));
            }
        }
        cx.request_frame();
    }
}
