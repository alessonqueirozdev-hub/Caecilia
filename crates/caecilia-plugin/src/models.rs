// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Registration models: ready-made sounds to choose with one press, or with
//! a key of the keyboard. Each is a selector, so it means the same on any
//! organ, and draws whole: what it doesn't name is retired.
//!
//! A keyboard on its own then plays the manuals the model drew stops on, so a
//! model of the swell is played on the swell at once, and one of the whole
//! organ on every manual.

use caecilia_model::{DivisionKind, Organ};
use caecilia_registration::resolve_expression;

/// A model: what it's called, with `{s}` and `{g}` for the names of the swell
/// and the great; the stops it draws; and whether the tremulant shakes them.
pub(crate) struct Model {
    name: &'static str,
    selector: &'static str,
    pub(crate) tremulant: bool,
}

const fn model(name: &'static str, selector: &'static str) -> Model {
    Model {
        name,
        selector,
        tremulant: false,
    }
}

const fn trembling(name: &'static str, selector: &'static str) -> Model {
    Model {
        name,
        selector,
        tremulant: true,
    }
}

/// How many models there are.
pub const MODELS: usize = 26;

/// The models, in the order the console offers them: one manual's first, to
/// change quickly between them, then 1.x's whole organs.
pub(crate) static TABLE: [Model; MODELS] = [
    model(
        "{g}: foundations",
        "div:great & pitch:8 & (family:principal | family:flute) | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{g}: principals 8' 4'",
        "div:great & family:principal & (pitch:8 | pitch:4) | div:pedal & family:principal & pitch:8",
    ),
    model(
        "{g}: plenum",
        "div:great & (family:principal | family:mixture) | div:pedal & family:principal",
    ),
    model(
        "{g}: flutes",
        "div:great & family:flute & (pitch:8 | pitch:4) | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{g}: trumpet",
        "div:great & (family:reed & pitch:8 | family:flute & pitch:8) | div:pedal & family:flute & pitch:16",
    ),
    trembling(
        "{s}: strings",
        "div:swell & family:string | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{s}: flutes",
        "div:swell & family:flute & (pitch:8 | pitch:4) | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{s}: cornet",
        "div:swell & family:flute | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{s}: oboe",
        "div:swell & (name:hautbois | name:oboe | family:flute & pitch:8) | div:pedal & family:flute & pitch:16",
    ),
    trembling(
        "{s}: Voix Humaine",
        "div:swell & (name:\"voix humaine\" | family:flute & pitch:8) | div:pedal & family:flute & pitch:16",
    ),
    model(
        "{s}: full swell",
        "div:swell | div:pedal & family:principal",
    ),
    model(
        "Foundations",
        "pitch:8 & (family:principal | family:flute) | pitch:4 & family:principal",
    ),
    model("Psalmody", "family:flute & pitch:8"),
    model("Monastic", "family:flute & (pitch:8 | pitch:4)"),
    model(
        "Soft accompaniment",
        "pitch:8 & (family:flute | family:string & role:foundation)",
    ),
    trembling("Voix Céleste", "family:string & pitch:8"),
    model("Baroque organ", "family:principal | family:mixture"),
    model(
        "Renaissance organ",
        "family:flute | family:principal & (pitch:8 | pitch:4)",
    ),
    model(
        "Classic organ",
        "(family:principal | family:flute) & (pitch:16 | pitch:8 | pitch:4 | pitch:2)",
    ),
    model(
        "Romantic organ",
        "!family:mixture & (pitch:16 | pitch:8 | pitch:4 | pitch:2)",
    ),
    model("Pipe organ I", "pitch:8 & !family:mixture"),
    model(
        "Pipe organ II",
        "(family:principal | family:flute) & (pitch:8 | pitch:4)",
    ),
    model("String organ", "family:string | family:flute & pitch:8"),
    model("Flute organ", "family:flute"),
    model("Reed organ", "family:reed"),
    model("Tutti", "*"),
];

/// What model `n` does on `organ`: the stops it draws, as a mask, and the
/// manuals a keyboard on its own then plays, as bits by division.
pub(crate) fn resolve(organ: &Organ, n: usize) -> (u64, u16) {
    let Some(model) = TABLE.get(n) else {
        return (0, 0);
    };
    let stops = resolve_expression(organ, model.selector).to_mask();
    let mut manuals = 0u16;
    for stop in organ.stops() {
        let drawn = stop.id.index() < 64 && stops & (1 << stop.id.index()) != 0;
        let manual = organ
            .division(stop.division)
            .is_some_and(|d| d.kind == DivisionKind::Manual);
        if drawn && manual {
            manuals |= 1u16.checked_shl(u32::from(stop.division.0)).unwrap_or(0);
        }
    }
    (stops, manuals)
}

/// What model `n` is called on `organ`.
pub(crate) fn label(organ: &Organ, n: usize) -> String {
    let Some(model) = TABLE.get(n) else {
        return format!("Model {n}");
    };
    let great = organ
        .division(organ.primary_manual())
        .map_or("Great", |d| d.name.as_str());
    let swell = organ
        .divisions()
        .iter()
        .find(|d| d.kind == DivisionKind::Manual && d.enclosed)
        .map_or("Swell", |d| d.name.as_str());
    model.name.replace("{g}", great).replace("{s}", swell)
}

#[cfg(test)]
mod tests {
    use super::*;
    use caecilia_model::caecilia_organ;

    fn division_of(organ: &Organ, name: &str) -> u16 {
        organ
            .divisions()
            .iter()
            .find(|d| d.name == name)
            .map(|d| 1 << d.id.0)
            .expect("the division")
    }

    #[test]
    fn every_model_draws_stops_on_the_caecilia_organ() {
        let organ = caecilia_organ();
        for n in 0..MODELS {
            let (stops, manuals) = resolve(&organ, n);
            assert_ne!(stops, 0, "{} draws nothing", label(&organ, n));
            assert_ne!(manuals, 0, "{} plays no manual", label(&organ, n));
        }
    }

    #[test]
    fn a_model_of_one_manual_plays_that_manual() {
        let organ = caecilia_organ();
        let manuals = |name: &str| {
            let n = (0..MODELS)
                .find(|n| label(&organ, *n) == name)
                .unwrap_or_else(|| panic!("no model {name}"));
            resolve(&organ, n).1
        };
        let (great, swell) = (
            division_of(&organ, "Grand-Orgue"),
            division_of(&organ, "Récit"),
        );
        assert_eq!(manuals("Récit: strings"), swell);
        assert_eq!(manuals("Grand-Orgue: plenum"), great);
        assert_eq!(manuals("Tutti"), great | swell);
    }

    #[test]
    fn every_model_differs_from_every_other() {
        let organ = caecilia_organ();
        let sounds: Vec<(u64, bool)> = TABLE
            .iter()
            .enumerate()
            .map(|(n, m)| (resolve(&organ, n).0, m.tremulant))
            .collect();
        for (a, sound) in sounds.iter().enumerate() {
            if let Some(b) = sounds[..a].iter().position(|other| other == sound) {
                panic!("{} and {} are the same", label(&organ, a), label(&organ, b));
            }
        }
    }
}
