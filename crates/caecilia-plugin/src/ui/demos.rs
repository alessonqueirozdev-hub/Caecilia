//! The demonstrations as the console plays them: each piece's parts given
//! to the organ playing, its stops drawn, and the tour that plays every stop
//! alone.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::Duration;

use manguebyte_plugin::editor::EditorContext;

use caecilia_model::{DivisionId, DivisionKind, Organ, StopId};
use caecilia_registration::{RegistrationState, StopSet};

use super::Shared;
use super::actions;
use super::snapshot::name_registration;
use crate::UiNote;
use crate::demo::{PHRASE, PHRASE_INDEX, PIECES, Piece};

/// The rows the console offers: each piece, then the tour.
pub(crate) fn names() -> Vec<&'static str> {
    PIECES.iter().map(|p| p.name).chain([PHRASE.name]).collect()
}

/// The row of the tour.
pub(crate) const TOUR_ROW: usize = PIECES.len();

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

/// The stops `piece` draws on this organ.
fn registration(organ: &Organ, piece: &Piece) -> RegistrationState {
    let parts = parts(organ);
    let mut stops = StopSet::new();
    for (part, families) in piece.registration {
        let division = parts[usize::from(*part)].0;
        for stop in organ.stops() {
            if stop.division == division && families.contains(&stop.family) {
                stops.insert(stop.id);
            }
        }
    }
    RegistrationState {
        stops,
        couplers: Vec::new(),
    }
}

fn send(shared: &Shared, note: UiNote) {
    if let Ok(mut notes) = shared.notes.lock() {
        let _ = notes.push(note);
    }
}

/// Draw piece `n`'s stops and play it; returns the stops drawn.
pub(crate) fn play(context: &EditorContext, shared: &Shared, organ: &Organ, n: usize) -> u64 {
    let Some(piece) = PIECES.get(n) else {
        return 0;
    };
    let state = registration(organ, piece);
    let mask = state.stops.to_mask();
    actions::recall_state(context, shared, state);
    let channels = parts(organ).map(|p| p.1);
    send(
        shared,
        UiNote::Play {
            piece: n as u8,
            channels,
            shift: 0,
        },
    );
    mask
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
