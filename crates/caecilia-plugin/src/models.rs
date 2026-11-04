// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Registration models: ready-made sounds to choose with one press, or with
//! a key of the keyboard. Each is a selector, so it means the same on any
//! organ, and draws whole: what it doesn't name is retired.
//!
//! A keyboard on its own then plays the manuals the model drew stops on, so a
//! model of the swell is played on the swell at once, and one of the whole
//! organ on every manual.
//!
//! An organ without a swell has its second manual play the swell's models; one
//! with a single manual plays them all there. A model that finds nothing to
//! play on the manuals isn't offered, nor one named after a stop the organ
//! doesn't have.

use crate::console::ConsoleMemory;
use caecilia_model::{DivisionKind, Organ};
use caecilia_registration::resolve_expression;

/// A model: what it's called, with `{s}` and `{g}` for the names of the swell
/// and the great; the stops it draws; whether the tremulant shakes them; and
/// what the organ must have for its name to be true, if anything.
pub(crate) struct Model {
    name: &'static str,
    selector: &'static str,
    pub(crate) tremulant: bool,
    needs: &'static str,
}

const fn model(name: &'static str, selector: &'static str) -> Model {
    Model {
        name,
        selector,
        tremulant: false,
        needs: "",
    }
}

const fn trembling(name: &'static str, selector: &'static str) -> Model {
    Model {
        name,
        selector,
        tremulant: true,
        needs: "",
    }
}

impl Model {
    const fn needing(self, needs: &'static str) -> Model {
        Model { needs, ..self }
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
    )
    .needing("div:great & (name:tromp | name:trump)"),
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
    )
    .needing("div:swell & family:flute & pitch:8/5"),
    model(
        "{s}: oboe",
        "div:swell & (name:hautbois | name:oboe | family:flute & pitch:8) | div:pedal & family:flute & pitch:16",
    )
    .needing("div:swell & (name:hautbois | name:oboe)"),
    trembling(
        "{s}: Voix Humaine",
        "div:swell & (name:\"voix humaine\" | family:flute & pitch:8) | div:pedal & family:flute & pitch:16",
    )
    .needing("div:swell & name:\"voix humaine\""),
    model(
        "{s}: full",
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
    trembling("Voix Céleste", "family:string & pitch:8").needing("name:céleste | name:celeste"),
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

/// The names of the manuals that are the great and the swell on `organ`.
fn great_and_swell(organ: &Organ) -> (&str, &str) {
    let great = organ
        .division(organ.primary_manual())
        .filter(|d| d.kind == DivisionKind::Manual);
    let manuals = || {
        organ
            .divisions()
            .iter()
            .filter(|d| d.kind == DivisionKind::Manual)
    };
    let swell = manuals()
        .find(|d| d.enclosed)
        .or_else(|| manuals().find(|d| great.is_none_or(|g| g.id != d.id)))
        .or(great);
    (
        great.map_or("Great", |d| d.name.as_str()),
        swell.map_or("Swell", |d| d.name.as_str()),
    )
}

/// What model `n` does on `organ`: the stops it draws, as a mask, and the
/// manuals a keyboard on its own then plays, as bits by division. Nothing, if
/// it would play no manual.
pub(crate) fn resolve(organ: &Organ, n: usize) -> (u64, u16) {
    let Some(model) = TABLE.get(n) else {
        return (0, 0);
    };
    let (great, swell) = great_and_swell(organ);
    let on_organ = |selector: &str| {
        let selector = selector
            .replace("div:great", &format!("div:\"{great}\""))
            .replace("div:swell", &format!("div:\"{swell}\""));
        resolve_expression(organ, &selector).to_mask()
    };
    if !model.needs.is_empty() && on_organ(model.needs) == 0 {
        return (0, 0);
    }
    let stops = on_organ(model.selector);
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
    if manuals == 0 {
        return (0, 0);
    }
    (stops, manuals)
}

/// The models to offer on the organ playing: those that play something, each
/// sound once.
pub(crate) fn offered(console: &ConsoleMemory) -> Vec<usize> {
    let mut sounds = Vec::with_capacity(MODELS);
    (0..MODELS)
        .filter(|n| {
            let Some((stops, _, tremulant)) = console.model(*n) else {
                return false;
            };
            if stops == 0 || sounds.contains(&(stops, tremulant)) {
                return false;
            }
            sounds.push((stops, tremulant));
            true
        })
        .collect()
}

/// What model `n` is called on `organ`.
pub(crate) fn label(organ: &Organ, n: usize) -> String {
    let Some(model) = TABLE.get(n) else {
        return format!("Model {n}");
    };
    let (great, swell) = great_and_swell(organ);
    model.name.replace("{g}", great).replace("{s}", swell)
}

#[cfg(test)]
mod tests {
    use super::*;
    use caecilia_model::{
        BUILT_IN_ORGANS, caecilia_organ, chapel_organ, french_romantic_organ, german_baroque_organ,
    };

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

    /// What's offered on `organ`, as the console would have it.
    fn offered_on(organ: &Organ) -> Vec<usize> {
        let console = ConsoleMemory::default();
        for (n, model) in TABLE.iter().enumerate() {
            let (stops, manuals) = resolve(organ, n);
            console.set_model(n, stops, manuals, model.tremulant);
        }
        offered(&console)
    }

    #[test]
    fn every_organ_the_instrument_carries_offers_models_with_names_of_their_own() {
        for (name, make) in BUILT_IN_ORGANS {
            let organ = make();
            let n = offered_on(&organ);
            assert!(n.len() >= 8, "{name}: only {} models", n.len());
            let mut labels: Vec<String> = n.iter().map(|n| label(&organ, *n)).collect();
            labels.sort();
            let count = labels.len();
            labels.dedup();
            assert_eq!(labels.len(), count, "{name}: two models share a name");
        }
    }

    #[test]
    fn the_second_manual_plays_the_swell_s_models_where_there_is_no_swell() {
        let organ = german_baroque_organ();
        let n = (0..MODELS)
            .find(|n| label(&organ, *n) == "Rückpositiv: flutes")
            .expect("named after the manual that plays it");
        assert_eq!(resolve(&organ, n).1, division_of(&organ, "Rückpositiv"));
        // Where there's a real one, it's the swell, whichever manual it is.
        let organ = french_romantic_organ();
        assert!((0..MODELS).any(|n| label(&organ, n) == "Récit: strings"));
    }

    #[test]
    fn one_manual_plays_every_model_of_a_manual_there() {
        let organ = chapel_organ();
        let manual = division_of(&organ, "Manual");
        for n in offered_on(&organ) {
            assert_eq!(resolve(&organ, n).1, manual, "{}", label(&organ, n));
        }
    }

    #[test]
    fn a_model_named_after_a_stop_needs_the_stop() {
        let offers =
            |organ: &Organ, name: &str| offered_on(organ).iter().any(|n| label(organ, *n) == name);
        let german = german_baroque_organ();
        assert!(offers(&german, "Hauptwerk: trumpet"));
        assert!(offers(&german, "Rückpositiv: cornet"));
        assert!(!offers(&german, "Rückpositiv: oboe"));
        assert!(!offers(&german, "Rückpositiv: Voix Humaine"));
        assert!(!offers(&german, "Voix Céleste"));
        let chapel = chapel_organ();
        assert!(offers(&chapel, "Manual: oboe"));
        assert!(!offers(&chapel, "Manual: trumpet"));
        let french = french_romantic_organ();
        for name in [
            "Grand-Orgue: trumpet",
            "Récit: cornet",
            "Récit: oboe",
            "Récit: Voix Humaine",
            "Voix Céleste",
        ] {
            assert!(offers(&french, name), "{name}");
        }
    }

    #[test]
    fn a_model_that_would_play_no_manual_is_not_offered() {
        let organ = caecilia_model::italian_renaissance_organ();
        let reeds = (0..MODELS)
            .find(|n| label(&organ, *n) == "Reed organ")
            .expect("the model");
        assert_eq!(resolve(&organ, reeds), (0, 0));
        assert!(!offered_on(&organ).contains(&reeds));
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
