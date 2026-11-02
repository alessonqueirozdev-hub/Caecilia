// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demonstrations as the console plays them: each piece's parts given
//! to the organ playing, its registrations drawn as it reaches them, and
//! the tour that plays every stop alone.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::Duration;

use manguebyte_params::Param;
use manguebyte_plugin::editor::{EditorContext, Gesture};

use caecilia_model::{CouplerId, DivisionId, DivisionKind, Organ, StopId};
use caecilia_registration::{RegistrationState, StopSet};

use super::Shared;
use super::actions;
use super::snapshot::name_registration;
use crate::UiNote;
use crate::demo::{self, PHRASE, PHRASE_INDEX, Registration};

/// The rows the console offers: each piece, then the tour.
pub(crate) fn names() -> Vec<&'static str> {
    demo::demos()
        .iter()
        .map(|d| d.name.as_str())
        .chain([PHRASE.name])
        .collect()
}

/// The rows' short names, for a desk too narrow for the whole ones.
pub(crate) fn labels() -> Vec<&'static str> {
    demo::demos()
        .iter()
        .map(|d| d.short.as_str())
        .chain(["Every stop, one by one"])
        .collect()
}

/// The row of the tour.
pub(crate) fn tour_row() -> usize {
    demo::demos().len()
}

/// How long each stop of the tour sounds, its phrase and a breath.
const TOUR_STEP: Duration = Duration::from_millis(2400);

/// The division and the MIDI channel each part plays on this organ: the
/// great on the main manual, the swell on the enclosed one (else another
/// manual), the pedal on the pedalboard; a part the organ lacks falls back
/// to the great.
pub(crate) fn parts(organ: &Organ) -> [(DivisionId, u8); 3] {
    let manual_of = |division: DivisionId| {
        organ
            .manuals()
            .iter()
            .find(|m| m.division == division)
            .map_or(0, |m| m.midi_channel.clamp(0, 15) as u8)
    };
    let kind = |d: DivisionId| organ.division(d).map(|d| (d.kind, d.enclosed));
    let great = organ.primary_manual();
    let manuals: Vec<DivisionId> = organ
        .manuals()
        .iter()
        .map(|m| m.division)
        .filter(|d| *d != great && kind(*d).is_some_and(|k| k.0 == DivisionKind::Manual))
        .collect();
    let swell = manuals
        .iter()
        .find(|d| kind(**d).is_some_and(|k| k.1))
        .or(manuals.first())
        .copied()
        .unwrap_or(great);
    let pedal = organ
        .manuals()
        .iter()
        .map(|m| m.division)
        .find(|d| kind(*d).is_some_and(|k| k.0 == DivisionKind::Pedal))
        .unwrap_or(great);
    [great, swell, pedal].map(|d| (d, manual_of(d)))
}

/// What `registration` draws on this organ: each part's stops by name, or
/// by family where the organ has none of the names; the couplers it names.
fn registration(organ: &Organ, registration: &Registration) -> RegistrationState {
    let parts = parts(organ);
    let mut stops = StopSet::new();
    for draw in registration.draws {
        let division = parts[usize::from(draw.part)].0;
        let named: Vec<StopId> = organ
            .stops()
            .iter()
            .filter(|s| s.division == division && draw.names.contains(&s.name.as_str()))
            .map(|s| s.id)
            .collect();
        if named.is_empty() {
            for stop in organ.stops() {
                if stop.division == division && draw.families.contains(&stop.family) {
                    stops.insert(stop.id);
                }
            }
        } else {
            for id in named {
                stops.insert(id);
            }
        }
    }
    let mut couplers: Vec<CouplerId> = organ
        .couplers()
        .iter()
        .enumerate()
        .filter(|(_, c)| registration.couplers.contains(&c.name.as_str()))
        .map(|(i, _)| CouplerId(i as u16))
        .collect();
    couplers.sort();
    RegistrationState { stops, couplers }
}

/// Draw `r`, the tremulant with it, naming it `label` in the history.
fn draw_registration(
    context: &EditorContext,
    shared: &Shared,
    organ: &Organ,
    named: &Rc<RefCell<Vec<(u64, String)>>>,
    r: &Registration,
    label: &str,
) {
    let state = registration(organ, r);
    name_registration(named, state.stops.to_mask(), label);
    actions::recall_state(context, shared, state);
    let tremulant = &shared.params.tremulant;
    if tremulant.value() != r.tremulant {
        Gesture::set_once(
            context,
            tremulant.param_id(),
            if r.tremulant { 1.0 } else { 0.0 },
        );
    }
}

fn send(shared: &Shared, note: UiNote) {
    if let Ok(mut notes) = shared.notes.lock() {
        let _ = notes.push(note);
    }
}

/// Draw piece `n`'s first registration and play it.
pub(crate) fn play(
    context: &EditorContext,
    shared: &Shared,
    organ: &Organ,
    named: &Rc<RefCell<Vec<(u64, String)>>>,
    n: usize,
) {
    let Some(piece) = demo::demos().get(n) else {
        return;
    };
    if let Some(first) = piece.changes.first() {
        draw_registration(
            context,
            shared,
            organ,
            named,
            &first.registration,
            first.label,
        );
    }
    let channels = parts(organ).map(|p| p.1);
    send(
        shared,
        UiNote::Play {
            piece: n as u8,
            channels,
            shift: 0,
        },
    );
}

/// The registrations of a piece playing, drawn as it reaches them.
pub(crate) struct Script {
    context: EditorContext,
    named: Rc<RefCell<Vec<(u64, String)>>>,
    piece: usize,
    /// The next registration to draw.
    next: usize,
    /// When the piece began, on the console's clock; `None` before the
    /// first frame.
    began: Option<Duration>,
}

impl Script {
    /// The registrations after the first of piece `n`, which [`play`] drew.
    pub(crate) fn new(
        context: &EditorContext,
        named: Rc<RefCell<Vec<(u64, String)>>>,
        n: usize,
    ) -> Script {
        Script {
            context: context.clone(),
            named,
            piece: n,
            next: 1,
            began: None,
        }
    }

    /// Draw the registration the piece has reached, if one is due; false
    /// once every one is drawn.
    pub(crate) fn advance(&mut self, now: Duration, shared: &Shared, organ: &Organ) -> bool {
        let Some(piece) = demo::demos().get(self.piece) else {
            return false;
        };
        let began = *self.began.get_or_insert(now);
        let at = now.saturating_sub(began).as_secs_f32();
        while let Some(change) = piece.changes.get(self.next) {
            if change.beat * piece.beat > at {
                return true;
            }
            draw_registration(
                &self.context,
                shared,
                organ,
                &self.named,
                &change.registration,
                change.label,
            );
            self.next += 1;
        }
        false
    }
}

/// Stop whatever plays.
pub(crate) fn stop(shared: &Shared) {
    send(shared, UiNote::Stop);
}

/// The tour of the stops: each stop drawn alone, playing its phrase.
pub(crate) struct Tour {
    context: EditorContext,
    named: Rc<RefCell<Vec<(u64, String)>>>,
    step: usize,
    /// When the next stop is due, on the console's clock; `None` before the
    /// first.
    next: Option<Duration>,
}

impl Tour {
    pub(crate) fn new(context: &EditorContext, named: Rc<RefCell<Vec<(u64, String)>>>) -> Tour {
        Tour {
            context: context.clone(),
            named,
            step: 0,
            next: None,
        }
    }

    /// The stop sounding: its index and its number in the tour.
    pub(crate) fn at(&self) -> usize {
        self.step.saturating_sub(1)
    }

    /// Move on if the stop playing has had its time; false once every stop
    /// has played.
    pub(crate) fn advance(&mut self, now: Duration, shared: &Shared, organ: &Organ) -> bool {
        if self.next.is_some_and(|due| now < due) {
            return true;
        }
        let Some(stop) = organ.stops().get(self.step) else {
            return false;
        };
        let mut stops = StopSet::new();
        stops.insert(StopId(self.step as u16));
        name_registration(&self.named, stops.to_mask(), PHRASE.short);
        actions::recall_state(
            &self.context,
            shared,
            RegistrationState {
                stops,
                couplers: Vec::new(),
            },
        );
        let parts = parts(organ);
        let pedal = organ
            .division(stop.division)
            .is_some_and(|d| d.kind == DivisionKind::Pedal);
        let channel = parts
            .iter()
            .find(|p| p.0 == stop.division)
            .map(|p| p.1)
            .unwrap_or_else(|| {
                organ
                    .manuals()
                    .iter()
                    .find(|m| m.division == stop.division)
                    .map_or(parts[0].1, |m| m.midi_channel.clamp(0, 15) as u8)
            });
        send(
            shared,
            UiNote::Play {
                piece: PHRASE_INDEX,
                channels: [channel; 3],
                shift: if pedal { -24 } else { 0 },
            },
        );
        self.step += 1;
        self.next = Some(now + TOUR_STEP);
        true
    }
}
