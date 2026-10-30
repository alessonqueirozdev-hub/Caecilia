//! Pistons, and the generals an organ comes with.

use caecilia_model::Organ;

use crate::command::Command;
use crate::intent::RegistrationIntent;
use crate::parser::parse;
use crate::state::{RegistrationState, StopSet};

/// What a piston recalls: the literal stops, or the meaning (which survives a
/// change of organ).
#[derive(Clone, Debug, PartialEq)]
pub enum Piston {
    /// Exactly these stops and couplers.
    Snapshot(RegistrationState),
    /// This registration's meaning, ported onto whatever organ is loaded.
    Intent(RegistrationIntent),
}

impl Piston {
    /// The command that recalls it.
    pub fn to_command(&self) -> Command {
        match self {
            Self::Snapshot(s) => Command::RecallSnapshot(s.clone()),
            Self::Intent(i) => Command::ApplyIntent(i.clone()),
        }
    }
}

/// A factory general: a legend and a selector in the shared grammar.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FactoryGeneral {
    /// The legend: "pp", "f", "T".
    pub tag: &'static str,
    /// What it draws.
    pub expression: &'static str,
    /// Whether it carries everything below it, as a crescendo does.
    pub cumulative: bool,
}

/// The row of generals every organ gets until it is given its own: a
/// crescendo from the softest flutes to the tutti, then a solo combination
/// (not cumulative: a different gesture, at the end of the row).
pub const FACTORY_GENERALS: [FactoryGeneral; 8] = [
    FactoryGeneral {
        tag: "pp",
        expression: "family:flute & pitch:8 & !div:pedal",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "p",
        expression: "family:string",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "mp",
        expression: "family:principal & pitch:8",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "mf",
        expression: "family:principal & pitch:4 | family:flute & pitch:4",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "f",
        expression: "pitch:2 | family:reed & role:color",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "ff",
        expression: "family:mixture | family:reed",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "T",
        expression: "*",
        cumulative: true,
    },
    FactoryGeneral {
        tag: "So",
        expression: "family:reed | name:cornet",
        cumulative: false,
    },
];

/// The stops an expression draws on `organ`, against an empty registration --
/// a general says what to draw, not what to add to whatever was out.
pub fn resolve_expression(organ: &Organ, expression: &str) -> StopSet {
    parse(expression).map_or_else(
        |_| StopSet::new(),
        |s| s.resolve(organ, &RegistrationState::default()),
    )
}

/// The factory generals resolved on `organ`. A cumulative general carries
/// everything below it, even past one that matched nothing: an organ without
/// strings must not break the pistons above "p".
pub fn factory_generals(organ: &Organ) -> Vec<(FactoryGeneral, Piston)> {
    let mut accumulated = StopSet::new();
    FACTORY_GENERALS
        .iter()
        .map(|g| {
            let own = resolve_expression(organ, g.expression);
            let stops = if g.cumulative {
                accumulated.unite(&own);
                accumulated.clone()
            } else {
                own
            };
            (
                *g,
                Piston::Snapshot(RegistrationState {
                    stops,
                    couplers: Vec::new(),
                }),
            )
        })
        .collect()
}
