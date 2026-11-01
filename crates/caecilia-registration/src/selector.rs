// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Selecting stops by what they are.

use caecilia_model::{
    ChorusRole, DivisionId, Footage, Organ, PitchClass, Stop, StopId, TonalFamily,
};

use crate::state::{RegistrationState, StopSet};

/// Whether a query constrains the engaged state.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Engaged {
    /// No constraint.
    #[default]
    Any,
    /// Only drawn stops.
    Only,
    /// Only stops not drawn.
    Exclude,
}

/// One predicate: optional constraints on a stop's identity, all ANDed.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct StopQuery {
    /// Exact stop id.
    pub id: Option<StopId>,
    /// Tonal family.
    pub family: Option<TonalFamily>,
    /// Chorus role.
    pub role: Option<ChorusRole>,
    /// Pitch class.
    pub pitch_class: Option<PitchClass>,
    /// Exact footage.
    pub footage: Option<Footage>,
    /// Resolved division.
    pub division: Option<DivisionId>,
    /// A division by name, matched case-insensitively against the organ's
    /// division names and their usual equivalents in other traditions.
    pub division_name: String,
    /// A case-insensitive substring of the stop's name.
    pub name_contains: String,
    /// Engaged-state constraint.
    pub engaged: Engaged,
    /// Keep only non-octave mutations.
    pub mutations_only: bool,
    /// Drop non-octave mutations.
    pub exclude_mutations: bool,
}

fn contains_no_case(haystack: &str, needle: &str) -> bool {
    needle.is_empty() || haystack.to_lowercase().contains(&needle.to_lowercase())
}

/// Division names that mean the same keyboard across traditions: what a
/// French organ calls the Récit an English one calls the Swell.
const DIVISION_EQUIVALENTS: &[&[&str]] = &[
    &["swell", "récit", "recit", "schwellwerk", "oberwerk"],
    &[
        "great",
        "grand-orgue",
        "grand orgue",
        "hauptwerk",
        "hoofdwerk",
    ],
    &[
        "choir",
        "positif",
        "positive",
        "rückpositiv",
        "ruckpositiv",
        "brustwerk",
    ],
    &["pedal", "pédale", "pedale", "pedaal"],
    &["solo", "bombarde"],
];

/// The organ's division called `name`, directly or by its equivalent name.
pub fn division_named(organ: &Organ, name: &str) -> Option<DivisionId> {
    if let Some(d) = organ
        .divisions()
        .iter()
        .find(|d| contains_no_case(&d.name, name))
    {
        return Some(d.id);
    }
    let lower = name.to_lowercase();
    let group = DIVISION_EQUIVALENTS
        .iter()
        .find(|g| g.iter().any(|n| lower.contains(n)))?;
    organ
        .divisions()
        .iter()
        .find(|d| {
            let dn = d.name.to_lowercase();
            group.iter().any(|n| dn.contains(n))
        })
        .map(|d| d.id)
}

impl StopQuery {
    /// Whether this stop satisfies every constraint except the division name.
    pub fn matches(&self, stop: &Stop, engaged: bool) -> bool {
        self.id.is_none_or(|i| stop.id == i)
            && self.family.is_none_or(|f| stop.family == f)
            && self.role.is_none_or(|r| stop.role == r)
            && self.pitch_class.is_none_or(|p| stop.pitch_class == p)
            && self.footage.is_none_or(|f| stop.footage == f)
            && self.division.is_none_or(|d| stop.division == d)
            && !(self.mutations_only && !stop.footage.is_mutation())
            && !(self.exclude_mutations && stop.footage.is_mutation())
            && match self.engaged {
                Engaged::Any => true,
                Engaged::Only => engaged,
                Engaged::Exclude => !engaged,
            }
            && contains_no_case(&stop.name, &self.name_contains)
    }

    /// The stops it selects. A division name that names nothing selects
    /// nothing: an unknown division cannot match a stop.
    pub fn resolve(&self, organ: &Organ, state: &RegistrationState) -> StopSet {
        let mut division = self.division;
        if !self.division_name.is_empty() && division.is_none() {
            match division_named(organ, &self.division_name) {
                Some(d) => division = Some(d),
                None => return StopSet::new(),
            }
        }
        organ
            .stops()
            .iter()
            .filter(|s| division.is_none_or(|d| s.division == d))
            .filter(|s| self.matches(s, state.is_engaged(s.id)))
            .map(|s| s.id)
            .collect()
    }
}

/// A lazily evaluated set-algebra tree over [`StopQuery`] atoms.
#[derive(Clone, Debug, PartialEq)]
pub enum Selector {
    /// One predicate.
    Atom(StopQuery),
    /// `a | b | ...`.
    Union(Vec<Selector>),
    /// `a & b & ...`.
    Intersect(Vec<Selector>),
    /// The first minus the rest.
    Difference(Vec<Selector>),
    /// Everything but this.
    Complement(Box<Selector>),
}

impl Default for Selector {
    /// Every stop.
    fn default() -> Self {
        Self::Atom(StopQuery::default())
    }
}

impl Selector {
    /// Every stop of the organ.
    pub fn universe(organ: &Organ) -> StopSet {
        organ.stops().iter().map(|s| s.id).collect()
    }

    /// The stops it selects, against `state`.
    pub fn resolve(&self, organ: &Organ, state: &RegistrationState) -> StopSet {
        match self {
            Self::Atom(q) => q.resolve(organ, state),
            Self::Union(v) => {
                let mut acc = StopSet::new();
                for s in v {
                    acc.unite(&s.resolve(organ, state));
                }
                acc
            }
            Self::Intersect(v) => {
                let mut it = v.iter();
                let Some(first) = it.next() else {
                    return StopSet::new();
                };
                let mut acc = first.resolve(organ, state);
                for s in it {
                    acc.intersect(&s.resolve(organ, state));
                }
                acc
            }
            Self::Difference(v) => {
                let mut it = v.iter();
                let Some(first) = it.next() else {
                    return StopSet::new();
                };
                let mut acc = first.resolve(organ, state);
                for s in it {
                    acc.subtract(&s.resolve(organ, state));
                }
                acc
            }
            Self::Complement(inner) => {
                let mut u = Self::universe(organ);
                u.subtract(&inner.resolve(organ, state));
                u
            }
        }
    }
}

impl std::ops::BitOr for Selector {
    type Output = Selector;
    fn bitor(self, rhs: Self) -> Self {
        Self::Union(vec![self, rhs])
    }
}

impl std::ops::BitAnd for Selector {
    type Output = Selector;
    fn bitand(self, rhs: Self) -> Self {
        Self::Intersect(vec![self, rhs])
    }
}

impl std::ops::Sub for Selector {
    type Output = Selector;
    fn sub(self, rhs: Self) -> Self {
        Self::Difference(vec![self, rhs])
    }
}

impl std::ops::Not for Selector {
    type Output = Selector;
    fn not(self) -> Self {
        Self::Complement(Box::new(self))
    }
}
