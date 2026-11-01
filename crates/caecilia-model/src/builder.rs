// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Assembling an organ.

use crate::footage::Footage;
use crate::ids::{
    ChorusRole, CouplerId, DivisionId, MidiNote, PitchClass, RankId, StopId, TonalFamily,
    WindchestId,
};
use crate::organ::{
    Coupler, Division, DivisionKind, Manual, Organ, Rank, RankCharacter, RecipeOverride, Stop,
    Windchest,
};

impl RankCharacter {
    /// The character a family is given when nothing more specific is said.
    pub fn for_family(family: TonalFamily) -> Self {
        let mut v = Self::default();
        let (chiff, development, brightness, wind) = match family {
            TonalFamily::Principal => (0.15, 0.65, 0.60, 0.45),
            TonalFamily::Flute => (0.25, 0.30, 0.40, 0.35),
            TonalFamily::String => (0.10, 0.85, 0.80, 0.55),
            TonalFamily::Reed => (0.35, 0.90, 0.75, 0.60),
            TonalFamily::Mixture => (0.20, 0.95, 0.90, 0.50),
            _ => return v,
        };
        v.chiff = chiff;
        v.harmonic_development = development;
        v.brightness = brightness;
        v.wind_sensitivity = wind;
        match family {
            TonalFamily::String => v.detune_scatter_cents = 1.5,
            TonalFamily::Mixture => v.detune_scatter_cents = 2.0,
            _ => {}
        }
        v
    }
}

/// One stop and the rank it draws.
#[derive(Clone, Debug)]
pub struct StopSpec<'a> {
    /// Name, shared by the stop and its rank.
    pub name: &'a str,
    /// Tonal family.
    pub family: TonalFamily,
    /// Length.
    pub footage: Footage,
    /// Pitch class.
    pub pitch_class: PitchClass,
    /// Role in a registration.
    pub role: ChorusRole,
    /// A mixture's ranks; empty for a simple stop.
    pub mixture: &'a [Footage],
    /// Stopped (no even harmonics) or open.
    pub stopped: bool,
    /// Voicing character; [`RankCharacter::for_family`] if unsure.
    pub character: RankCharacter,
    /// The rank's own recipe parameters, if it has a voice of its own.
    pub recipe: Option<RecipeOverride>,
}

impl<'a> StopSpec<'a> {
    /// A stopped rank of `family` with its family's character.
    pub fn new(
        name: &'a str,
        family: TonalFamily,
        footage: Footage,
        pitch_class: PitchClass,
        role: ChorusRole,
    ) -> Self {
        Self {
            name,
            family,
            footage,
            pitch_class,
            role,
            mixture: &[],
            stopped: true,
            character: RankCharacter::for_family(family),
            recipe: None,
        }
    }
}

/// Builds an [`Organ`], assigning dense ids in the order things are added.
#[derive(Debug)]
pub struct OrganBuilder {
    organ: Organ,
}

impl OrganBuilder {
    /// An empty instrument.
    pub fn new(name: &str, builder: &str, year: i32) -> Self {
        Self {
            organ: Organ {
                name: name.into(),
                builder: builder.into(),
                year,
                ..Organ::default()
            },
        }
    }

    /// Add a windchest.
    pub fn chest(&mut self, name: &str, pressure_pa: f32, tremulant: bool) -> WindchestId {
        let id = WindchestId(self.organ.windchests.len() as u16);
        self.organ.windchests.push(Windchest {
            id,
            name: name.into(),
            nominal_pressure_pa: pressure_pa,
            has_tremulant: tremulant,
            ranks: Vec::new(),
        });
        id
    }

    /// Add a division on `chest`.
    #[allow(clippy::too_many_arguments)]
    pub fn division(
        &mut self,
        name: &str,
        kind: DivisionKind,
        low: MidiNote,
        high: MidiNote,
        enclosed: bool,
        tremulant: bool,
        chest: WindchestId,
    ) -> DivisionId {
        let id = DivisionId(self.organ.divisions.len() as u16);
        self.organ.divisions.push(Division {
            id,
            name: name.into(),
            kind,
            low_note: low,
            high_note: high,
            enclosed,
            has_tremulant: tremulant,
            windchests: vec![chest],
            stops: Vec::new(),
        });
        id
    }

    /// Add a stop and its rank to `division`, on the division's first chest
    /// and with its compass.
    pub fn stop(&mut self, division: DivisionId, spec: StopSpec<'_>) -> StopId {
        let rank_id = RankId(self.organ.ranks.len() as u16);
        let stop_id = StopId(self.organ.stops.len() as u16);
        let d = &self.organ.divisions[division.index()];
        let (chest, low, high) = (d.windchests[0], d.low_note, d.high_note);
        self.organ.ranks.push(Rank {
            id: rank_id,
            name: spec.name.into(),
            family: spec.family,
            footage: spec.footage,
            stopped: spec.stopped,
            windchest: chest,
            division,
            low_note: low,
            high_note: high,
            character: spec.character,
            measured_spectrum: None,
            spectrum_path: None,
            pan: 0.0,
            distance_m: 8.0,
            recipe: spec.recipe,
        });
        self.organ.windchests[chest.index()].ranks.push(rank_id);
        self.organ.stops.push(Stop {
            id: stop_id,
            name: spec.name.into(),
            family: spec.family,
            footage: spec.footage,
            pitch_class: spec.pitch_class,
            role: spec.role,
            division,
            rank: rank_id,
            mixture: spec.mixture.to_vec(),
        });
        self.organ.divisions[division.index()].stops.push(stop_id);
        stop_id
    }

    /// Add a coupler.
    pub fn coupler(&mut self, name: &str, from: DivisionId, to: DivisionId, semitones: i32) {
        let id = CouplerId(self.organ.couplers.len() as u16);
        self.organ.couplers.push(Coupler {
            id,
            name: name.into(),
            from,
            to,
            semitones,
        });
    }

    /// Add a keyboard.
    pub fn manual(&mut self, manual: Manual) {
        self.organ.manuals.push(manual);
    }

    /// The finished instrument.
    pub fn build(self) -> Organ {
        self.organ
    }
}
