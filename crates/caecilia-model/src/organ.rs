//! The compiled instrument.
//!
//! Every element's id is its index in the organ's arrays. The organ is built
//! off the audio thread and read-only afterwards, so every query here is safe
//! to make from anywhere.

use crate::footage::Footage;
use crate::ids::{
    ChorusRole, CouplerId, DivisionId, MidiNote, PitchClass, RankId, StopId, TonalFamily,
    WindchestId,
};
use crate::spectrum::SpectralModel;

/// A rank's voicing character, as the organ file states it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RankCharacter {
    /// Strength of the attack chiff, 0..1.
    pub chiff: f32,
    /// Upper-harmonic richness, 0..1.
    pub harmonic_development: f32,
    /// Spectral tilt, 0..1.
    pub brightness: f32,
    /// How strongly the rank responds to the wind, 0..1.
    pub wind_sensitivity: f32,
    /// Largest per-pipe detune, in cents.
    pub detune_scatter_cents: f32,
    /// Largest per-pipe level trim, in dB.
    pub level_scatter_db: f32,
    /// Largest per-pipe brightness deviation.
    pub brightness_scatter: f32,
    /// Largest per-pipe attack jitter, in ms.
    pub attack_scatter_ms: f32,
}

impl Default for RankCharacter {
    fn default() -> Self {
        Self {
            chiff: 0.0,
            harmonic_development: 0.5,
            brightness: 0.5,
            wind_sensitivity: 0.5,
            detune_scatter_cents: 1.0,
            level_scatter_db: 0.5,
            brightness_scatter: 0.05,
            attack_scatter_ms: 2.0,
        }
    }
}

/// A wind supply and the ranks standing on it.
#[derive(Clone, Debug, PartialEq)]
pub struct Windchest {
    /// Its id.
    pub id: WindchestId,
    /// Human name.
    pub name: String,
    /// Regulated pressure, in pascals.
    pub nominal_pressure_pa: f32,
    /// Whether a tremulant shakes this chest.
    pub has_tremulant: bool,
    /// The ranks it feeds.
    pub ranks: Vec<RankId>,
}

/// Whether a division is played by hand or by foot.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum DivisionKind {
    /// A manual.
    #[default]
    Manual,
    /// The pedalboard.
    Pedal,
}

/// A keyboard section and its stops.
#[derive(Clone, Debug, PartialEq)]
pub struct Division {
    /// Its id.
    pub id: DivisionId,
    /// Human name.
    pub name: String,
    /// Manual or pedal.
    pub kind: DivisionKind,
    /// Lowest key.
    pub low_note: MidiNote,
    /// Highest key.
    pub high_note: MidiNote,
    /// Whether it stands in a swell box.
    pub enclosed: bool,
    /// Whether it has a tremulant.
    pub has_tremulant: bool,
    /// The chests it draws from.
    pub windchests: Vec<WindchestId>,
    /// Its stops.
    pub stops: Vec<StopId>,
}

impl Division {
    /// Whether `note` is on this division's keyboard.
    pub fn contains(&self, note: MidiNote) -> bool {
        (self.low_note..=self.high_note).contains(&note)
    }
}

/// A homogeneous set of pipes.
#[derive(Clone, Debug, PartialEq)]
pub struct Rank {
    /// Its id.
    pub id: RankId,
    /// Human name.
    pub name: String,
    /// Tonal family.
    pub family: TonalFamily,
    /// Length.
    pub footage: Footage,
    /// Stopped (no even harmonics) or open.
    pub stopped: bool,
    /// The chest it stands on.
    pub windchest: WindchestId,
    /// The division whose stop draws it.
    pub division: DivisionId,
    /// Lowest pipe.
    pub low_note: MidiNote,
    /// Highest pipe.
    pub high_note: MidiNote,
    /// Voicing character.
    pub character: RankCharacter,
    /// A measured spectrum, when the organ file names one.
    pub measured_spectrum: Option<SpectralModel>,
}

/// A drawstop: the semantic identity a registration reasons about.
#[derive(Clone, Debug, PartialEq)]
pub struct Stop {
    /// Its id.
    pub id: StopId,
    /// Human name.
    pub name: String,
    /// Tonal family.
    pub family: TonalFamily,
    /// Length.
    pub footage: Footage,
    /// Pitch class.
    pub pitch_class: PitchClass,
    /// Role in a registration.
    pub role: ChorusRole,
    /// The division it belongs to.
    pub division: DivisionId,
    /// The rank it draws.
    pub rank: RankId,
    /// A mixture's constituent ranks; empty for a simple stop.
    pub mixture: Vec<Footage>,
}

impl Stop {
    /// Whether this stop draws several ranks at once.
    pub fn is_compound(&self) -> bool {
        !self.mixture.is_empty()
    }

    /// Whether this stop sounds a non-octave harmonic.
    pub fn is_mutation(&self) -> bool {
        self.footage.is_mutation()
    }
}

/// A key route from one division to another, optionally transposed.
#[derive(Clone, Debug, PartialEq)]
pub struct Coupler {
    /// Its id.
    pub id: CouplerId,
    /// Human name.
    pub name: String,
    /// The division whose ranks are borrowed.
    pub from: DivisionId,
    /// The division whose keys borrow them.
    pub to: DivisionId,
    /// Transposition in semitones: 0 unison, +12 super, -12 sub.
    pub semitones: i32,
}

/// A physical keyboard.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Manual {
    /// The division it plays.
    pub division: DivisionId,
    /// Stacking order from the bottom.
    pub index: i32,
    /// 0-based MIDI channel, or -1.
    pub midi_channel: i32,
    /// Lowest key.
    pub low_note: MidiNote,
    /// Highest key.
    pub high_note: MidiNote,
}

/// The compiled, immutable instrument.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Organ {
    /// Name.
    pub name: String,
    /// Builder.
    pub builder: String,
    /// Year.
    pub year: i32,
    pub(crate) windchests: Vec<Windchest>,
    pub(crate) ranks: Vec<Rank>,
    pub(crate) stops: Vec<Stop>,
    pub(crate) divisions: Vec<Division>,
    pub(crate) manuals: Vec<Manual>,
    pub(crate) couplers: Vec<Coupler>,
}

impl Organ {
    /// Every windchest, by id.
    pub fn windchests(&self) -> &[Windchest] {
        &self.windchests
    }
    /// Every rank, by id.
    pub fn ranks(&self) -> &[Rank] {
        &self.ranks
    }
    /// Every stop, by id.
    pub fn stops(&self) -> &[Stop] {
        &self.stops
    }
    /// Every division, by id.
    pub fn divisions(&self) -> &[Division] {
        &self.divisions
    }
    /// The keyboards.
    pub fn manuals(&self) -> &[Manual] {
        &self.manuals
    }
    /// Every coupler, by id.
    pub fn couplers(&self) -> &[Coupler] {
        &self.couplers
    }
    /// A stop by id.
    pub fn stop(&self, id: StopId) -> Option<&Stop> {
        self.stops.get(id.index())
    }
    /// A rank by id.
    pub fn rank(&self, id: RankId) -> Option<&Rank> {
        self.ranks.get(id.index())
    }
    /// A division by id.
    pub fn division(&self, id: DivisionId) -> Option<&Division> {
        self.divisions.get(id.index())
    }
    /// A windchest by id.
    pub fn windchest(&self, id: WindchestId) -> Option<&Windchest> {
        self.windchests.get(id.index())
    }

    /// The division a single-keyboard controller should play: the manual with
    /// the most stops (ties to the lowest id). Never the pedal -- a manual
    /// keyboard routed to a thirty-note pedalboard is silently wrong.
    pub fn primary_manual(&self) -> DivisionId {
        let mut best: Option<&Division> = None;
        for d in self
            .divisions
            .iter()
            .filter(|d| d.kind != DivisionKind::Pedal)
        {
            if best.is_none_or(|b| d.stops.len() > b.stops.len()) {
                best = Some(d);
            }
        }
        best.or(self.divisions.first())
            .map(|d| d.id)
            .unwrap_or_default()
    }

    /// The registration the instrument opens on: the principal chorus of
    /// `manual` with its mixtures and an 8' flute; failing that, the first 8'
    /// anywhere. An instrument that comes up silent reads as broken.
    pub fn default_opening_registration(&self, manual: DivisionId) -> Vec<StopId> {
        let mut drawn: Vec<StopId> = self
            .stops
            .iter()
            .filter(|s| s.division == manual)
            .filter(|s| {
                matches!(s.family, TonalFamily::Principal | TonalFamily::Mixture)
                    || (s.family == TonalFamily::Flute && s.footage == Footage::EIGHT)
            })
            .map(|s| s.id)
            .collect();
        if drawn.is_empty() {
            drawn.extend(
                self.stops
                    .iter()
                    .find(|s| s.footage == Footage::EIGHT)
                    .map(|s| s.id),
            );
        }
        drawn.sort();
        drawn
    }
}
