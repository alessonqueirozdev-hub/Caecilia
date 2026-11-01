// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Every change to a registration, as a value.

use caecilia_model::{CouplerId, DivisionId, Organ, StopId, TonalFamily};

use crate::intent::{RegistrationIntent, port};
use crate::selector::Selector;
use crate::state::{RegistrationState, StopSet};

/// What build-plenum is asked for.
#[derive(Clone, Debug, PartialEq)]
pub struct PlenumSpec {
    /// Only this division; the whole organ if `None`.
    pub division: Option<DivisionId>,
    /// Crown the chorus with its mixtures.
    pub include_mixtures: bool,
    /// Add the chorus and foundation reeds (not solo colour).
    pub include_chorus_reeds: bool,
    /// Keep quints and tierces out of the diapason chorus.
    pub exclude_mutations: bool,
    /// Lowest octave class: -1 for 16'.
    pub lowest_octave: i32,
    /// Highest octave class: 2 for 2'.
    pub highest_octave: i32,
    /// Add to what is drawn instead of replacing it.
    pub additive: bool,
}

impl Default for PlenumSpec {
    fn default() -> Self {
        Self {
            division: None,
            include_mixtures: true,
            include_chorus_reeds: false,
            exclude_mutations: true,
            lowest_octave: -1,
            highest_octave: 2,
            additive: false,
        }
    }
}

/// The principal chorus the builder's convention draws: principals 16' to 2'
/// in the spec's range, mutations excluded (exact footages keep the quints
/// out), crowned by the mixtures, chorus reeds on request.
pub fn build_plenum(organ: &Organ, spec: &PlenumSpec, current: &RegistrationState) -> StopSet {
    let mut chosen: StopSet = organ
        .stops()
        .iter()
        .filter(|s| spec.division.is_none_or(|d| s.division == d))
        .filter(|s| {
            if s.family == TonalFamily::Mixture || s.is_compound() {
                return spec.include_mixtures;
            }
            if s.family == TonalFamily::Reed {
                use caecilia_model::ChorusRole::{Chorus, Foundation};
                return spec.include_chorus_reeds && matches!(s.role, Foundation | Chorus);
            }
            if s.family != TonalFamily::Principal
                || (spec.exclude_mutations && s.footage.is_mutation())
            {
                return false;
            }
            (spec.lowest_octave..=spec.highest_octave).contains(&s.footage.octave_class_from_8())
        })
        .map(|s| s.id)
        .collect();
    if spec.additive {
        chosen.unite(&current.stops);
    }
    chosen
}

/// One change to the registration.
#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    /// Draw a stop.
    EngageStop(StopId),
    /// Retire a stop.
    DisengageStop(StopId),
    /// Flip a stop.
    ToggleStop(StopId),
    /// Draw every stop matching.
    EngageSelector(Selector),
    /// Retire every stop matching.
    DisengageSelector(Selector),
    /// Flip every stop matching.
    ToggleSelector(Selector),
    /// Draw only what matches (within a division if given); couplers untouched.
    Solo(Selector, Option<DivisionId>),
    /// Retire everything (within a division if given).
    Clear(Option<DivisionId>),
    /// Derive a plenum.
    BuildPlenum(PlenumSpec),
    /// Draw a coupler.
    EngageCoupler(CouplerId),
    /// Retire a coupler.
    DisengageCoupler(CouplerId),
    /// Flip a coupler.
    ToggleCoupler(CouplerId),
    /// Replace the whole state.
    RecallSnapshot(RegistrationState),
    /// Port an intent onto this organ and draw it; couplers untouched.
    ApplyIntent(RegistrationIntent),
}

impl Command {
    /// A short human description.
    pub fn describe(&self) -> &'static str {
        match self {
            Self::EngageStop(_) => "engage stop",
            Self::DisengageStop(_) => "disengage stop",
            Self::ToggleStop(_) => "toggle stop",
            Self::EngageSelector(_) => "engage selector",
            Self::DisengageSelector(_) => "disengage selector",
            Self::ToggleSelector(_) => "toggle selector",
            Self::Solo(..) => "solo selector",
            Self::Clear(_) => "clear",
            Self::BuildPlenum(_) => "build plenum",
            Self::EngageCoupler(_) => "engage coupler",
            Self::DisengageCoupler(_) => "disengage coupler",
            Self::ToggleCoupler(_) => "toggle coupler",
            Self::RecallSnapshot(_) => "recall snapshot",
            Self::ApplyIntent(_) => "apply intent",
        }
    }

    /// The state this command makes of `base` on `organ`.
    pub fn apply_to(&self, organ: &Organ, base: &RegistrationState) -> RegistrationState {
        let mut next = base.clone();
        let in_division =
            |id: StopId, d: DivisionId| organ.stop(id).is_some_and(|s| s.division == d);
        match self {
            Self::EngageStop(s) => {
                next.stops.insert(*s);
            }
            Self::DisengageStop(s) => {
                next.stops.remove(*s);
            }
            Self::ToggleStop(s) => {
                next.stops.toggle(*s);
            }
            Self::EngageSelector(sel) => next.stops.unite(&sel.resolve(organ, base)),
            Self::DisengageSelector(sel) => next.stops.subtract(&sel.resolve(organ, base)),
            Self::ToggleSelector(sel) => {
                for &id in sel.resolve(organ, base).ids() {
                    next.stops.toggle(id);
                }
            }
            Self::Solo(sel, scope) => {
                let matched = sel.resolve(organ, base);
                match scope {
                    Some(d) => {
                        for &id in base.stops.ids().iter().filter(|&&id| in_division(id, *d)) {
                            next.stops.remove(id);
                        }
                        for &id in matched.ids().iter().filter(|&&id| in_division(id, *d)) {
                            next.stops.insert(id);
                        }
                    }
                    None => next.stops = matched,
                }
            }
            Self::Clear(scope) => match scope {
                Some(d) => {
                    for &id in base.stops.ids().iter().filter(|&&id| in_division(id, *d)) {
                        next.stops.remove(id);
                    }
                }
                None => next = RegistrationState::default(),
            },
            Self::BuildPlenum(spec) => {
                let built = build_plenum(organ, spec, base);
                match (spec.additive, spec.division) {
                    (false, Some(d)) => {
                        for &id in base.stops.ids().iter().filter(|&&id| in_division(id, d)) {
                            next.stops.remove(id);
                        }
                        next.stops.unite(&built);
                    }
                    _ => next.stops = built,
                }
            }
            Self::EngageCoupler(c) => next.engage_coupler(*c),
            Self::DisengageCoupler(c) => next.disengage_coupler(*c),
            Self::ToggleCoupler(c) => next.toggle_coupler(*c),
            Self::RecallSnapshot(s) => next = s.clone(),
            Self::ApplyIntent(i) => next.stops = port(i, organ).resolved,
        }
        next
    }
}
