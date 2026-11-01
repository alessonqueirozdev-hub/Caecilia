// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organ file: 1.x's documents, 2.0's round trip, and the errors.

use caecilia_model::{
    Calibration, Organ, OrganFileError, StopId, build_rank_voicing, demo_organ, read_organ,
    read_organ_file, write_organ,
};

fn example() -> String {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../examples/caecilia-demo.organ.json"
    );
    std::fs::read_to_string(path).expect("the 1.x example organ")
}

/// Everything but the order the keyboards are listed in.
fn assert_same_organ(a: &Organ, b: &Organ, ignore_recipes: bool) {
    assert_eq!(a.name, b.name);
    assert_eq!(a.windchests(), b.windchests());
    assert_eq!(a.divisions(), b.divisions());
    assert_eq!(a.stops(), b.stops());
    assert_eq!(a.couplers(), b.couplers());
    assert_eq!(a.ranks().len(), b.ranks().len());
    for (x, y) in a.ranks().iter().zip(b.ranks()) {
        let mut y = y.clone();
        if ignore_recipes {
            y.recipe = x.recipe;
        }
        assert_eq!(*x, y, "rank {}", x.name);
    }
    let mut ma = a.manuals().to_vec();
    let mut mb = b.manuals().to_vec();
    ma.sort_by_key(|m| m.division);
    mb.sort_by_key(|m| m.division);
    assert_eq!(ma, mb);
}

#[test]
fn the_1x_example_loads_as_the_demo_organ_and_sounds_as_1x_did() {
    let doc = read_organ(&example(), None).expect("loads");
    assert!(doc.warnings.is_empty(), "{:?}", doc.warnings);
    let demo = demo_organ();
    // 1.x's file has no per-rank recipes: those are 2.0's.
    assert_same_organ(&doc.organ, &demo, true);
    for s in demo.stops() {
        let ours = build_rank_voicing(&doc.organ, s.id, Calibration::OneX);
        assert_eq!(
            ours,
            build_rank_voicing(&demo, s.id, Calibration::OneX),
            "{}",
            s.name
        );
    }
}

#[test]
fn an_organ_written_reads_back_the_same() {
    let demo = demo_organ();
    let text = write_organ(&demo);
    let back = read_organ(&text, None).expect("reads back").organ;
    assert_same_organ(&demo, &back, false);
    for s in demo.stops() {
        let v2 = |o: &Organ| build_rank_voicing(o, s.id, Calibration::V2);
        assert_eq!(
            v2(&back),
            v2(&demo),
            "{}: the 2.0 voicing survives the file",
            s.name
        );
    }
    // And it is still a 1.x document: the keys 1.x reads are all there.
    assert!(
        text.contains("\"rank\": \"Récit chest/Trompette 8\""),
        "ambiguous ranks are qualified"
    );
}

fn errors(text: &str) -> Vec<String> {
    match read_organ(text, None) {
        Err(OrganFileError::Invalid { errors, .. }) => {
            errors.iter().map(ToString::to_string).collect()
        }
        other => panic!("expected errors, got {other:?}"),
    }
}

/// The 1.x example with `change` made to it.
fn edited(change: impl FnOnce(&mut serde_json::Value)) -> String {
    let mut v: serde_json::Value = serde_json::from_str(&example()).unwrap();
    change(&mut v);
    v.to_string()
}

#[test]
fn a_bad_document_says_where_and_how_to_fix_it() {
    match read_organ(
        "{
  \"name\": \"x\",
  \"ranks\": [ }",
        None,
    ) {
        Err(OrganFileError::Syntax { line, .. }) => assert_eq!(line, 3),
        other => panic!("{other:?}"),
    }

    let e = errors(&edited(|v| v["stops"][18]["division"] = "Recit".into()));
    assert!(
        e.iter()
            .any(|m| m.contains("stop[18] 'Bourdon 8'") && m.contains("unknown division 'Recit'")),
        "{e:?}"
    );

    // An ambiguous rank, and the fix offered.
    let e = errors(&edited(|v| v["stops"][24]["rank"] = "Trompette 8".into()));
    assert!(
        e.iter()
            .any(|m| m.contains("names 3 ranks") && m.contains("chest/Trompette 8")),
        "{e:?}"
    );

    // More stops than a registration word holds: the first one over the line is named.
    let e = errors(&edited(|v| {
        let stops = v["stops"].as_array().unwrap().clone();
        v["stops"] = stops.iter().cycle().take(65).cloned().collect();
    }));
    assert!(
        e.iter()
            .any(|m| m.contains("stop[64]") && m.contains("at most 64")),
        "{e:?}"
    );

    // A spectrum path that would read the recipient's disk.
    let e = errors(&edited(|v| {
        v["ranks"][0]["spectrum"] = "../../secret.json".into()
    }));
    assert!(
        e.iter().any(|m| m.contains("inside the organ's folder")),
        "{e:?}"
    );
    let e = errors(&edited(|v| {
        v["ranks"][0]["spectrum"] = "C:/Windows/win.ini".into()
    }));
    assert!(
        e.iter().any(|m| m.contains("inside the organ's folder")),
        "{e:?}"
    );
}

#[test]
fn a_measured_spectrum_beside_the_file_is_heard() {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("organ-file-test");
    std::fs::create_dir_all(dir.join("spectra")).unwrap();
    std::fs::write(
        dir.join("spectra/montre.json"),
        r#"{ "fundamentalHz": 261.6, "partials": [
             { "ratioToF0": 1, "ampDb": -3 }, { "ratioToF0": 2, "ampDb": -9 }, { "ratioToF0": 3, "ampDb": -15 } ] }"#,
    )
    .unwrap();
    let text = edited(|v| v["ranks"][7]["spectrum"] = "spectra/montre.json".into());
    std::fs::write(dir.join("organ.json"), &text).unwrap();
    let doc = read_organ_file(&dir.join("organ.json")).expect("loads");
    let montre = doc
        .organ
        .stops()
        .iter()
        .find(|s| s.name == "Montre 8")
        .unwrap()
        .id;
    let v = build_rank_voicing(&doc.organ, montre, Calibration::V2).unwrap();
    assert_eq!(
        v.spectrum.partials.len(),
        3,
        "the measured partials, not the recipe's twelve"
    );
    // Normalised: the loudest measured partial joins at the recipe's level.
    let fresh = build_rank_voicing(&demo_organ(), StopId(montre.0), Calibration::V2).unwrap();
    assert!((v.spectrum.partials[0].amp_db - fresh.spectrum.partials[0].amp_db).abs() < 1e-4);

    // Missing beside the file: a warning, and the rank keeps its reference.
    std::fs::remove_file(dir.join("spectra/montre.json")).unwrap();
    let doc = read_organ_file(&dir.join("organ.json")).expect("still loads");
    assert!(
        doc.warnings
            .iter()
            .any(|w| w.message.contains("speaks from its recipe"))
    );
    assert!(write_organ(&doc.organ).contains("spectra/montre.json"));
}

#[test]
fn a_file_saved_with_a_byte_order_mark_still_opens() {
    let with_bom = format!("\u{feff}{}", example());
    assert!(read_organ(&with_bom, None).is_ok());
}
