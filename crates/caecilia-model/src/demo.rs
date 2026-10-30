//! The Caecilia demonstration organ: three divisions, 26 stops, one rank per
//! stop, one chest per division, three couplers.

use crate::footage::Footage;
use crate::ids::{
    ChorusRole, CouplerId, DivisionId, MidiNote, PitchClass, RankId, StopId, TonalFamily,
    WindchestId,
};
use crate::organ::{
    Coupler, Division, DivisionKind, Manual, Organ, Rank, RankCharacter, Stop, Windchest,
};

/// The voicing character the demo gives each family.
fn character_for(family: TonalFamily) -> RankCharacter {
    let mut v = RankCharacter::default();
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

struct Builder {
    organ: Organ,
}

impl Builder {
    fn chest(&mut self, name: &str, pressure_pa: f32, tremulant: bool) -> WindchestId {
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

    #[allow(clippy::too_many_arguments)]
    fn division(
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

    #[allow(clippy::too_many_arguments)]
    fn stop(
        &mut self,
        division: DivisionId,
        name: &str,
        family: TonalFamily,
        footage: Footage,
        pitch_class: PitchClass,
        role: ChorusRole,
        mixture: &[Footage],
        stopped: bool,
    ) -> StopId {
        let rank_id = RankId(self.organ.ranks.len() as u16);
        let stop_id = StopId(self.organ.stops.len() as u16);
        let d = &self.organ.divisions[division.index()];
        let (chest, low, high) = (d.windchests[0], d.low_note, d.high_note);
        self.organ.ranks.push(Rank {
            id: rank_id,
            name: name.into(),
            family,
            footage,
            stopped,
            windchest: chest,
            division,
            low_note: low,
            high_note: high,
            character: character_for(family),
            measured_spectrum: None,
        });
        self.organ.windchests[chest.index()].ranks.push(rank_id);
        self.organ.stops.push(Stop {
            id: stop_id,
            name: name.into(),
            family,
            footage,
            pitch_class,
            role,
            division,
            rank: rank_id,
            mixture: mixture.to_vec(),
        });
        self.organ.divisions[division.index()].stops.push(stop_id);
        stop_id
    }

    fn coupler(&mut self, name: &str, from: DivisionId, to: DivisionId, semitones: i32) {
        let id = CouplerId(self.organ.couplers.len() as u16);
        self.organ.couplers.push(Coupler {
            id,
            name: name.into(),
            from,
            to,
            semitones,
        });
    }
}

/// Build the demonstration organ.
pub fn demo_organ() -> Organ {
    use ChorusRole as R;
    use Footage as F;
    use PitchClass as P;
    use TonalFamily as T;

    let mut b = Builder {
        organ: Organ {
            name: "Caecilia Demonstration Organ".into(),
            builder: "Alesson Queiroz".into(),
            year: 2026,
            ..Organ::default()
        },
    };

    let chest_pedal = b.chest("Pédale chest", 980.0, false);
    let chest_go = b.chest("Grand-Orgue chest", 812.0, false);
    let chest_recit = b.chest("Récit chest", 735.0, true);

    let pedal = b.division(
        "Pédale",
        DivisionKind::Pedal,
        36,
        67,
        false,
        false,
        chest_pedal,
    );
    let go = b.division(
        "Grand-Orgue",
        DivisionKind::Manual,
        36,
        96,
        false,
        false,
        chest_go,
    );
    let recit = b.division(
        "Récit",
        DivisionKind::Manual,
        36,
        96,
        true,
        true,
        chest_recit,
    );

    // Every stop is stopped (a bourdon) unless said otherwise: only flutes read it.
    let s = |b: &mut Builder, d, n, t, f, p, r| b.stop(d, n, t, f, p, r, &[], true);

    // Pédale.
    s(
        &mut b,
        pedal,
        "Contrebasse 16",
        T::Principal,
        F::SIXTEEN,
        P::Sub,
        R::Foundation,
    );
    s(
        &mut b,
        pedal,
        "Soubasse 16",
        T::Flute,
        F::SIXTEEN,
        P::Sub,
        R::Foundation,
    );
    s(
        &mut b,
        pedal,
        "Octavebasse 8",
        T::Principal,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        pedal,
        "Bourdon 8",
        T::Flute,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        pedal,
        "Bombarde 16",
        T::Reed,
        F::SIXTEEN,
        P::Sub,
        R::Foundation,
    );
    s(
        &mut b,
        pedal,
        "Trompette 8",
        T::Reed,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );

    // Grand-Orgue.
    s(
        &mut b,
        go,
        "Montre 16",
        T::Principal,
        F::SIXTEEN,
        P::Sub,
        R::Foundation,
    );
    s(
        &mut b,
        go,
        "Montre 8",
        T::Principal,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        go,
        "Bourdon 16",
        T::Flute,
        F::SIXTEEN,
        P::Sub,
        R::Foundation,
    );
    s(
        &mut b,
        go,
        "Bourdon 8",
        T::Flute,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        go,
        "Prestant 4",
        T::Principal,
        F::FOUR,
        P::Octave,
        R::Chorus,
    );
    // Open: an open flute has the even harmonics a stopped one cannot produce.
    b.stop(
        go,
        "Flûte 4",
        T::Flute,
        F::FOUR,
        P::Octave,
        R::Chorus,
        &[],
        false,
    );
    s(
        &mut b,
        go,
        "Nazard 2 2/3",
        T::Flute,
        F::TWO_AND_TWO_THIRDS,
        P::Mutation,
        R::Color,
    );
    s(
        &mut b,
        go,
        "Doublette 2",
        T::Principal,
        F::TWO,
        P::Super,
        R::Chorus,
    );
    s(
        &mut b,
        go,
        "Tierce 1 3/5",
        T::Flute,
        F::ONE_AND_THREE_FIFTHS,
        P::Mutation,
        R::Color,
    );
    b.stop(
        go,
        "Fourniture IV",
        T::Mixture,
        F::TWO,
        P::Compound,
        R::MixtureCrown,
        &[F::TWO, F::ONE_AND_THIRD, F::ONE, F::new(2, 3)],
        true,
    );
    s(
        &mut b,
        go,
        "Trompette 8",
        T::Reed,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        go,
        "Clairon 4",
        T::Reed,
        F::FOUR,
        P::Octave,
        R::Chorus,
    );

    // Récit (enclosed).
    s(
        &mut b,
        recit,
        "Bourdon 8",
        T::Flute,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    b.stop(
        recit,
        "Flûte octaviante 4",
        T::Flute,
        F::FOUR,
        P::Octave,
        R::Chorus,
        &[],
        false,
    );
    s(
        &mut b,
        recit,
        "Gambe 8",
        T::String,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        recit,
        "Voix Céleste 8",
        T::String,
        F::EIGHT,
        P::Unison,
        R::Color,
    );
    b.stop(
        recit,
        "Plein Jeu III",
        T::Mixture,
        F::TWO,
        P::Compound,
        R::MixtureCrown,
        &[F::TWO, F::ONE_AND_THIRD, F::ONE],
        true,
    );
    s(
        &mut b,
        recit,
        "Basson-Hautbois 8",
        T::Reed,
        F::EIGHT,
        P::Unison,
        R::Color,
    );
    s(
        &mut b,
        recit,
        "Trompette 8",
        T::Reed,
        F::EIGHT,
        P::Unison,
        R::Foundation,
    );
    s(
        &mut b,
        recit,
        "Clairon 4",
        T::Reed,
        F::FOUR,
        P::Octave,
        R::Chorus,
    );

    b.organ.manuals = vec![
        Manual {
            division: go,
            index: 0,
            midi_channel: 0,
            low_note: 36,
            high_note: 96,
        },
        Manual {
            division: recit,
            index: 1,
            midi_channel: 1,
            low_note: 36,
            high_note: 96,
        },
        Manual {
            division: pedal,
            index: 2,
            midi_channel: 2,
            low_note: 36,
            high_note: 67,
        },
    ];

    b.coupler("Récit/Grand-Orgue", recit, go, 0);
    b.coupler("Grand-Orgue/Pédale", go, pedal, 0);
    b.coupler("Récit/Pédale", recit, pedal, 0);
    b.organ
}
