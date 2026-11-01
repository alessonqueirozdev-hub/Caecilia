// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organ file: an instrument as a JSON document.
//!
//! The format is 1.x's (`docs/ORGAN_FILE.md`): 1.x documents load as they
//! are. 2.0 adds a rank's own `recipe` (brightness, formant, fundamental) and
//! a `formatVersion`; 1.x ignores both. Nothing is loaded until the whole
//! document has parsed and validated, so a file that will not open leaves the
//! organ being played exactly where it was.

use std::collections::BTreeMap;
use std::path::{Component, Path};

use serde_json::{Map, Value};

use crate::footage::Footage;
use crate::ids::{
    ChorusRole, CouplerId, DivisionId, PitchClass, RankId, StopId, TonalFamily, WindchestId,
};
use crate::organ::{
    Coupler, Division, DivisionKind, Manual, Organ, Rank, RankCharacter, RecipeOverride, Stop,
    Windchest,
};
use crate::spectrum::{PartialTrack, SpectralModel};

/// The format version this build writes. Documents without one are 1.x's.
pub const FORMAT_VERSION: u32 = 2;

/// The most stops an organ can have: the registration the audio thread reads
/// is one 64-bit word.
pub const MAX_STOPS: usize = 64;

/// The most couplers: past the sixteenth no surface can engage one.
pub const MAX_COUPLERS: usize = 16;

/// Something said about a document, at a place in it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Diagnostic {
    /// Where: `stop[14] 'Bombarde 16'`.
    pub path: String,
    /// What.
    pub message: String,
}

impl std::fmt::Display for Diagnostic {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.path.is_empty() {
            write!(f, "{}", self.message)
        } else {
            write!(f, "{}: {}", self.path, self.message)
        }
    }
}

/// Why a document did not load.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OrganFileError {
    /// It is not JSON.
    Syntax {
        /// 1-based line.
        line: usize,
        /// 1-based column.
        column: usize,
        /// What the parser said.
        message: String,
    },
    /// It is JSON but not an organ: every error, and the warnings beside them.
    Invalid {
        /// What stops it loading.
        errors: Vec<Diagnostic>,
        /// What would only have been reported.
        warnings: Vec<Diagnostic>,
    },
    /// The file could not be read.
    Io(String),
}

impl std::fmt::Display for OrganFileError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Syntax {
                line,
                column,
                message,
            } => write!(f, "line {line}, column {column}: {message}"),
            Self::Invalid { errors, .. } => {
                write!(
                    f,
                    "{}",
                    errors.first().map_or_else(String::new, ToString::to_string)
                )?;
                if errors.len() > 1 {
                    write!(f, " (and {} more)", errors.len() - 1)?;
                }
                Ok(())
            }
            Self::Io(e) => write!(f, "{e}"),
        }
    }
}

impl std::error::Error for OrganFileError {}

/// A loaded organ and what was said about it on the way.
#[derive(Clone, Debug)]
pub struct OrganDocument {
    /// The instrument.
    pub organ: Organ,
    /// What did not stop the load.
    pub warnings: Vec<Diagnostic>,
}

#[derive(Default)]
struct Report {
    errors: Vec<Diagnostic>,
    warnings: Vec<Diagnostic>,
}

impl Report {
    fn error(&mut self, path: &str, message: impl Into<String>) {
        self.errors.push(Diagnostic {
            path: path.into(),
            message: message.into(),
        });
    }
    fn warn(&mut self, path: &str, message: impl Into<String>) {
        self.warnings.push(Diagnostic {
            path: path.into(),
            message: message.into(),
        });
    }
}

fn str_of<'a>(o: &'a Map<String, Value>, key: &str) -> Option<&'a str> {
    o.get(key).and_then(Value::as_str)
}

fn num_of(o: &Map<String, Value>, key: &str, default: f64, at: &str, r: &mut Report) -> f64 {
    match o.get(key) {
        None => default,
        Some(v) => v.as_f64().unwrap_or_else(|| {
            r.error(at, format!("'{key}' must be a number"));
            default
        }),
    }
}

fn bool_of(o: &Map<String, Value>, key: &str, default: bool, at: &str, r: &mut Report) -> bool {
    match o.get(key) {
        None => default,
        Some(v) => v.as_bool().unwrap_or_else(|| {
            r.error(at, format!("'{key}' must be true or false"));
            default
        }),
    }
}

fn note_of(o: &Map<String, Value>, key: &str, default: u8, at: &str, r: &mut Report) -> u8 {
    let n = num_of(o, key, f64::from(default), at, r);
    if (0.0..=127.0).contains(&n) && n.fract() == 0.0 {
        n as u8
    } else {
        r.error(at, format!("'{key}' must be a MIDI note, 0 to 127"));
        default
    }
}

/// `8`, `2.667` or `[8, 3]`.
fn footage_value(v: &Value) -> Option<Footage> {
    match v {
        Value::Array(a) if a.len() == 2 => {
            let (n, d) = (a[0].as_i64()?, a[1].as_i64()?);
            (n > 0 && d > 0).then(|| Footage::new(n as i32, d as i32))
        }
        Value::Number(n) => {
            let f = n.as_f64()?;
            if f <= 0.0 {
                None
            } else if f.fract() == 0.0 {
                Some(Footage::new(f as i32, 1))
            } else {
                Some(Footage::from_feet(f))
            }
        }
        _ => None,
    }
}

fn footage_of(o: &Map<String, Value>, at: &str, r: &mut Report) -> Footage {
    match o.get("footage") {
        None => Footage::EIGHT,
        Some(v) => footage_value(v).unwrap_or_else(|| {
            r.error(
                at,
                "'footage' is feet: a positive number or [numerator, denominator]",
            );
            Footage::EIGHT
        }),
    }
}

fn family_of(o: &Map<String, Value>, at: &str, r: &mut Report) -> TonalFamily {
    match str_of(o, "family") {
        None => TonalFamily::Principal,
        Some(s) => match s {
            "Principal" => TonalFamily::Principal,
            "Flute" => TonalFamily::Flute,
            "String" => TonalFamily::String,
            "Reed" => TonalFamily::Reed,
            "Mixture" => TonalFamily::Mixture,
            "Hybrid" => TonalFamily::Hybrid,
            "Percussion" => TonalFamily::Percussion,
            "Undefined" => TonalFamily::Undefined,
            other => {
                r.warn(at, format!("unknown family '{other}', taken as Undefined"));
                TonalFamily::Undefined
            }
        },
    }
}

fn family_name(f: TonalFamily) -> &'static str {
    match f {
        TonalFamily::Principal => "Principal",
        TonalFamily::Flute => "Flute",
        TonalFamily::String => "String",
        TonalFamily::Reed => "Reed",
        TonalFamily::Mixture => "Mixture",
        TonalFamily::Hybrid => "Hybrid",
        TonalFamily::Percussion => "Percussion",
        TonalFamily::Undefined => "Undefined",
    }
}

const PITCH_CLASSES: [(&str, PitchClass); 6] = [
    ("Sub", PitchClass::Sub),
    ("Unison", PitchClass::Unison),
    ("Octave", PitchClass::Octave),
    ("Super", PitchClass::Super),
    ("Mutation", PitchClass::Mutation),
    ("Compound", PitchClass::Compound),
];

const ROLES: [(&str, ChorusRole); 7] = [
    ("Foundation", ChorusRole::Foundation),
    ("Chorus", ChorusRole::Chorus),
    ("MixtureCrown", ChorusRole::MixtureCrown),
    ("Solo", ChorusRole::Solo),
    ("Color", ChorusRole::Color),
    ("Effect", ChorusRole::Effect),
    ("Undefined", ChorusRole::Undefined),
];

fn enum_of<T: Copy>(
    o: &Map<String, Value>,
    key: &str,
    table: &[(&str, T)],
    default: T,
    at: &str,
    r: &mut Report,
) -> T {
    match str_of(o, key) {
        None => default,
        Some(s) => table
            .iter()
            .find(|(n, _)| *n == s)
            .map(|&(_, v)| v)
            .unwrap_or_else(|| {
                let names: Vec<&str> = table.iter().map(|(n, _)| *n).collect();
                r.error(
                    at,
                    format!("unknown {key} '{s}' (one of {})", names.join(", ")),
                );
                default
            }),
    }
}

fn name_of<'a>(o: &'a Map<String, Value>, at: &str, r: &mut Report) -> &'a str {
    match str_of(o, "name") {
        Some(n) if !n.trim().is_empty() => n,
        _ => {
            r.error(at, "needs a 'name'");
            ""
        }
    }
}

fn objects<'a>(
    root: &'a Map<String, Value>,
    key: &str,
    r: &mut Report,
) -> Vec<&'a Map<String, Value>> {
    match root.get(key) {
        None => Vec::new(),
        Some(Value::Array(a)) => a
            .iter()
            .enumerate()
            .filter_map(|(i, v)| {
                v.as_object().or_else(|| {
                    r.error(&format!("{key}[{i}]"), "must be an object");
                    None
                })
            })
            .collect(),
        Some(_) => {
            r.error(key, "must be an array");
            Vec::new()
        }
    }
}

/// A spectrum file's path, relative and staying inside the organ's folder.
/// An organ file travels; one that could name an absolute path, or climb out
/// with `..`, would read its recipient's disk.
fn safe_relative(p: &str) -> bool {
    let path = Path::new(p);
    !p.is_empty()
        && path
            .components()
            .all(|c| matches!(c, Component::Normal(_) | Component::CurDir))
}

/// Read a measured spectrum document (`caecilia-partial-extractor`'s).
pub fn read_spectrum(text: &str) -> Result<SpectralModel, String> {
    let v: Value = serde_json::from_str(text).map_err(|e| e.to_string())?;
    let o = v
        .as_object()
        .ok_or("a spectrum document is a JSON object")?;
    let f0 = o
        .get("fundamentalHz")
        .and_then(Value::as_f64)
        .ok_or("needs 'fundamentalHz'")?;
    if !(f0 > 0.0 && f0.is_finite()) {
        return Err("fundamentalHz must be a positive, finite frequency".into());
    }
    let partials = o
        .get("partials")
        .and_then(Value::as_array)
        .ok_or("needs a 'partials' array")?;
    if partials.is_empty() {
        return Err("the 'partials' array is empty; there is no timbre here".into());
    }
    let mut m = SpectralModel {
        fundamental_hz: f0 as f32,
        ..SpectralModel::default()
    };
    for (i, p) in partials.iter().enumerate() {
        let p = p
            .as_object()
            .ok_or(format!("partials[{i}] is not an object"))?;
        let get = |k: &str, d: f64| p.get(k).and_then(Value::as_f64).unwrap_or(d) as f32;
        let ratio = p
            .get("ratioToF0")
            .and_then(Value::as_f64)
            .ok_or(format!("partials[{i}] needs 'ratioToF0'"))?;
        m.partials.push(PartialTrack {
            ratio: ratio as f32,
            rank_base_ratio: 1.0,
            amp_db: get("ampDb", 0.0),
            phase: get("phase", 0.0),
            wind_sensitivity: get("windSensitivity", 0.0),
            onset_seconds: get("onsetSeconds", 0.0),
            ..PartialTrack::default()
        });
    }
    Ok(m)
}

/// Read an organ from its document. `folder` is where it lives, for the
/// measured spectra it names; without one, spectra are reported and skipped.
pub fn read_organ(text: &str, folder: Option<&Path>) -> Result<OrganDocument, OrganFileError> {
    // A byte-order mark is how Notepad and PowerShell save UTF-8; it is not JSON.
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let value: Value = serde_json::from_str(text).map_err(|e| OrganFileError::Syntax {
        line: e.line(),
        column: e.column(),
        message: e.to_string(),
    })?;
    let mut r = Report::default();
    let Some(root) = value.as_object() else {
        return Err(OrganFileError::Invalid {
            errors: vec![Diagnostic {
                path: String::new(),
                message: "an organ document is a JSON object".into(),
            }],
            warnings: Vec::new(),
        });
    };
    let version = root
        .get("formatVersion")
        .and_then(Value::as_u64)
        .unwrap_or(1);
    if version > u64::from(FORMAT_VERSION) {
        r.warn("formatVersion", format!("written by a newer Caecilia (format {version}); what this one does not know is ignored"));
    }

    let mut organ = Organ {
        name: str_of(root, "name").unwrap_or_default().to_string(),
        builder: str_of(root, "builder").unwrap_or_default().to_string(),
        year: num_of(root, "year", 0.0, "year", &mut r) as i32,
        ..Organ::default()
    };
    if organ.name.is_empty() {
        r.warn("name", "the organ has no name");
    }

    // Windchests.
    let mut chest_by_name = BTreeMap::new();
    for (i, c) in objects(root, "windchests", &mut r).into_iter().enumerate() {
        let at = format!("windchest[{i}]");
        let name = name_of(c, &at, &mut r).to_string();
        if chest_by_name
            .insert(name.clone(), WindchestId(i as u16))
            .is_some()
        {
            r.error(&at, format!("a second windchest called '{name}'"));
        }
        organ.windchests.push(Windchest {
            id: WindchestId(i as u16),
            name,
            nominal_pressure_pa: num_of(c, "pressurePa", 812.0, &at, &mut r) as f32,
            has_tremulant: bool_of(c, "tremulant", false, &at, &mut r),
            ranks: Vec::new(),
        });
    }
    if organ.windchests.is_empty() {
        r.warn("windchests", "no windchest: one is supplied at 812 Pa");
        organ.windchests.push(Windchest {
            id: WindchestId(0),
            name: "Main".into(),
            nominal_pressure_pa: 812.0,
            has_tremulant: false,
            ranks: Vec::new(),
        });
    }

    // Ranks.
    for (i, o) in objects(root, "ranks", &mut r).into_iter().enumerate() {
        let name = name_of(o, &format!("rank[{i}]"), &mut r).to_string();
        let at = format!("rank[{i}] '{name}'");
        let chest = match str_of(o, "windchest") {
            None | Some("") => WindchestId(0),
            Some(c) => chest_by_name.get(c).copied().unwrap_or_else(|| {
                r.error(&at, format!("references unknown windchest '{c}'"));
                WindchestId(0)
            }),
        };
        if let Some(engine) = str_of(o, "engine").filter(|e| *e != "Additive") {
            r.warn(
                &at,
                format!("engine '{engine}' is not built yet; the rank speaks additive"),
            );
        }
        if o.contains_key("sampleSet") {
            r.warn(
                &at,
                "sample sets are not played yet; the rank speaks from its recipe",
            );
        }
        let (mut low, mut high) = (
            note_of(o, "lowNote", 36, &at, &mut r),
            note_of(o, "highNote", 96, &at, &mut r),
        );
        if low > high {
            r.warn(&at, "an inverted compass, turned the right way up");
            std::mem::swap(&mut low, &mut high);
        }
        let family = family_of(o, &at, &mut r);
        let mut character = RankCharacter::default();
        if let Some(v) = o.get("voicing").and_then(Value::as_object) {
            let mut f = |k: &str, d: f32| num_of(v, k, f64::from(d), &at, &mut r) as f32;
            character = RankCharacter {
                chiff: f("chiff", character.chiff),
                harmonic_development: f("harmonicDevelopment", character.harmonic_development),
                brightness: f("brightness", character.brightness),
                wind_sensitivity: f("windSensitivity", character.wind_sensitivity),
                detune_scatter_cents: f("detuneScatterCents", character.detune_scatter_cents),
                level_scatter_db: f("levelScatterDb", character.level_scatter_db),
                brightness_scatter: f("brightnessScatter", character.brightness_scatter),
                attack_scatter_ms: f("attackScatterMs", character.attack_scatter_ms),
            };
        }
        let recipe = o
            .get("recipe")
            .and_then(Value::as_object)
            .map(|v| RecipeOverride {
                brightness: num_of(v, "brightness", 1.0, &at, &mut r) as f32,
                formant_hz: num_of(v, "formantHz", 1200.0, &at, &mut r) as f32,
                fundamental_db: num_of(v, "fundamentalDb", 0.0, &at, &mut r) as f32,
            });
        let spectrum_path = str_of(o, "spectrum").map(str::to_string);
        let mut measured = None;
        if let Some(p) = &spectrum_path {
            if !safe_relative(p) {
                r.error(
                    &at,
                    format!("spectrum '{p}' must be a path inside the organ's folder"),
                );
            } else if let Some(dir) = folder {
                match std::fs::read_to_string(dir.join(p))
                    .map_err(|e| e.to_string())
                    .and_then(|t| read_spectrum(&t))
                {
                    Ok(m) => measured = Some(m),
                    Err(e) => r.warn(
                        &at,
                        format!("spectrum '{p}' not used ({e}); the rank speaks from its recipe"),
                    ),
                }
            } else {
                r.warn(
                    &at,
                    format!("spectrum '{p}' not read: the document has no folder"),
                );
            }
        }
        organ.ranks.push(Rank {
            id: RankId(i as u16),
            name,
            family,
            footage: footage_of(o, &at, &mut r),
            stopped: bool_of(o, "stopped", true, &at, &mut r),
            windchest: chest,
            division: DivisionId(0),
            low_note: low,
            high_note: high,
            character,
            measured_spectrum: measured,
            spectrum_path,
            pan: num_of(o, "pan", 0.0, &at, &mut r) as f32,
            distance_m: num_of(o, "distanceM", 8.0, &at, &mut r) as f32,
            recipe,
        });
    }

    // Divisions.
    let mut division_by_name = BTreeMap::new();
    let mut manuals = Vec::new();
    for (i, o) in objects(root, "divisions", &mut r).into_iter().enumerate() {
        let name = name_of(o, &format!("division[{i}]"), &mut r).to_string();
        let at = format!("division[{i}] '{name}'");
        if division_by_name
            .insert(name.clone(), DivisionId(i as u16))
            .is_some()
        {
            r.error(&at, format!("a second division called '{name}'"));
        }
        let kind = enum_of(
            o,
            "kind",
            &[
                ("Manual", DivisionKind::Manual),
                ("Pedal", DivisionKind::Pedal),
                ("Floating", DivisionKind::Floating),
            ],
            DivisionKind::Manual,
            &at,
            &mut r,
        );
        let (mut low, mut high) = (
            note_of(o, "lowNote", 36, &at, &mut r),
            note_of(o, "highNote", 96, &at, &mut r),
        );
        if low > high {
            r.warn(&at, "an inverted compass, turned the right way up");
            std::mem::swap(&mut low, &mut high);
        }
        let channel = num_of(o, "midiChannel", -1.0, &at, &mut r) as i32;
        if !(-1..=15).contains(&channel) {
            r.error(&at, "'midiChannel' is 0 to 15, or -1 for none");
        }
        if kind != DivisionKind::Floating {
            manuals.push(Manual {
                division: DivisionId(i as u16),
                index: num_of(o, "manual", 0.0, &at, &mut r) as i32,
                midi_channel: channel,
                low_note: low,
                high_note: high,
            });
        }
        organ.divisions.push(Division {
            id: DivisionId(i as u16),
            name,
            kind,
            low_note: low,
            high_note: high,
            enclosed: bool_of(o, "enclosed", false, &at, &mut r),
            has_tremulant: bool_of(o, "tremulant", false, &at, &mut r),
            windchests: Vec::new(),
            stops: Vec::new(),
        });
    }
    if organ.divisions.is_empty() {
        r.error("divisions", "an organ needs at least one division");
    }
    organ.manuals = manuals;

    // Stops.
    let mut drawn = vec![false; organ.ranks.len()];
    for (i, o) in objects(root, "stops", &mut r).into_iter().enumerate() {
        let name = name_of(o, &format!("stop[{i}]"), &mut r).to_string();
        let at = format!("stop[{i}] '{name}'");
        if i == MAX_STOPS {
            r.error(
                &at,
                format!(
                    "is stop {}: an organ carries at most {MAX_STOPS}",
                    MAX_STOPS + 1
                ),
            );
        }
        let division = match str_of(o, "division") {
            Some(d) => division_by_name.get(d).copied().unwrap_or_else(|| {
                r.error(&at, format!("references unknown division '{d}'"));
                DivisionId(0)
            }),
            None => {
                r.error(&at, "needs a 'division'");
                DivisionId(0)
            }
        };
        let rank = match str_of(o, "rank") {
            None => {
                r.error(&at, "needs a 'rank'");
                RankId(0)
            }
            Some(reference) => {
                let (chest, rank_name) = match reference.split_once('/') {
                    Some((c, n)) if chest_by_name.contains_key(c) => {
                        (chest_by_name.get(c).copied(), n)
                    }
                    _ => (None, reference),
                };
                let candidates: Vec<&Rank> = organ
                    .ranks
                    .iter()
                    .filter(|k| k.name == rank_name && chest.is_none_or(|c| k.windchest == c))
                    .collect();
                match candidates.as_slice() {
                    [one] => one.id,
                    [] => {
                        r.error(&at, format!("references unknown rank '{reference}'"));
                        RankId(0)
                    }
                    _ => {
                        r.error(
                            &at,
                            format!("'{reference}' names {} ranks; qualify it with its chest, as \"chest/{rank_name}\"", candidates.len()),
                        );
                        RankId(0)
                    }
                }
            }
        };
        let mixture = match o.get("mixture") {
            None => Vec::new(),
            Some(Value::Array(a)) => a
                .iter()
                .filter_map(|v| {
                    footage_value(v).or_else(|| {
                        r.error(&at, "every 'mixture' entry is a footage");
                        None
                    })
                })
                .collect(),
            Some(_) => {
                r.error(&at, "'mixture' is an array of footages");
                Vec::new()
            }
        };
        if let Some(k) = organ.ranks.get_mut(rank.index()) {
            if !drawn[rank.index()] {
                k.division = division;
            }
            drawn[rank.index()] = true;
        }
        organ.stops.push(Stop {
            id: StopId(i as u16),
            name,
            family: family_of(o, &at, &mut r),
            footage: footage_of(o, &at, &mut r),
            pitch_class: enum_of(
                o,
                "pitchClass",
                &PITCH_CLASSES,
                PitchClass::Unison,
                &at,
                &mut r,
            ),
            role: enum_of(o, "role", &ROLES, ChorusRole::Foundation, &at, &mut r),
            division,
            rank,
            mixture,
        });
    }
    for (k, d) in organ.ranks.iter().zip(&drawn) {
        if !d {
            r.warn(
                &format!("rank[{}] '{}'", k.id.0, k.name),
                "no stop draws it: nobody can play it",
            );
        }
    }

    // Couplers.
    for (i, o) in objects(root, "couplers", &mut r).into_iter().enumerate() {
        let name = name_of(o, &format!("coupler[{i}]"), &mut r).to_string();
        let at = format!("coupler[{i}] '{name}'");
        if i == MAX_COUPLERS {
            r.error(
                &at,
                format!(
                    "is coupler {}: an organ carries at most {MAX_COUPLERS}",
                    MAX_COUPLERS + 1
                ),
            );
        }
        let mut division = |key: &str| match str_of(o, key) {
            Some(d) => division_by_name.get(d).copied().unwrap_or_else(|| {
                r.error(&at, format!("'{key}' names unknown division '{d}'"));
                DivisionId(0)
            }),
            None => {
                r.error(&at, format!("needs '{key}'"));
                DivisionId(0)
            }
        };
        let (from, to) = (division("from"), division("to"));
        if let Some(kind) = str_of(o, "kind").filter(|k| matches!(*k, "Melody" | "Bass")) {
            r.warn(
                &at,
                format!("a {kind} coupler is not built yet; it couples every key"),
            );
        }
        organ.couplers.push(Coupler {
            id: CouplerId(i as u16),
            name,
            from,
            to,
            semitones: num_of(o, "octaveShift", 0.0, &at, &mut r) as i32,
        });
    }

    // What follows from the references: each chest's ranks, each division's
    // stops and the chests they stand on.
    for k in &organ.ranks {
        if let Some(c) = organ.windchests.get_mut(k.windchest.index()) {
            c.ranks.push(k.id);
        }
    }
    for s in &organ.stops {
        let chest = organ.ranks.get(s.rank.index()).map(|k| k.windchest);
        if let Some(d) = organ.divisions.get_mut(s.division.index()) {
            d.stops.push(s.id);
            if let Some(c) = chest {
                if !d.windchests.contains(&c) {
                    d.windchests.push(c);
                }
            }
        }
    }
    for d in &mut organ.divisions {
        d.windchests.sort();
        if d.windchests.is_empty() {
            d.windchests.push(WindchestId(0));
        }
    }

    if r.errors.is_empty() {
        Ok(OrganDocument {
            organ,
            warnings: r.warnings,
        })
    } else {
        Err(OrganFileError::Invalid {
            errors: r.errors,
            warnings: r.warnings,
        })
    }
}

/// Read an organ file from disk; its spectra are found beside it.
pub fn read_organ_file(path: &Path) -> Result<OrganDocument, OrganFileError> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| OrganFileError::Io(format!("{}: {e}", path.display())))?;
    read_organ(&text, path.parent())
}

// --- Writing ---------------------------------------------------------------

/// JSON with its keys in the order a person reads them.
enum Out {
    Obj(Vec<(&'static str, Out)>),
    Arr(Vec<Out>),
    Str(String),
    Num(String),
    Bool(bool),
}

fn num(v: impl std::fmt::Display) -> Out {
    Out::Num(v.to_string())
}

fn footage_out(f: Footage) -> Out {
    if f.den() == 1 {
        num(f.num())
    } else {
        Out::Arr(vec![num(f.num()), num(f.den())])
    }
}

fn print(v: &Out, indent: usize, s: &mut String) {
    let pad = |n: usize| "  ".repeat(n);
    match v {
        Out::Str(t) => s.push_str(&serde_json::to_string(t).unwrap_or_default()),
        Out::Num(t) => s.push_str(t),
        Out::Bool(b) => s.push_str(if *b { "true" } else { "false" }),
        Out::Arr(a)
            if a.iter()
                .all(|x| matches!(x, Out::Num(_) | Out::Str(_) | Out::Bool(_) | Out::Arr(_))) =>
        {
            s.push('[');
            for (i, x) in a.iter().enumerate() {
                if i > 0 {
                    s.push_str(", ");
                }
                print(x, indent, s);
            }
            s.push(']');
        }
        Out::Arr(a) => {
            s.push_str("[\n");
            for (i, x) in a.iter().enumerate() {
                s.push_str(&pad(indent + 1));
                print(x, indent + 1, s);
                s.push_str(if i + 1 < a.len() { ",\n" } else { "\n" });
            }
            s.push_str(&pad(indent));
            s.push(']');
        }
        Out::Obj(o) => {
            s.push_str("{\n");
            for (i, (k, x)) in o.iter().enumerate() {
                s.push_str(&pad(indent + 1));
                s.push_str(&format!("\"{k}\": "));
                print(x, indent + 1, s);
                s.push_str(if i + 1 < o.len() { ",\n" } else { "\n" });
            }
            s.push_str(&pad(indent));
            s.push('}');
        }
    }
}

/// Write an organ as a document [`read_organ`] reads back to the same organ.
pub fn write_organ(organ: &Organ) -> String {
    let lookup = |list: &[(&'static str, PitchClass)], v: PitchClass| {
        list.iter()
            .find(|(_, x)| *x == v)
            .map_or("Unison", |(n, _)| *n)
    };
    let role = |v: ChorusRole| {
        ROLES
            .iter()
            .find(|(_, x)| *x == v)
            .map_or("Foundation", |(n, _)| *n)
    };
    let chest_name = |c: WindchestId| organ.windchest(c).map_or(String::new(), |w| w.name.clone());
    let division_name = |d: DivisionId| organ.division(d).map_or(String::new(), |x| x.name.clone());
    let rank_ref = |k: RankId| {
        let rank = &organ.ranks[k.index()];
        if organ.ranks.iter().filter(|x| x.name == rank.name).count() > 1 {
            format!("{}/{}", chest_name(rank.windchest), rank.name)
        } else {
            rank.name.clone()
        }
    };

    let chests = organ
        .windchests
        .iter()
        .map(|c| {
            Out::Obj(vec![
                ("name", Out::Str(c.name.clone())),
                ("pressurePa", num(c.nominal_pressure_pa)),
                ("tremulant", Out::Bool(c.has_tremulant)),
            ])
        })
        .collect();
    let ranks = organ
        .ranks
        .iter()
        .map(|k| {
            let c = &k.character;
            let mut o = vec![
                ("name", Out::Str(k.name.clone())),
                ("family", Out::Str(family_name(k.family).into())),
                ("footage", footage_out(k.footage)),
                ("windchest", Out::Str(chest_name(k.windchest))),
                ("stopped", Out::Bool(k.stopped)),
                ("lowNote", num(k.low_note)),
                ("highNote", num(k.high_note)),
                ("pan", num(k.pan)),
                ("distanceM", num(k.distance_m)),
                (
                    "voicing",
                    Out::Obj(vec![
                        ("chiff", num(c.chiff)),
                        ("harmonicDevelopment", num(c.harmonic_development)),
                        ("brightness", num(c.brightness)),
                        ("windSensitivity", num(c.wind_sensitivity)),
                        ("detuneScatterCents", num(c.detune_scatter_cents)),
                        ("levelScatterDb", num(c.level_scatter_db)),
                        ("brightnessScatter", num(c.brightness_scatter)),
                        ("attackScatterMs", num(c.attack_scatter_ms)),
                    ]),
                ),
            ];
            if let Some(p) = &k.spectrum_path {
                o.push(("spectrum", Out::Str(p.clone())));
            }
            if let Some(rc) = k.recipe {
                o.push((
                    "recipe",
                    Out::Obj(vec![
                        ("brightness", num(rc.brightness)),
                        ("formantHz", num(rc.formant_hz)),
                        ("fundamentalDb", num(rc.fundamental_db)),
                    ]),
                ));
            }
            Out::Obj(o)
        })
        .collect();
    let divisions = organ
        .divisions
        .iter()
        .map(|d| {
            let manual = organ.manuals.iter().find(|m| m.division == d.id);
            Out::Obj(vec![
                ("name", Out::Str(d.name.clone())),
                (
                    "kind",
                    Out::Str(
                        match d.kind {
                            DivisionKind::Manual => "Manual",
                            DivisionKind::Pedal => "Pedal",
                            DivisionKind::Floating => "Floating",
                        }
                        .into(),
                    ),
                ),
                ("lowNote", num(d.low_note)),
                ("highNote", num(d.high_note)),
                ("enclosed", Out::Bool(d.enclosed)),
                ("tremulant", Out::Bool(d.has_tremulant)),
                ("manual", num(manual.map_or(0, |m| m.index))),
                ("midiChannel", num(manual.map_or(-1, |m| m.midi_channel))),
            ])
        })
        .collect();
    let stops = organ
        .stops
        .iter()
        .map(|s| {
            let mut o = vec![
                ("name", Out::Str(s.name.clone())),
                ("family", Out::Str(family_name(s.family).into())),
                ("footage", footage_out(s.footage)),
                (
                    "pitchClass",
                    Out::Str(lookup(&PITCH_CLASSES, s.pitch_class).into()),
                ),
                ("role", Out::Str(role(s.role).into())),
                ("division", Out::Str(division_name(s.division))),
                ("rank", Out::Str(rank_ref(s.rank))),
            ];
            if !s.mixture.is_empty() {
                o.push((
                    "mixture",
                    Out::Arr(s.mixture.iter().map(|&f| footage_out(f)).collect()),
                ));
            }
            Out::Obj(o)
        })
        .collect();
    let couplers = organ
        .couplers
        .iter()
        .map(|c| {
            Out::Obj(vec![
                ("name", Out::Str(c.name.clone())),
                ("from", Out::Str(division_name(c.from))),
                ("to", Out::Str(division_name(c.to))),
                ("octaveShift", num(c.semitones)),
            ])
        })
        .collect();
    let doc = Out::Obj(vec![
        ("formatVersion", num(FORMAT_VERSION)),
        ("name", Out::Str(organ.name.clone())),
        ("builder", Out::Str(organ.builder.clone())),
        ("year", num(organ.year)),
        ("windchests", Out::Arr(chests)),
        ("ranks", Out::Arr(ranks)),
        ("divisions", Out::Arr(divisions)),
        ("stops", Out::Arr(stops)),
        ("couplers", Out::Arr(couplers)),
    ]);
    let mut s = String::new();
    print(&doc, 0, &mut s);
    s.push('\n');
    s
}
