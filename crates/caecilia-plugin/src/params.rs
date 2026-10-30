//! What a host sees and automates.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use caecilia_model::{Diagnostic, Organ, demo_organ};
use caecilia_registration::{MASK_CAPACITY, Piston, factory_generals};

use crate::console::{ConsoleMemory, GENERALS};
use crate::switch::{OrganLoadError, OrganSlot, SwitchAudio};
use manguebyte_params::{BoolParam, ChoiceParam, FloatParam, FloatRange, Param, ParamSet};

/// The temperaments a host can choose, in the order of the choice.
pub const TEMPERAMENT_NAMES: [&str; 8] = [
    "Equal",
    "Quarter-comma meantone",
    "Werckmeister III",
    "Kirnberger III",
    "Vallotti",
    "Young",
    "Pythagorean",
    "Just (5-limit)",
];

/// The pitch classes a temperament can be rooted on.
pub const KEY_NAMES: [&str; 12] = [
    "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
];

/// Couplers the host can draw.
pub const MAX_COUPLERS: usize = 16;

/// The parameters. Stops and couplers are pools of a fixed size, because a
/// host fixes the parameter list when it loads a plugin; each is named after
/// the stop it draws on the organ the instance opened with, and the rest are
/// marked unused. Another organ draws through the same pools, by number.
pub struct CaeciliaParams {
    /// Output level, dB.
    pub level: FloatParam,
    /// Reverb blend.
    pub reverb: FloatParam,
    /// The swell shoe of every enclosed division, shut 0 to open 1.
    pub swell: FloatParam,
    /// The tremulant.
    pub tremulant: BoolParam,
    /// Its rate.
    pub tremulant_rate: FloatParam,
    /// Its depth, 0 to 1 of the strongest.
    pub tremulant_depth: FloatParam,
    /// The temperament, from [`TEMPERAMENT_NAMES`].
    pub temperament: ChoiceParam,
    /// The pitch class it is rooted on, from [`KEY_NAMES`].
    pub temperament_key: ChoiceParam,
    /// The pitch of A4, Hz: 440 modern, 415 baroque, 392 French chamber.
    pub pitch: FloatParam,
    /// Drawstops, by stop id.
    pub stops: Vec<BoolParam>,
    /// Couplers, by coupler id.
    pub couplers: Vec<BoolParam>,
    /// General pistons and MIDI-learned controls, saved with the session.
    pub console: ConsoleMemory,
    /// The organ, and the way to change it; its file is saved with the
    /// session.
    pub organ: OrganSlot,
}

/// The parameter id of stop `n`. Stable across versions: a host keys its
/// automation on it.
pub fn stop_param_id(n: usize) -> String {
    format!("stop-{n:02}")
}

/// The parameter id of coupler `n`.
pub fn coupler_param_id(n: usize) -> String {
    format!("coupler-{n:02}")
}

impl CaeciliaParams {
    /// The parameters for `organ` (read from `path`, if it was), drawn as it
    /// opens, and the audio thread's end of the organ slot.
    pub(crate) fn new(organ: Arc<Organ>, path: Option<PathBuf>) -> (Self, SwitchAudio) {
        let opening = organ.default_opening_registration(organ.primary_manual());
        let stops = (0..MASK_CAPACITY)
            .map(|n| {
                let (name, on) = match organ.stops().get(n) {
                    Some(s) => {
                        let division = organ.division(s.division).map_or("", |d| d.name.as_str());
                        (format!("{division} {}", s.name), opening.contains(&s.id))
                    }
                    None => (format!("Stop {n:02} (unused)"), false),
                };
                BoolParam::new(&stop_param_id(n), &name, on)
            })
            .collect();
        let couplers = (0..MAX_COUPLERS)
            .map(|n| {
                let name = organ
                    .couplers()
                    .get(n)
                    .map_or_else(|| format!("Coupler {n:02} (unused)"), |c| c.name.clone());
                BoolParam::new(&coupler_param_id(n), &name, false)
            })
            .collect();
        let console = ConsoleMemory::default();
        set_factory_generals(&console, &organ);
        let (organ, audio) = OrganSlot::new(organ, path);
        let params = Self {
            level: FloatParam::new("level", "Level", FloatRange::linear(-40.0, 6.0), 0.0)
                .with_unit("dB")
                .with_precision(1),
            reverb: FloatParam::new("reverb", "Reverb", FloatRange::linear(0.0, 1.0), 0.25),
            swell: FloatParam::new("swell", "Swell", FloatRange::linear(0.0, 1.0), 1.0),
            tremulant: BoolParam::new("tremulant", "Tremulant", false),
            tremulant_rate: FloatParam::new(
                "tremulant-rate",
                "Tremulant rate",
                FloatRange::linear(3.0, 9.0),
                5.5,
            )
            .with_unit("Hz")
            .with_precision(2),
            tremulant_depth: FloatParam::new(
                "tremulant-depth",
                "Tremulant depth",
                FloatRange::linear(0.0, 1.0),
                0.5,
            ),
            temperament: ChoiceParam::new("temperament", "Temperament", &TEMPERAMENT_NAMES, 0),
            temperament_key: ChoiceParam::new("temperament-key", "Temperament key", &KEY_NAMES, 0),
            pitch: FloatParam::new(
                "pitch",
                "Pitch (A4)",
                FloatRange::linear(380.0, 480.0),
                440.0,
            )
            .with_unit("Hz")
            .with_precision(1),
            stops,
            couplers,
            console,
            organ,
        };
        (params, audio)
    }

    /// Play the organ in `path` from the next block, its opening registration
    /// drawn and its factory generals in the pistons. Keys held go on
    /// sounding on it; the organ being left fades out. Pistons, the sequencer
    /// and cancel keep their learned controls; stops and couplers lose theirs.
    /// Builds the engine here, so never on the audio thread. Returns the
    /// file's warnings; on an error nothing changes.
    pub fn load_organ_file(&self, path: &Path) -> Result<Vec<Diagnostic>, OrganLoadError> {
        let (organ, warnings) = OrganSlot::read(path)?;
        self.take_organ(organ, Some(path), true)?;
        Ok(warnings)
    }

    /// Play `organ`, built in memory, as [`load_organ_file`](Self::load_organ_file) does.
    pub fn use_organ(&self, organ: Organ) -> Result<(), OrganLoadError> {
        self.take_organ(organ, None, true)
    }

    /// Switch, drawing the opening registration or keeping the parameters as
    /// they are (a session restoring its own).
    fn take_organ(
        &self,
        organ: Organ,
        path: Option<&Path>,
        open: bool,
    ) -> Result<(), OrganLoadError> {
        let registration = open.then(|| {
            let opening = organ.default_opening_registration(organ.primary_manual());
            let stops = organ
                .stops()
                .iter()
                .filter(|s| opening.contains(&s.id))
                .fold(0u64, |m, s| m | 1 << s.id.0);
            (stops, 0)
        });
        let generals = ConsoleMemory::default();
        set_factory_generals(&generals, &organ);
        self.organ.switch(organ, path, registration)?;
        for n in 0..GENERALS {
            let (stops, couplers) = generals.general(n).unwrap_or_default();
            self.console.set_general(n, stops, couplers);
        }
        self.console.forget_organ();
        Ok(())
    }

    /// The session's organ, from its saved text: a path, or nothing for the
    /// demonstration organ. One that does not load leaves the organ playing
    /// as it is.
    fn restore_organ(&self, text: &str) {
        let wanted = text.trim();
        let path = (!wanted.is_empty()).then(|| PathBuf::from(wanted));
        let same = match (&path, self.organ.path()) {
            (None, None) => true,
            (Some(p), Some(playing)) => std::path::absolute(p).is_ok_and(|p| p == playing),
            _ => false,
        };
        if same {
            return;
        }
        let result = match &path {
            Some(p) => {
                OrganSlot::read(p).and_then(|(organ, _)| self.take_organ(organ, Some(p), false))
            }
            None => self.take_organ(demo_organ(), None, false),
        };
        if let Err(e) = result {
            eprintln!("Caecilia: the session's organ {wanted} did not load: {e}");
        }
    }

    /// The drawn stops, bit `n` being stop `n`. Real-time safe.
    pub fn stop_mask(&self) -> u64 {
        self.stops
            .iter()
            .enumerate()
            .filter(|(_, p)| p.value())
            .fold(0, |m, (n, _)| m | 1 << n)
    }

    /// The drawn couplers, bit `n` being coupler `n`. Real-time safe.
    pub fn coupler_mask(&self) -> u32 {
        self.couplers
            .iter()
            .enumerate()
            .filter(|(_, p)| p.value())
            .fold(0, |m, (n, _)| m | 1 << n)
    }
}

/// The factory generals in the first pistons, the rest empty.
fn set_factory_generals(console: &ConsoleMemory, organ: &Organ) {
    for n in 0..GENERALS {
        console.set_general(n, 0, 0);
    }
    for (n, (_, piston)) in factory_generals(organ).into_iter().enumerate() {
        if let Piston::Snapshot(state) = piston {
            console.set_general(n, state.stops.to_mask(), 0);
        }
    }
}

impl ParamSet for CaeciliaParams {
    fn visit<'a>(&'a self, visitor: &mut dyn FnMut(&'a dyn Param)) {
        visitor(&self.level);
        visitor(&self.reverb);
        visitor(&self.swell);
        visitor(&self.tremulant);
        visitor(&self.tremulant_rate);
        visitor(&self.tremulant_depth);
        visitor(&self.temperament);
        visitor(&self.temperament_key);
        visitor(&self.pitch);
        for p in &self.stops {
            visitor(p);
        }
        for p in &self.couplers {
            visitor(p);
        }
    }

    fn save_extra(&self, section: &mut dyn FnMut(&str, &str)) {
        // The organ first: restoring it resets the pistons the next section
        // fills.
        let path = self.organ.path();
        section(
            "organ",
            &path.map_or_else(String::new, |p| p.display().to_string()),
        );
        section("generals", &self.console.generals_text());
        section("midi-learn", &self.console.bindings_text());
    }

    fn load_extra(&self, name: &str, text: &str) -> bool {
        match name {
            "organ" => {
                self.restore_organ(text);
                true
            }
            "generals" => self.console.load_generals(text),
            "midi-learn" => self.console.load_bindings(text) || text.trim().is_empty(),
            _ => false,
        }
    }
}
