// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The registration brain: commands folded into state, a branching history,
//! and why every stop is drawn.

use std::collections::BTreeMap;
use std::sync::Arc;

use caecilia_model::{Organ, StopId, TonalFamily};

use crate::command::Command;
use crate::intent::{PortResult, RegistrationIntent, port};
use crate::selector::Selector;
use crate::state::{RegistrationState, StateDelta, StopSet};

/// Why a stop is in its state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reason {
    /// It is not drawn.
    NotEngaged,
    /// Drawn directly.
    Direct,
    /// Drawn by a selector.
    SelectorMatch,
    /// The principal chorus of a build-plenum.
    PlenumPrincipal,
    /// The mixture crown of a build-plenum.
    PlenumMixture,
    /// A chorus reed of a build-plenum.
    PlenumReed,
    /// Ported from an intent captured on another organ.
    IntentPort,
    /// Restored by a snapshot or piston.
    SnapshotRecall,
}

/// Why a stop is drawn, and which command did it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Explanation {
    /// The stop.
    pub stop: StopId,
    /// Why.
    pub reason: Reason,
    /// The history node of the command that caused it.
    pub node: usize,
}

type Provenance = BTreeMap<StopId, (Reason, usize)>;

/// One node of the history: the command, the state it made, and the
/// provenance in force there -- so an undo restores the "why" along with the
/// "what" (1.x rebuilt it conservatively and lost it).
#[derive(Clone, Debug)]
pub struct HistoryNode {
    /// The command that made this node; `None` at the root.
    pub command: Option<Command>,
    /// The state it made.
    pub state: RegistrationState,
    /// Its parent; `None` at the root.
    pub parent: Option<usize>,
    /// Branches explored from here.
    pub children: Vec<usize>,
    /// The branch redo follows: the most recent.
    pub last_child: Option<usize>,
    provenance: Provenance,
}

/// Command-sourced, branching undo and redo. Undoing then doing something new
/// forks a sibling branch; nothing an organist auditioned is ever thrown away.
#[derive(Debug)]
pub struct RegistrationEngine {
    organ: Arc<Organ>,
    nodes: Vec<HistoryNode>,
    current: usize,
}

impl RegistrationEngine {
    /// An engine for `organ`, starting from `initial`.
    pub fn new(organ: Arc<Organ>, initial: RegistrationState) -> Self {
        let provenance = initial
            .stops
            .ids()
            .iter()
            .map(|&s| (s, (Reason::SnapshotRecall, 0)))
            .collect();
        Self {
            organ,
            nodes: vec![HistoryNode {
                command: None,
                state: initial,
                parent: None,
                children: Vec::new(),
                last_child: None,
                provenance,
            }],
            current: 0,
        }
    }

    /// The organ.
    pub fn organ(&self) -> &Organ {
        &self.organ
    }

    /// What is drawn.
    pub fn state(&self) -> &RegistrationState {
        &self.nodes[self.current].state
    }

    /// The history's nodes.
    pub fn history(&self) -> &[HistoryNode] {
        &self.nodes
    }

    /// The node the state is at.
    pub fn current_node(&self) -> usize {
        self.current
    }

    /// Whether there is something to undo.
    pub fn can_undo(&self) -> bool {
        self.nodes[self.current].parent.is_some()
    }

    /// Whether there is something to redo.
    pub fn can_redo(&self) -> bool {
        self.nodes[self.current].last_child.is_some()
    }

    fn reason_for(&self, command: &Command, stop: StopId) -> Reason {
        match command {
            Command::BuildPlenum(_) => match self.organ.stop(stop) {
                Some(s) if s.family == TonalFamily::Mixture || s.is_compound() => {
                    Reason::PlenumMixture
                }
                Some(s) if s.family == TonalFamily::Reed => Reason::PlenumReed,
                _ => Reason::PlenumPrincipal,
            },
            Command::EngageSelector(_) | Command::ToggleSelector(_) | Command::Solo(..) => {
                Reason::SelectorMatch
            }
            Command::ApplyIntent(_) => Reason::IntentPort,
            Command::RecallSnapshot(_) => Reason::SnapshotRecall,
            _ => Reason::Direct,
        }
    }

    /// Apply a command: a new history node, and what the audio side must change.
    pub fn apply(&mut self, command: Command) -> StateDelta {
        let before = self.state().clone();
        let after = command.apply_to(&self.organ, &before);
        let index = self.nodes.len();
        let mut provenance = self.nodes[self.current].provenance.clone();
        provenance.retain(|s, _| after.is_engaged(*s));
        for &s in after.stops.ids() {
            if !before.is_engaged(s) || !provenance.contains_key(&s) {
                provenance.insert(s, (self.reason_for(&command, s), index));
            }
        }
        let parent = self.current;
        self.nodes.push(HistoryNode {
            command: Some(command),
            state: after,
            parent: Some(parent),
            children: Vec::new(),
            last_child: None,
            provenance,
        });
        self.nodes[parent].children.push(index);
        self.nodes[parent].last_child = Some(index);
        self.current = index;
        StateDelta::between(&before, self.state())
    }

    /// Step back to the parent node.
    pub fn undo(&mut self) -> StateDelta {
        let before = self.state().clone();
        if let Some(p) = self.nodes[self.current].parent {
            self.current = p;
        }
        StateDelta::between(&before, self.state())
    }

    /// Step forward along the most recent branch.
    pub fn redo(&mut self) -> StateDelta {
        let before = self.state().clone();
        if let Some(c) = self.nodes[self.current].last_child {
            self.current = c;
        }
        StateDelta::between(&before, self.state())
    }

    /// The stops a selector picks out now.
    pub fn query(&self, selector: &Selector) -> StopSet {
        selector.resolve(&self.organ, self.state())
    }

    /// Why `stop` is (or is not) drawn.
    pub fn explain(&self, stop: StopId) -> Explanation {
        if !self.state().is_engaged(stop) {
            return Explanation {
                stop,
                reason: Reason::NotEngaged,
                node: self.current,
            };
        }
        let (reason, node) = self.nodes[self.current]
            .provenance
            .get(&stop)
            .copied()
            .unwrap_or((Reason::Direct, self.current));
        Explanation { stop, reason, node }
    }

    /// What is drawn, as meaning.
    pub fn capture_intent(&self) -> RegistrationIntent {
        RegistrationIntent::capture(&self.organ, self.state())
    }

    /// How an intent would land on this organ, without applying it.
    pub fn port(&self, intent: &RegistrationIntent) -> PortResult {
        port(intent, &self.organ)
    }
}
