// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The queryable registration.
//!
//! Stops selected by what they are (`family:reed & pitch:8 & div:swell`),
//! combined with set algebra; every change a [`Command`] folded into state
//! through a branching history; `explain` answers why a stop is drawn;
//! `build_plenum` knows organ-building convention; and an intent captured on
//! one organ ports onto another with a graded confidence.
//!
//! None of this runs on the audio thread. What crosses to it is the drawn set
//! as a mask ([`StopSet::to_mask`]) or a [`StateDelta`].

mod command;
mod engine;
mod generals;
mod intent;
mod parser;
mod selector;
mod state;

pub use command::{Command, PlenumSpec, build_plenum};
pub use engine::{Explanation, HistoryNode, Reason, RegistrationEngine};
pub use generals::{
    FACTORY_GENERALS, FactoryGeneral, Piston, factory_generals, resolve_expression,
};
pub use intent::{IntentItem, ItemOutcome, MatchTier, PortResult, RegistrationIntent, port};
pub use parser::{GRAMMAR_VERSION, ParseError, parse};
pub use selector::{Engaged, Selector, StopQuery, division_named};
pub use state::{MASK_CAPACITY, RegistrationState, StateDelta, StopSet};
