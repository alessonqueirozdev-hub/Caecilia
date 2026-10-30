//! The registration brain behind the host parameters.

use std::path::Path;
use std::sync::Arc;

use caecilia_model::{CouplerId, Diagnostic, Organ, StopId};
use caecilia_registration::{
    Command, Explanation, ParseError, RegistrationEngine, RegistrationState, StateDelta, StopSet,
    parse,
};
use manguebyte_core::spsc::{Consumer, Producer};
use manguebyte_dsp::Tuning;
use manguebyte_dsp::tuning::{KeyboardMapping, ScalaScale, Scale, TuningSettings};

use crate::console::{Action, Binding};
use crate::params::{CaeciliaParams, MAX_COUPLERS};
use crate::switch::OrganLoadError;

/// Who changed the registration on the audio side.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Origin {
    /// The host: automation, a session load, its generic editor.
    Host,
    /// A general piston, by program change or a learned control.
    General(u16),
    /// A learned control that draws or retires one stop or coupler.
    Control,
}

/// A registration the audio thread is now playing, sent back so the history
/// knows about it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Played {
    /// Drawn stops.
    pub stops: u64,
    /// Drawn couplers.
    pub couplers: u32,
    /// Who drew them.
    pub origin: Origin,
    /// On which organ: the slot's generation when it was played.
    pub generation: u32,
}

pub(crate) fn state_of(stops: u64, couplers: u32) -> RegistrationState {
    RegistrationState {
        stops: StopSet::from_mask(stops),
        couplers: (0..MAX_COUPLERS as u16)
            .filter(|b| couplers & (1 << b) != 0)
            .map(CouplerId)
            .collect(),
    }
}

fn coupler_mask(state: &RegistrationState) -> u32 {
    state
        .couplers
        .iter()
        .filter(|c| c.index() < MAX_COUPLERS)
        .fold(0, |m, c| m | 1 << c.0)
}

/// Selectors, commands, undo and redo, `explain`, general pistons and MIDI
/// learn, for an editor, a control surface or a script. Lives off the audio
/// thread, behind a mutex; what it decides reaches the audio thread by
/// writing the host parameters, which the host is then told about.
pub struct RegistrationController {
    engine: RegistrationEngine,
    params: Arc<CaeciliaParams>,
    played: Consumer<Played>,
    tunings: Producer<Tuning>,
    /// The organ the history is about.
    generation: u32,
}

impl RegistrationController {
    pub(crate) fn new(
        organ: Arc<Organ>,
        params: Arc<CaeciliaParams>,
        played: Consumer<Played>,
        tunings: Producer<Tuning>,
    ) -> Self {
        let initial = state_of(params.stop_mask(), params.coupler_mask());
        Self {
            engine: RegistrationEngine::new(organ, initial),
            generation: params.organ.generation(),
            params,
            played,
            tunings,
        }
    }

    /// Play the organ in `path`, as
    /// [`CaeciliaParams::load_organ_file`] does; the history starts again
    /// from its opening registration. Returns the file's warnings.
    pub fn load_organ(&mut self, path: &Path) -> Result<Vec<Diagnostic>, OrganLoadError> {
        let warnings = self.params.load_organ_file(path)?;
        self.sync();
        Ok(warnings)
    }

    /// Play `organ`, built in memory.
    pub fn use_organ(&mut self, organ: Organ) -> Result<(), OrganLoadError> {
        self.params.use_organ(organ)?;
        self.sync();
        Ok(())
    }

    /// The organ the registration is about.
    pub fn organ(&self) -> Arc<Organ> {
        self.params.organ.organ()
    }

    /// Play in any tuning the framework can build -- an equal division of any
    /// period, a twelve-note table, a Scala scale through its keyboard
    /// mapping -- from the next note on. It holds until the temperament, key
    /// or pitch parameter moves. Returns false if the audio thread has not
    /// taken the last one yet.
    pub fn use_tuning(&mut self, settings: &TuningSettings) -> Result<bool, String> {
        let tuning = settings.build().map_err(|e| e.to_string())?;
        Ok(self.tunings.push(tuning).is_ok())
    }

    /// Play a Scala scale (`.scl` text), through a keyboard mapping (`.kbm`
    /// text) if given, else linearly from middle C with A4 at 440.
    pub fn use_scala(&mut self, scl: &str, kbm: Option<&str>) -> Result<bool, String> {
        let scale = ScalaScale::parse(scl).map_err(|e| e.to_string())?;
        let mapping = kbm
            .map(KeyboardMapping::parse)
            .transpose()
            .map_err(|e| e.to_string())?;
        self.use_tuning(&TuningSettings {
            scale: Scale::Scala { scale, mapping },
            ..TuningSettings::default()
        })
    }

    /// Take into the history what the audio thread played since the last call:
    /// the host's automation, pistons, learned controls. Each becomes one
    /// history node, so it can be undone like anything drawn here.
    /// An organ changed from anywhere (a session, the standalone, a call
    /// here) starts the history again, and what the audio thread played on
    /// the organ before it is ignored.
    pub fn sync(&mut self) {
        self.params.organ.collect();
        let (organ, generation, drawn) = self.params.organ.record();
        if generation != self.generation {
            self.generation = generation;
            let (stops, couplers) =
                drawn.unwrap_or_else(|| (self.params.stop_mask(), self.params.coupler_mask()));
            self.engine = RegistrationEngine::new(organ, state_of(stops, couplers));
        }
        while let Some(p) = self.played.pop() {
            if p.generation != self.generation {
                continue;
            }
            let state = state_of(p.stops, p.couplers);
            if state != *self.engine.state() {
                self.engine.apply(Command::RecallSnapshot(state));
            }
        }
    }

    fn write(&self) {
        let state = self.engine.state();
        let stops = state.stops.to_mask();
        for (n, p) in self.params.stops.iter().enumerate() {
            p.set(stops & (1 << n) != 0);
        }
        let couplers = coupler_mask(state);
        for (n, p) in self.params.couplers.iter().enumerate() {
            p.set(couplers & (1 << n) != 0);
        }
    }

    /// Apply a command; the audio thread plays it from its next block.
    pub fn apply(&mut self, command: Command) -> StateDelta {
        self.sync();
        let delta = self.engine.apply(command);
        self.write();
        delta
    }

    /// Draw every stop a selector matches, given as text in the shared grammar.
    pub fn engage(&mut self, selector: &str) -> Result<StateDelta, ParseError> {
        Ok(self.apply(Command::EngageSelector(parse(selector)?)))
    }

    /// Step back.
    pub fn undo(&mut self) -> StateDelta {
        self.sync();
        let d = self.engine.undo();
        self.write();
        d
    }

    /// Step forward.
    pub fn redo(&mut self) -> StateDelta {
        self.sync();
        let d = self.engine.redo();
        self.write();
        d
    }

    /// What is drawn.
    pub fn state(&mut self) -> RegistrationState {
        self.sync();
        self.engine.state().clone()
    }

    /// Why a stop is drawn.
    pub fn explain(&mut self, stop: StopId) -> Explanation {
        self.sync();
        self.engine.explain(stop)
    }

    /// The registration engine itself, for queries and its history.
    pub fn engine(&mut self) -> &RegistrationEngine {
        self.sync();
        &self.engine
    }

    /// Store what is drawn in general piston `n`.
    pub fn capture_general(&mut self, n: usize) {
        self.sync();
        let state = self.engine.state();
        self.params
            .console
            .set_general(n, state.stops.to_mask(), coupler_mask(state));
    }

    /// Recall general `n` from here (the audio thread does the same for a
    /// program change or a learned piston).
    pub fn recall_general(&mut self, n: usize) -> Option<StateDelta> {
        let (stops, couplers) = self.params.console.general(n)?;
        self.params.console.set_current_general(n);
        Some(self.apply(Command::RecallSnapshot(state_of(stops, couplers))))
    }

    /// Wait for the next control the organist moves, and bind it to `action`.
    pub fn learn(&self, action: Action) {
        self.params.console.arm(action);
    }

    /// Stop waiting.
    pub fn cancel_learn(&self) {
        self.params.console.disarm();
    }

    /// Every learned control.
    pub fn bindings(&self) -> Vec<Binding> {
        self.params.console.bindings()
    }

    /// Forget every learned control.
    pub fn clear_bindings(&self) {
        self.params.console.clear_bindings();
    }
}
