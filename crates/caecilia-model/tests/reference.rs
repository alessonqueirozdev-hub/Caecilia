// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demo organ and its voicing, against what 1.x built.
//!
//! `reference/cpp-1.x/reference.json` was written by 1.x's `tools/reference` (tag `v1-cpp-final`) from the
//! C++ instrument. The voicing is deterministic, so this is not a tolerance on
//! a sound but agreement on data: identical seeds, identical structure, and
//! floats equal to the last few ulps (the two math libraries may round a
//! `log10f` or `powf` differently by one).

use caecilia_model::{
    Calibration, DivisionKind, StopId, TonalFamily, build_rank_voicing, demo_organ,
};
use serde_json::Value;

fn reference() -> Value {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../reference/cpp-1.x/reference.json"
    );
    let text = std::fs::read_to_string(path).expect("reference/cpp-1.x/reference.json");
    serde_json::from_str(&text).expect("valid JSON")
}

fn f(v: &Value) -> f64 {
    v.as_f64().expect("a number")
}

/// Equal within a few ulps of an f32 at this magnitude, or `abs` absolutely.
fn close(ours: f32, theirs: f64, abs: f64, what: &str) {
    let tol = abs.max(theirs.abs() * 4.0 * f64::from(f32::EPSILON));
    assert!(
        (f64::from(ours) - theirs).abs() <= tol,
        "{what}: ours {ours}, 1.x {theirs} (tolerance {tol})"
    );
}

fn family_name(f: TonalFamily) -> &'static str {
    match f {
        TonalFamily::Principal => "Principal",
        TonalFamily::Flute => "Flute",
        TonalFamily::String => "String",
        TonalFamily::Reed => "Reed",
        TonalFamily::Mixture => "Mixture",
        _ => "Other",
    }
}

#[test]
fn the_organ_has_the_same_chests_divisions_and_stops() {
    let r = reference();
    let organ = demo_organ();
    let o = &r["organ"];
    assert_eq!(o["name"], organ.name.as_str());

    let chests = o["windchests"].as_array().unwrap();
    assert_eq!(chests.len(), organ.windchests().len());
    for (c, ours) in chests.iter().zip(organ.windchests()) {
        assert_eq!(c["name"], ours.name.as_str());
        assert_eq!(f(&c["nominalPressurePa"]) as f32, ours.nominal_pressure_pa);
        assert_eq!(c["hasTremulant"], ours.has_tremulant);
    }

    let divisions = o["divisions"].as_array().unwrap();
    assert_eq!(divisions.len(), organ.divisions().len());
    for (d, ours) in divisions.iter().zip(organ.divisions()) {
        assert_eq!(d["name"], ours.name.as_str());
        assert_eq!(f(&d["lowNote"]) as u8, ours.low_note);
        assert_eq!(f(&d["highNote"]) as u8, ours.high_note);
        assert_eq!(d["enclosed"], ours.enclosed);
        let chests: Vec<u16> = d["windchests"]
            .as_array()
            .unwrap()
            .iter()
            .map(|c| f(c) as u16)
            .collect();
        assert_eq!(
            chests,
            ours.windchests.iter().map(|c| c.0).collect::<Vec<_>>()
        );
    }

    let stops = o["stops"].as_array().unwrap();
    assert_eq!(stops.len(), organ.stops().len());
    for (s, ours) in stops.iter().zip(organ.stops()) {
        assert_eq!(s["name"], ours.name.as_str());
        assert_eq!(s["family"], family_name(ours.family));
        assert_eq!(f(&s["footageNum"]) as i32, ours.footage.num());
        assert_eq!(f(&s["footageDen"]) as i32, ours.footage.den());
        assert_eq!(f(&s["division"]) as u16, ours.division.0);
        assert_eq!(s["compound"], ours.is_compound(), "{}", ours.name);
    }
}

#[test]
fn every_stop_is_voiced_exactly_as_1x_voiced_it() {
    let r = reference();
    let organ = demo_organ();
    for s in r["organ"]["stops"].as_array().unwrap() {
        let id = StopId(f(&s["id"]) as u16);
        let v = build_rank_voicing(&organ, id, Calibration::OneX).expect("a stop of this organ");
        let name = s["name"].as_str().unwrap();

        assert_eq!(f(&s["chest"]) as u16, v.chest.0, "{name}: chest");
        close(
            v.wind_sensitivity,
            f(&s["windSensitivity"]),
            0.0,
            &format!("{name}: wind sensitivity"),
        );

        let sp = &s["speech"];
        close(
            v.speech.attack_at_c2_sec,
            f(&sp["attackAtC2Sec"]),
            0.0,
            &format!("{name}: attack C2"),
        );
        close(
            v.speech.attack_at_c7_sec,
            f(&sp["attackAtC7Sec"]),
            0.0,
            &format!("{name}: attack C7"),
        );
        close(
            v.speech.release_at_c2_sec,
            f(&sp["releaseAtC2Sec"]),
            0.0,
            &format!("{name}: release C2"),
        );
        close(
            v.speech.release_at_c7_sec,
            f(&sp["releaseAtC7Sec"]),
            0.0,
            &format!("{name}: release C7"),
        );

        let partials = s["partials"].as_array().unwrap();
        assert_eq!(
            partials.len(),
            v.spectrum.partials.len(),
            "{name}: partial count"
        );
        for (k, (p, ours)) in partials.iter().zip(&v.spectrum.partials).enumerate() {
            let at = format!("{name}, partial {k}");
            assert_eq!(f(&p["seed"]) as u32, ours.seed, "{at}: seed");
            close(ours.ratio, f(&p["ratio"]), 0.0, &format!("{at}: ratio"));
            close(
                ours.rank_base_ratio,
                f(&p["rankBaseRatio"]),
                0.0,
                &format!("{at}: rank base ratio"),
            );
            close(
                ours.rank_ratio,
                f(&p["rankRatioToF0"]),
                0.0,
                &format!("{at}: rank ratio"),
            );
            // dB values near zero have no meaningful ulp scale.
            close(ours.amp_db, f(&p["ampDb"]), 1e-5, &format!("{at}: level"));
            close(ours.phase, f(&p["phase"]), 1e-6, &format!("{at}: phase"));
            close(
                ours.wind_sensitivity,
                f(&p["windSensitivity"]),
                0.0,
                &format!("{at}: wind"),
            );
            close(
                ours.onset_seconds,
                f(&p["onsetSeconds"]),
                1e-9,
                &format!("{at}: onset"),
            );
            close(
                ours.brightness_track,
                f(&p["brightnessTrack"]),
                0.0,
                &format!("{at}: brightness"),
            );
        }

        let formants = s["formants"].as_array().unwrap();
        assert_eq!(
            formants.len(),
            v.spectrum.formants.len(),
            "{name}: formants"
        );
        for (fm, ours) in formants.iter().zip(&v.spectrum.formants) {
            close(
                ours.center_hz,
                f(&fm["centerHz"]),
                0.0,
                &format!("{name}: formant centre"),
            );
            close(
                ours.gain_db,
                f(&fm["gainDb"]),
                0.0,
                &format!("{name}: formant gain"),
            );
            close(
                ours.bandwidth_hz,
                f(&fm["bandwidthHz"]),
                0.0,
                &format!("{name}: formant width"),
            );
        }
    }
}

#[test]
fn a_controller_plays_the_grand_orgue_and_opens_on_its_plenum() {
    let organ = demo_organ();
    let manual = organ.primary_manual();
    let division = organ.division(manual).unwrap();
    assert_eq!(division.name, "Grand-Orgue");
    assert_eq!(division.kind, DivisionKind::Manual);
    let names: Vec<&str> = organ
        .default_opening_registration(manual)
        .into_iter()
        .map(|id| organ.stop(id).unwrap().name.as_str())
        .collect();
    assert_eq!(
        names,
        [
            "Montre 16",
            "Montre 8",
            "Bourdon 8",
            "Prestant 4",
            "Doublette 2",
            "Fourniture IV"
        ]
    );
}
