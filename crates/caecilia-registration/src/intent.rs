// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! A registration's meaning, carried across organs.

use caecilia_model::{ChorusRole, Organ};

use crate::selector::{Selector, StopQuery};
use crate::state::{RegistrationState, StopSet};

/// One clause of an intent: a stop described by what it is.
#[derive(Clone, Debug, PartialEq)]
pub struct IntentItem {
    /// What it asks for: family, footage, pitch class and division.
    pub query: StopQuery,
    /// Human label, e.g. "Grand-Orgue Montre 8'".
    pub label: String,
    /// Weight in the confidence, (0, 1].
    pub importance: f32,
    /// A foundation the sound cannot do without: a miss weighs double.
    pub essential: bool,
}

/// A registration as meaning rather than as stop numbers.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct RegistrationIntent {
    /// The organ it was captured on.
    pub source_organ: String,
    /// Its clauses.
    pub items: Vec<IntentItem>,
}

impl RegistrationIntent {
    /// Describe what is drawn on `organ` semantically.
    pub fn capture(organ: &Organ, state: &RegistrationState) -> Self {
        let items = state
            .stops
            .ids()
            .iter()
            .filter_map(|&id| organ.stop(id))
            .map(|s| {
                let division = organ
                    .division(s.division)
                    .map(|d| d.name.clone())
                    .unwrap_or_default();
                IntentItem {
                    query: StopQuery {
                        family: Some(s.family),
                        footage: Some(s.footage),
                        pitch_class: Some(s.pitch_class),
                        division_name: division.clone(),
                        ..StopQuery::default()
                    },
                    label: format!("{division} {}", s.name),
                    importance: 1.0,
                    essential: s.role == ChorusRole::Foundation,
                }
            })
            .collect();
        Self {
            source_organ: organ.name.clone(),
            items,
        }
    }
}

/// How well a clause found its stop on the target.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MatchTier {
    /// Same family, footage and division (by name or equivalent name).
    Exact,
    /// Same family and footage, another division.
    OtherDivision,
    /// Same family and pitch class (a 4' for a 4', say), nearest available.
    NearestPitch,
    /// Nothing of the kind on this organ.
    Missing,
}

impl MatchTier {
    /// The clause's score in [0, 1].
    pub fn score(self) -> f32 {
        match self {
            Self::Exact => 1.0,
            Self::OtherDivision => 0.75,
            Self::NearestPitch => 0.45,
            Self::Missing => 0.0,
        }
    }
}

/// What one clause became.
#[derive(Clone, Debug, PartialEq)]
pub struct ItemOutcome {
    /// The clause's label.
    pub label: String,
    /// How it matched.
    pub tier: MatchTier,
    /// The stops it drew.
    pub matched: StopSet,
    /// Whether it was essential.
    pub essential: bool,
}

/// An intent carried onto an organ.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct PortResult {
    /// The registration to draw.
    pub resolved: StopSet,
    /// Weighted match quality in [0, 1].
    pub confidence: f32,
    /// One per clause.
    pub outcomes: Vec<ItemOutcome>,
}

impl PortResult {
    /// Whether every essential clause found something.
    pub fn all_essentials_matched(&self) -> bool {
        self.outcomes
            .iter()
            .all(|o| !o.essential || o.tier != MatchTier::Missing)
    }
}

/// Carry `intent` onto `organ`, relaxing each clause until it matches: exact,
/// then any division, then the nearest pitch of the family. A relaxed clause
/// draws one stop, not every candidate -- asking for a Bourdon 8' must not
/// draw all three of an organ's.
pub fn port(intent: &RegistrationIntent, organ: &Organ) -> PortResult {
    let empty = RegistrationState::default();
    let mut r = PortResult::default();
    let (mut score, mut weight) = (0.0f32, 0.0f32);
    for item in &intent.items {
        let q = &item.query;
        let exact = Selector::Atom(q.clone()).resolve(organ, &empty);
        let (tier, matched) = if !exact.is_empty() {
            (MatchTier::Exact, exact)
        } else {
            let any_division = StopQuery {
                division_name: String::new(),
                division: None,
                ..q.clone()
            };
            let nearest = StopQuery {
                footage: None,
                ..any_division.clone()
            };
            let first = |q: StopQuery| {
                Selector::Atom(q)
                    .resolve(organ, &empty)
                    .ids()
                    .first()
                    .copied()
            };
            match (first(any_division), first(nearest)) {
                (Some(s), _) => (MatchTier::OtherDivision, std::iter::once(s).collect()),
                (None, Some(s)) if q.pitch_class.is_some() => {
                    (MatchTier::NearestPitch, std::iter::once(s).collect())
                }
                _ => (MatchTier::Missing, StopSet::new()),
            }
        };
        let w = item.importance * if item.essential { 2.0 } else { 1.0 };
        score += tier.score() * w;
        weight += w;
        r.resolved.unite(&matched);
        r.outcomes.push(ItemOutcome {
            label: item.label.clone(),
            tier,
            matched,
            essential: item.essential,
        });
    }
    r.confidence = if weight > 0.0 { score / weight } else { 1.0 };
    r
}
