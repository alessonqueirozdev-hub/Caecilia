// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Selectors, commands, history, explain, port and the factory generals.

use std::sync::Arc;

use caecilia_model::{
    ChorusRole, DivisionId, DivisionKind, Footage, Organ, OrganBuilder, PitchClass, StopId,
    StopSpec, TonalFamily, demo_organ,
};
use caecilia_registration::*;

fn names(organ: &Organ, set: &StopSet) -> Vec<String> {
    set.ids()
        .iter()
        .map(|&id| {
            let s = organ.stop(id).unwrap();
            format!("{}:{}", organ.division(s.division).unwrap().name, s.name)
        })
        .collect()
}

fn select(organ: &Organ, text: &str) -> Vec<String> {
    names(
        organ,
        &parse(text)
            .unwrap()
            .resolve(organ, &RegistrationState::default()),
    )
}

/// A small English instrument: the same music, other names.
fn english_organ() -> Organ {
    use ChorusRole as R;
    use Footage as F;
    use PitchClass as P;
    use TonalFamily as T;
    let mut b = OrganBuilder::new("St Somewhere", "Test", 1890);
    let c = b.chest("Main", 800.0, true);
    let great = b.division("Great", DivisionKind::Manual, 36, 96, false, false, c);
    let swell = b.division("Swell", DivisionKind::Manual, 36, 96, true, true, c);
    let pedal = b.division("Pedal", DivisionKind::Pedal, 36, 67, false, false, c);
    for (d, n, t, f, p, r) in [
        (
            great,
            "Open Diapason 8",
            T::Principal,
            F::EIGHT,
            P::Unison,
            R::Foundation,
        ),
        (
            great,
            "Principal 4",
            T::Principal,
            F::FOUR,
            P::Octave,
            R::Chorus,
        ),
        (
            great,
            "Fifteenth 2",
            T::Principal,
            F::TWO,
            P::Super,
            R::Chorus,
        ),
        (
            swell,
            "Stopped Diapason 8",
            T::Flute,
            F::EIGHT,
            P::Unison,
            R::Foundation,
        ),
        (swell, "Oboe 8", T::Reed, F::EIGHT, P::Unison, R::Color),
        (
            pedal,
            "Bourdon 16",
            T::Flute,
            F::SIXTEEN,
            P::Sub,
            R::Foundation,
        ),
    ] {
        b.stop(d, StopSpec::new(n, t, f, p, r));
    }
    b.build()
}

#[test]
fn the_grammar_selects_by_what_stops_are() {
    let o = demo_organ();
    assert_eq!(
        select(&o, "family:reed & pitch:4"),
        ["Grand-Orgue:Clairon 4", "Récit:Clairon 4"]
    );
    assert_eq!(
        select(&o, "family:flute & pitch:8/3"),
        ["Grand-Orgue:Nazard 2 2/3"]
    );
    assert_eq!(
        select(&o, "div:récit & (family:string | role:color)"),
        [
            "Récit:Gambe 8",
            "Récit:Voix Céleste 8",
            "Récit:Basson-Hautbois 8"
        ]
    );
    assert_eq!(
        select(&o, "name:\"voix céleste\""),
        ["Récit:Voix Céleste 8"]
    );
    // Difference and complement.
    assert_eq!(select(&o, "div:pedal - family:reed").len(), 4);
    assert_eq!(select(&o, "!div:grand").len(), 14);
    // The Swell is the Récit, by its other name.
    assert_eq!(
        select(&o, "div:swell & family:string"),
        select(&o, "div:récit & family:string")
    );
    // Empty text is everything; an unknown division is nothing.
    assert_eq!(select(&o, "").len(), 26);
    assert!(select(&o, "div:brustwerk").is_empty());
}

#[test]
fn a_bad_selector_says_where_it_went_wrong() {
    let e = parse("family:reed & pitch:9/0").unwrap_err();
    assert_eq!(e.position, 20);
    assert!(e.message.contains("footage"));
    assert!(
        parse("family:kazoo")
            .unwrap_err()
            .message
            .contains("unknown family")
    );
    assert!(parse("(family:reed").unwrap_err().message.contains("')'"));
    assert!(parse("id:12x").is_err());
    assert!(
        parse("family:reed reed")
            .unwrap_err()
            .message
            .contains("trailing")
    );
}

#[test]
fn commands_fold_into_state_and_history_branches() {
    let organ = Arc::new(demo_organ());
    let mut e = RegistrationEngine::new(organ.clone(), RegistrationState::default());
    let go = DivisionId(1);

    let d = e.apply(Command::BuildPlenum(PlenumSpec {
        division: Some(go),
        ..PlenumSpec::default()
    }));
    assert_eq!(
        names(&organ, &e.state().stops),
        [
            "Grand-Orgue:Montre 16",
            "Grand-Orgue:Montre 8",
            "Grand-Orgue:Prestant 4",
            "Grand-Orgue:Doublette 2",
            "Grand-Orgue:Fourniture IV"
        ]
    );
    assert_eq!(d.engage.len(), 5);
    assert_eq!(e.explain(StopId(15)).reason, Reason::PlenumMixture);
    assert_eq!(e.explain(StopId(7)).reason, Reason::PlenumPrincipal);

    e.apply(Command::EngageSelector(
        parse("family:reed & div:grand").unwrap(),
    ));
    assert_eq!(e.state().stops.len(), 7);
    assert_eq!(e.explain(StopId(16)).reason, Reason::SelectorMatch);

    // Undo, then something new: a sibling branch, the old one kept.
    e.undo();
    assert_eq!(e.state().stops.len(), 5);
    e.apply(Command::EngageStop(StopId(12)));
    let root_child = e.history()[0].children[0];
    assert_eq!(
        e.history()[root_child].children.len(),
        2,
        "both auditions are in the tree"
    );

    // Undo restores the why along with the what.
    e.apply(Command::DisengageStop(StopId(15)));
    e.apply(Command::EngageStop(StopId(15)));
    assert_eq!(e.explain(StopId(15)).reason, Reason::Direct);
    e.undo();
    e.undo();
    assert_eq!(e.explain(StopId(15)).reason, Reason::PlenumMixture);

    // Solo within a division leaves the others alone.
    e.apply(Command::EngageStop(StopId(20)));
    e.apply(Command::Solo(parse("family:flute").unwrap(), Some(go)));
    assert!(e.state().is_engaged(StopId(20)), "the Récit Gambe stays");
    assert_eq!(
        names(&organ, &e.state().stops)
            .into_iter()
            .filter(|n| n.starts_with("Grand"))
            .collect::<Vec<_>>(),
        [
            "Grand-Orgue:Bourdon 16",
            "Grand-Orgue:Bourdon 8",
            "Grand-Orgue:Flûte 4",
            "Grand-Orgue:Nazard 2 2/3",
            "Grand-Orgue:Tierce 1 3/5"
        ]
    );
    e.apply(Command::Clear(None));
    assert!(e.state().stops.is_empty());
}

#[test]
fn an_intent_ports_across_organs_with_a_graded_confidence() {
    let demo = Arc::new(demo_organ());
    let mut e = RegistrationEngine::new(demo.clone(), RegistrationState::default());
    // Montre 8 (GO), Prestant 4 (GO), Récit Bourdon 8, Récit Basson-Hautbois 8,
    // Pédale Soubasse 16, GO Fourniture IV.
    for s in [7, 10, 18, 23, 1, 15] {
        e.apply(Command::EngageStop(StopId(s)));
    }
    let intent = e.capture_intent();
    let english = english_organ();
    let r = port(&intent, &english);
    let tiers: Vec<_> = r
        .outcomes
        .iter()
        .map(|o| (o.label.as_str(), o.tier))
        .collect();
    assert_eq!(
        tiers,
        [
            ("Pédale Soubasse 16", MatchTier::Exact),
            ("Grand-Orgue Montre 8", MatchTier::Exact),
            ("Grand-Orgue Prestant 4", MatchTier::Exact),
            ("Grand-Orgue Fourniture IV", MatchTier::Missing),
            ("Récit Bourdon 8", MatchTier::Exact),
            ("Récit Basson-Hautbois 8", MatchTier::Exact),
        ]
    );
    assert_eq!(
        names(&english, &r.resolved),
        [
            "Great:Open Diapason 8",
            "Great:Principal 4",
            "Swell:Stopped Diapason 8",
            "Swell:Oboe 8",
            "Pedal:Bourdon 16"
        ]
    );
    assert!(r.all_essentials_matched());
    // Foundations weigh double: 2+2+1+2+1 found, the Fourniture (1) missing, of 9.
    assert!(
        (r.confidence - 8.0 / 9.0).abs() < 1e-6,
        "confidence {}",
        r.confidence
    );

    // Back the other way: the Swell Oboe is the Récit's, and a Great 2' finds the
    // Doublette; nothing is ported by stop number.
    let back = port(
        &RegistrationIntent::capture(
            &english,
            &RegistrationState {
                stops: r.resolved.clone(),
                couplers: vec![],
            },
        ),
        &demo,
    );
    assert!(
        back.resolved.contains(StopId(23)),
        "Oboe -> Basson-Hautbois"
    );
    assert!(back.all_essentials_matched());
}

#[test]
fn the_factory_generals_crescendo_to_tutti() {
    let organ = demo_organ();
    let g = factory_generals(&organ);
    let count = |i: usize| match &g[i].1 {
        Piston::Snapshot(s) => s.stops.len(),
        Piston::Intent(_) => 0,
    };
    let sizes: Vec<usize> = (0..g.len()).map(count).collect();
    assert!(
        sizes[..7].windows(2).all(|w| w[0] <= w[1]),
        "cumulative: {sizes:?}"
    );
    assert_eq!(sizes[6], 26, "T is everything");
    assert!(sizes[7] < sizes[6], "solo is its own gesture");
}

#[test]
fn a_mask_carries_the_registration_to_the_audio_thread() {
    let s: StopSet = [StopId(0), StopId(5), StopId(25)].into_iter().collect();
    assert_eq!(StopSet::from_mask(s.to_mask()), s);
}
