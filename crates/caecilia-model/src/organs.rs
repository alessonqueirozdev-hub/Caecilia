// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organs the instrument carries, besides the Caecilia Pipe Organ: each a
//! style rather than a copy of any instrument, its specification drawn from what
//! organs of that style commonly have. Their voices are the recipes every organ
//! gets, shaped by family, length and wind; what makes them differ is what stops
//! they have, how they're divided, and the wind they stand on.

use crate::builder::{OrganBuilder, StopSpec};
use crate::footage::Footage;
use crate::ids::{ChorusRole, DivisionId, PitchClass, TonalFamily};
use crate::organ::{DivisionKind, Manual, Organ, RecipeOverride};

use ChorusRole as R;
use Footage as F;
use TonalFamily as T;

/// A stop in a table: its name, family and length, and what else it is.
struct Row<'a> {
    name: &'static str,
    family: T,
    footage: F,
    /// A mixture's ranks, for a compound stop.
    ranks: &'a [F],
    /// Open, for a flute; only flutes read it.
    open: bool,
    /// A voice of its own: brightness, a reed's formant in hertz, fundamental
    /// in decibels.
    voice: Option<(f32, f32, f32)>,
    /// A colour rather than part of the chorus.
    colour: bool,
}

const fn row(name: &'static str, family: T, footage: F) -> Row<'static> {
    Row {
        name,
        family,
        footage,
        ranks: &[],
        open: false,
        voice: None,
        colour: false,
    }
}

const fn principal(name: &'static str, footage: F) -> Row<'static> {
    row(name, T::Principal, footage)
}

/// A stopped flute.
const fn stopped(name: &'static str, footage: F) -> Row<'static> {
    row(name, T::Flute, footage)
}

/// A Quintadena: stopped, with the twelfth speaking almost as loud as the note.
const fn quintadena(name: &'static str, footage: F) -> Row<'static> {
    Row {
        voice: Some((1.7, 1200.0, -4.0)),
        ..stopped(name, footage)
    }
}

/// An open flute, with the even harmonics a stopped one lacks.
const fn open(name: &'static str, footage: F) -> Row<'static> {
    Row {
        open: true,
        ..row(name, T::Flute, footage)
    }
}

const fn string(name: &'static str, footage: F) -> Row<'static> {
    row(name, T::String, footage)
}

/// A celeste, or another string that colours rather than founds.
const fn celeste(name: &'static str, footage: F) -> Row<'static> {
    Row {
        colour: true,
        ..row(name, T::String, footage)
    }
}

/// A mixture and its ranks, from the 2' they crown.
const fn mixture<'a>(name: &'static str, ranks: &'a [F]) -> Row<'a> {
    Row {
        ranks,
        ..row(name, T::Mixture, F::TWO)
    }
}

const fn reed(name: &'static str, footage: F, voice: (f32, f32, f32)) -> Row<'static> {
    Row {
        voice: Some(voice),
        ..row(name, T::Reed, footage)
    }
}

/// A reed that's a solo colour: an oboe, a cromorne, a vox humana.
const fn solo_reed(name: &'static str, footage: F, voice: (f32, f32, f32)) -> Row<'static> {
    Row {
        colour: true,
        ..reed(name, footage, voice)
    }
}

const QUINT: F = F::TWO_AND_TWO_THIRDS;
const TIERCE: F = F::ONE_AND_THREE_FIFTHS;
const LARIGOT: F = F::ONE_AND_THIRD;

// Reed voices: how bright, where the formant sits, how much fundamental.
const TRUMPET: (f32, f32, f32) = (1.0, 1200.0, 0.0);
const CLARION: (f32, f32, f32) = (1.15, 1450.0, -1.0);
const BOMBARDE: (f32, f32, f32) = (0.85, 950.0, 2.0);
const OBOE: (f32, f32, f32) = (0.6, 1750.0, -7.0);
const VOX_HUMANA: (f32, f32, f32) = (0.7, 1100.0, -5.0);
/// A cylindrical cromorne or clarinet: its fundamental strong, its tone hollow.
const CROMORNE: (f32, f32, f32) = (0.55, 900.0, 1.0);
/// A short regal, or a Baroque trumpet: brighter and quicker than a Romantic one.
const BAROQUE_TRUMPET: (f32, f32, f32) = (1.2, 1500.0, -2.0);

/// One division of a table: its name, whether it's the pedal, whether it stands
/// in a swell box, its chest's wind in pascals, whether the chest has a
/// tremulant, and its stops.
struct Part<'a> {
    name: &'static str,
    pedal: bool,
    enclosed: bool,
    pressure_pa: f32,
    tremulant: bool,
    stops: &'a [Row<'a>],
}

fn build(name: &str, parts: &[Part], couplers: &[(&str, usize, usize)]) -> Organ {
    let mut b = OrganBuilder::new(name, "The Caecilia contributors", 2026);
    let mut divisions: Vec<DivisionId> = Vec::with_capacity(parts.len());
    for part in parts {
        let chest = b.chest(
            &format!("{} chest", part.name),
            part.pressure_pa,
            part.tremulant,
        );
        let kind = if part.pedal {
            DivisionKind::Pedal
        } else {
            DivisionKind::Manual
        };
        let high = if part.pedal { 67 } else { 96 };
        divisions.push(b.division(
            part.name,
            kind,
            36,
            high,
            part.enclosed,
            part.enclosed,
            chest,
        ));
    }
    for (part, division) in parts.iter().zip(&divisions) {
        for stop in part.stops {
            let mutation = stop.footage.is_mutation();
            let feet = stop.footage.feet();
            let pitch_class = if !stop.ranks.is_empty() {
                PitchClass::Compound
            } else if mutation {
                PitchClass::Mutation
            } else if feet > 8.0 {
                PitchClass::Sub
            } else if feet > 4.0 {
                PitchClass::Unison
            } else if feet > 2.0 {
                PitchClass::Octave
            } else {
                PitchClass::Super
            };
            let role = if !stop.ranks.is_empty() {
                R::MixtureCrown
            } else if mutation || stop.colour {
                R::Color
            } else if feet >= 8.0 {
                R::Foundation
            } else {
                R::Chorus
            };
            let mut spec = StopSpec::new(stop.name, stop.family, stop.footage, pitch_class, role);
            spec.mixture = stop.ranks;
            spec.stopped = !stop.open;
            spec.recipe =
                stop.voice
                    .map(|(brightness, formant_hz, fundamental_db)| RecipeOverride {
                        brightness,
                        formant_hz,
                        fundamental_db,
                    });
            b.stop(*division, spec);
        }
    }
    for (i, (part, division)) in parts.iter().zip(&divisions).enumerate() {
        b.manual(Manual {
            division: *division,
            index: i as i32,
            midi_channel: i as i32,
            low_note: 36,
            high_note: if part.pedal { 67 } else { 96 },
        });
    }
    for (name, from, to) in couplers {
        b.coupler(name, divisions[*from], divisions[*to], 0);
    }
    b.build()
}

/// A German Baroque organ: bright principal choruses on low wind, a Rückpositiv
/// to answer the Hauptwerk, and a pedal that carries its own chorus.
pub fn german_baroque_organ() -> Organ {
    build(
        "German Baroque Organ",
        &[
            Part {
                name: "Hauptwerk",
                pedal: false,
                enclosed: false,
                pressure_pa: 650.0,
                tremulant: false,
                stops: &[
                    principal("Principal 16", F::SIXTEEN),
                    quintadena("Quintadena 16", F::SIXTEEN),
                    principal("Octave 8", F::EIGHT),
                    stopped("Rohrflöte 8", F::EIGHT),
                    principal("Octave 4", F::FOUR),
                    open("Spitzflöte 4", F::FOUR),
                    principal("Quinte 2 2/3", QUINT),
                    principal("Octave 2", F::TWO),
                    mixture(
                        "Mixtur V",
                        &[F::TWO, LARIGOT, F::ONE, F::new(2, 3), F::new(1, 2)],
                    ),
                    reed("Trompete 8", F::EIGHT, BAROQUE_TRUMPET),
                ],
            },
            Part {
                name: "Rückpositiv",
                pedal: false,
                enclosed: false,
                pressure_pa: 600.0,
                tremulant: true,
                stops: &[
                    stopped("Gedackt 8", F::EIGHT),
                    quintadena("Quintadena 8", F::EIGHT),
                    principal("Principal 4", F::FOUR),
                    stopped("Rohrflöte 4", F::FOUR),
                    principal("Octave 2", F::TWO),
                    open("Waldflöte 2", F::TWO),
                    open("Sesquialtera 2 2/3", QUINT),
                    open("Terz 1 3/5", TIERCE),
                    mixture("Scharff III", &[F::ONE, F::new(2, 3), F::new(1, 2)]),
                    solo_reed("Krummhorn 8", F::EIGHT, CROMORNE),
                ],
            },
            Part {
                name: "Pedal",
                pedal: true,
                enclosed: false,
                pressure_pa: 750.0,
                tremulant: false,
                stops: &[
                    principal("Principal 16", F::SIXTEEN),
                    stopped("Subbass 16", F::SIXTEEN),
                    principal("Octave 8", F::EIGHT),
                    principal("Octave 4", F::FOUR),
                    mixture("Mixtur IV", &[F::TWO, LARIGOT, F::ONE, F::new(2, 3)]),
                    reed("Posaune 16", F::SIXTEEN, BOMBARDE),
                    reed("Trompete 8", F::EIGHT, BAROQUE_TRUMPET),
                    reed("Cornett 4", F::FOUR, CLARION),
                ],
            },
        ],
        &[
            ("Rückpositiv/Hauptwerk", 1, 0),
            ("Hauptwerk/Pedal", 0, 2),
            ("Rückpositiv/Pedal", 1, 2),
        ],
    )
}

/// A French Romantic organ: harmonic flutes, a singing swell on high wind, and
/// the reeds that make its grand chorus, on three manuals.
pub fn french_romantic_organ() -> Organ {
    build(
        "French Romantic Organ",
        &[
            Part {
                name: "Grand-Orgue",
                pedal: false,
                enclosed: false,
                pressure_pa: 900.0,
                tremulant: false,
                stops: &[
                    principal("Montre 16", F::SIXTEEN),
                    stopped("Bourdon 16", F::SIXTEEN),
                    principal("Montre 8", F::EIGHT),
                    open("Flûte harmonique 8", F::EIGHT),
                    stopped("Bourdon 8", F::EIGHT),
                    string("Gambe 8", F::EIGHT),
                    principal("Prestant 4", F::FOUR),
                    open("Flûte 4", F::FOUR),
                    principal("Doublette 2", F::TWO),
                    mixture(
                        "Plein Jeu V",
                        &[F::TWO, LARIGOT, F::ONE, F::new(2, 3), F::new(1, 2)],
                    ),
                    reed("Bombarde 16", F::SIXTEEN, BOMBARDE),
                    reed("Trompette 8", F::EIGHT, TRUMPET),
                    reed("Clairon 4", F::FOUR, CLARION),
                ],
            },
            Part {
                name: "Positif",
                pedal: false,
                enclosed: false,
                pressure_pa: 800.0,
                tremulant: false,
                stops: &[
                    principal("Principal 8", F::EIGHT),
                    stopped("Bourdon 8", F::EIGHT),
                    string("Salicional 8", F::EIGHT),
                    principal("Prestant 4", F::FOUR),
                    open("Flûte douce 4", F::FOUR),
                    open("Nazard 2 2/3", QUINT),
                    open("Doublette 2", F::TWO),
                    open("Tierce 1 3/5", TIERCE),
                    open("Larigot 1 1/3", LARIGOT),
                    solo_reed("Clarinette 8", F::EIGHT, CROMORNE),
                    reed("Trompette 8", F::EIGHT, TRUMPET),
                ],
            },
            Part {
                name: "Récit",
                pedal: false,
                enclosed: true,
                pressure_pa: 950.0,
                tremulant: true,
                stops: &[
                    stopped("Bourdon 16", F::SIXTEEN),
                    principal("Diapason 8", F::EIGHT),
                    open("Flûte traversière 8", F::EIGHT),
                    stopped("Bourdon 8", F::EIGHT),
                    string("Gambe 8", F::EIGHT),
                    celeste("Voix Céleste 8", F::EIGHT),
                    open("Flûte octaviante 4", F::FOUR),
                    open("Nasard 2 2/3", QUINT),
                    open("Octavin 2", F::TWO),
                    open("Tierce 1 3/5", TIERCE),
                    mixture("Plein Jeu IV", &[F::TWO, LARIGOT, F::ONE, F::new(2, 3)]),
                    solo_reed("Basson-Hautbois 8", F::EIGHT, OBOE),
                    solo_reed("Voix Humaine 8", F::EIGHT, VOX_HUMANA),
                    reed("Trompette 8", F::EIGHT, TRUMPET),
                    reed("Clairon 4", F::FOUR, CLARION),
                ],
            },
            Part {
                name: "Pédale",
                pedal: true,
                enclosed: false,
                pressure_pa: 1000.0,
                tremulant: false,
                stops: &[
                    principal("Contrebasse 16", F::SIXTEEN),
                    stopped("Soubasse 16", F::SIXTEEN),
                    principal("Octavebasse 8", F::EIGHT),
                    stopped("Bourdon 8", F::EIGHT),
                    open("Flûte 4", F::FOUR),
                    reed("Bombarde 16", F::SIXTEEN, BOMBARDE),
                    reed("Trompette 8", F::EIGHT, TRUMPET),
                    reed("Clairon 4", F::FOUR, CLARION),
                ],
            },
        ],
        &[
            ("Récit/Grand-Orgue", 2, 0),
            ("Positif/Grand-Orgue", 1, 0),
            ("Récit/Positif", 2, 1),
            ("Grand-Orgue/Pédale", 0, 3),
            ("Récit/Pédale", 2, 3),
        ],
    )
}

/// An Italian Renaissance organ: one manual of separate principal ranks on very
/// gentle wind, with hardly anything else, and a small pedal to hold the bass.
pub fn italian_renaissance_organ() -> Organ {
    build(
        "Italian Renaissance Organ",
        &[
            Part {
                name: "Organo",
                pedal: false,
                enclosed: false,
                pressure_pa: 480.0,
                tremulant: true,
                stops: &[
                    principal("Principale 8", F::EIGHT),
                    principal("Ottava 4", F::FOUR),
                    principal("Decimaquinta 2", F::TWO),
                    principal("Decimanona 1 1/3", LARIGOT),
                    principal("Vigesimaseconda 1", F::ONE),
                    open("Flauto in ottava 4", F::FOUR),
                    open("Flauto in duodecima 2 2/3", QUINT),
                    celeste("Voce umana 8", F::EIGHT),
                ],
            },
            Part {
                name: "Pedale",
                pedal: true,
                enclosed: false,
                pressure_pa: 520.0,
                tremulant: false,
                stops: &[
                    stopped("Contrabbassi 16", F::SIXTEEN),
                    principal("Ottava di basso 8", F::EIGHT),
                ],
            },
        ],
        &[("Organo/Pedale", 0, 1)],
    )
}

/// A small chapel organ: a few gentle stops on one manual, for a small room and
/// for accompanying voices.
pub fn chapel_organ() -> Organ {
    build(
        "Chapel Organ",
        &[
            Part {
                name: "Manual",
                pedal: false,
                enclosed: false,
                pressure_pa: 550.0,
                tremulant: true,
                stops: &[
                    stopped("Stopped Diapason 8", F::EIGHT),
                    string("Dulciana 8", F::EIGHT),
                    principal("Principal 4", F::FOUR),
                    open("Chimney Flute 4", F::FOUR),
                    principal("Fifteenth 2", F::TWO),
                    mixture("Sesquialtera II", &[QUINT, TIERCE]),
                    solo_reed("Oboe 8", F::EIGHT, OBOE),
                ],
            },
            Part {
                name: "Pedal",
                pedal: true,
                enclosed: false,
                pressure_pa: 600.0,
                tremulant: false,
                stops: &[
                    stopped("Bourdon 16", F::SIXTEEN),
                    principal("Bass Flute 8", F::EIGHT),
                ],
            },
        ],
        &[("Manual/Pedal", 0, 1)],
    )
}

/// An organ the instrument carries: its name, and what makes it.
pub type BuiltInOrgan = (&'static str, fn() -> Organ);

/// The organs the instrument carries, by name, the Caecilia Pipe Organ first.
pub const BUILT_IN_ORGANS: [BuiltInOrgan; 5] = [
    ("Caecilia Pipe Organ", crate::demo::caecilia_organ),
    ("German Baroque Organ", german_baroque_organ),
    ("French Romantic Organ", french_romantic_organ),
    ("Italian Renaissance Organ", italian_renaissance_organ),
    ("Chapel Organ", chapel_organ),
];

/// The organ the instrument carries under `name`.
pub fn built_in_organ(name: &str) -> Option<Organ> {
    BUILT_IN_ORGANS
        .iter()
        .find(|(n, _)| *n == name)
        .map(|(_, make)| make())
}
