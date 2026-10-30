//! The Caecilia demonstration organ: three divisions, 26 stops, one rank per
//! stop, one chest per division, three couplers.

use crate::builder::{OrganBuilder, StopSpec};
use crate::footage::Footage;
use crate::ids::{ChorusRole, PitchClass, TonalFamily};
use crate::organ::{DivisionKind, Manual, Organ, RecipeOverride};

/// Each reed its own voice (2.0). A Bombarde is dark and fundamental-heavy; a
/// Clairon brighter than the Trompette under it; the Basson-Hautbois is a
/// narrow conical oboe, its fundamental weak and its formant high -- nothing
/// like the trumpet 1.x made of it.
fn reed_voice(name: &str) -> Option<RecipeOverride> {
    let (brightness, formant_hz, fundamental_db) = match name {
        "Bombarde 16" => (0.85, 950.0, 2.0),
        "Trompette 8" => (1.0, 1200.0, 0.0),
        "Clairon 4" => (1.15, 1450.0, -1.0),
        "Basson-Hautbois 8" => (0.6, 1750.0, -7.0),
        _ => return None,
    };
    Some(RecipeOverride {
        brightness,
        formant_hz,
        fundamental_db,
    })
}

/// Build the demonstration organ.
pub fn demo_organ() -> Organ {
    use ChorusRole as R;
    use Footage as F;
    use PitchClass as P;
    use TonalFamily as T;

    let mut b = OrganBuilder::new("Caecilia Demonstration Organ", "Alesson Queiroz", 2026);

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

    // (division, name, family, footage, class, role, mixture ranks, stopped).
    // Every rank is stopped unless said otherwise: only flutes read it, and the
    // two open flutes are the ones with the even harmonics a stopped pipe lacks.
    let mixture4 = [F::TWO, F::ONE_AND_THIRD, F::ONE, F::new(2, 3)];
    let mixture3 = [F::TWO, F::ONE_AND_THIRD, F::ONE];
    let stops: [(_, &str, _, _, _, _, &[F], bool); 26] = [
        (
            pedal,
            "Contrebasse 16",
            T::Principal,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
            &[],
            true,
        ),
        (
            pedal,
            "Soubasse 16",
            T::Flute,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
            &[],
            true,
        ),
        (
            pedal,
            "Octavebasse 8",
            T::Principal,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            pedal,
            "Bourdon 8",
            T::Flute,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            pedal,
            "Bombarde 16",
            T::Reed,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
            &[],
            true,
        ),
        (
            pedal,
            "Trompette 8",
            T::Reed,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Montre 16",
            T::Principal,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Montre 8",
            T::Principal,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Bourdon 16",
            T::Flute,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Bourdon 8",
            T::Flute,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Prestant 4",
            T::Principal,
            F::FOUR,
            P::Octave,
            R::Chorus,
            &[],
            true,
        ),
        (
            go,
            "Flûte 4",
            T::Flute,
            F::FOUR,
            P::Octave,
            R::Chorus,
            &[],
            false,
        ),
        (
            go,
            "Nazard 2 2/3",
            T::Flute,
            F::TWO_AND_TWO_THIRDS,
            P::Mutation,
            R::Color,
            &[],
            true,
        ),
        (
            go,
            "Doublette 2",
            T::Principal,
            F::TWO,
            P::Super,
            R::Chorus,
            &[],
            true,
        ),
        (
            go,
            "Tierce 1 3/5",
            T::Flute,
            F::ONE_AND_THREE_FIFTHS,
            P::Mutation,
            R::Color,
            &[],
            true,
        ),
        (
            go,
            "Fourniture IV",
            T::Mixture,
            F::TWO,
            P::Compound,
            R::MixtureCrown,
            &mixture4,
            true,
        ),
        (
            go,
            "Trompette 8",
            T::Reed,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            go,
            "Clairon 4",
            T::Reed,
            F::FOUR,
            P::Octave,
            R::Chorus,
            &[],
            true,
        ),
        (
            recit,
            "Bourdon 8",
            T::Flute,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            recit,
            "Flûte octaviante 4",
            T::Flute,
            F::FOUR,
            P::Octave,
            R::Chorus,
            &[],
            false,
        ),
        (
            recit,
            "Gambe 8",
            T::String,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            recit,
            "Voix Céleste 8",
            T::String,
            F::EIGHT,
            P::Unison,
            R::Color,
            &[],
            true,
        ),
        (
            recit,
            "Plein Jeu III",
            T::Mixture,
            F::TWO,
            P::Compound,
            R::MixtureCrown,
            &mixture3,
            true,
        ),
        (
            recit,
            "Basson-Hautbois 8",
            T::Reed,
            F::EIGHT,
            P::Unison,
            R::Color,
            &[],
            true,
        ),
        (
            recit,
            "Trompette 8",
            T::Reed,
            F::EIGHT,
            P::Unison,
            R::Foundation,
            &[],
            true,
        ),
        (
            recit,
            "Clairon 4",
            T::Reed,
            F::FOUR,
            P::Octave,
            R::Chorus,
            &[],
            true,
        ),
    ];
    for (division, name, family, footage, class, role, mixture, stopped) in stops {
        let mut spec = StopSpec::new(name, family, footage, class, role);
        spec.mixture = mixture;
        spec.stopped = stopped;
        spec.recipe = reed_voice(name);
        b.stop(division, spec);
    }

    for (division, index, channel, low, high) in [
        (go, 0, 0, 36, 96),
        (recit, 1, 1, 36, 96),
        (pedal, 2, 2, 36, 67),
    ] {
        b.manual(Manual {
            division,
            index,
            midi_channel: channel,
            low_note: low,
            high_note: high,
        });
    }

    b.coupler("Récit/Grand-Orgue", recit, go, 0);
    b.coupler("Grand-Orgue/Pédale", go, pedal, 0);
    b.coupler("Récit/Pédale", recit, pedal, 0);
    b.build()
}
