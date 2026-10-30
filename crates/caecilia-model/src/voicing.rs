//! One rank's voicing: the calibrated spectrum and speech a voice is built from.

use crate::footage::Footage;
use crate::ids::{ChorusRole, DivisionId, StopId, TonalFamily, WindchestId};
use crate::organ::{Organ, Stop};
use crate::recipes;
use crate::spectrum::{SpectralModel, SpeechProfile};

/// The most partials one rank's voice carries.
pub const MAX_PARTIALS: usize = 48;

/// Fixed calibration from spectral level to voice amplitude: a single
/// Principal 8' lands at a known level and everything else grows around it.
/// Registration-independent by design -- that is what makes drawing more stops
/// genuinely louder.
const REGISTRATION_CALIBRATION: f32 = 0.0879;

/// Everything a voice needs to sound one rank.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct RankVoicing {
    /// The stop that draws it.
    pub stop: StopId,
    /// The division whose keys sound it.
    pub division: DivisionId,
    /// The chest whose pressure it reads.
    pub chest: WindchestId,
    /// Tonal family.
    pub family: TonalFamily,
    /// Length.
    pub footage: Footage,
    /// The 8'-referenced, calibrated spectrum.
    pub spectrum: SpectralModel,
    /// How fast it speaks.
    pub speech: SpeechProfile,
    /// How hard the rank breathes with the wind, from the organ file.
    pub wind_sensitivity: f32,
}

/// Whether a stop's recipe is already placed relative to 8' unison.
fn unison_referenced(stop: &Stop) -> bool {
    stop.family == TonalFamily::Mixture || stop.is_compound() || stop.footage.is_mutation()
}

/// The procedural recipe for a stop.
pub fn spectral_model_for_stop(stop: &Stop, stopped: bool) -> SpectralModel {
    let f = stop.footage;
    if stop.family == TonalFamily::Mixture || stop.is_compound() {
        return recipes::mixture(&stop.mixture, 1.0);
    }
    match stop.family {
        TonalFamily::Principal => recipes::principal(f, 1.0, 12),
        TonalFamily::Flute if stop.is_mutation() => recipes::mutation(f, 1.0),
        TonalFamily::Flute => recipes::flute(f, 1.0, stopped),
        // Colour-role strings are the célestes: they get a beat.
        TonalFamily::String => recipes::string(
            f,
            if stop.role == ChorusRole::Color {
                3.0
            } else {
                0.0
            },
            1.0,
        ),
        TonalFamily::Reed => recipes::reed(f, 1200.0, 1.0),
        _ if stop.is_mutation() => recipes::mutation(f, 1.0),
        _ => recipes::principal(f, 1.0, 12),
    }
}

/// A rank's level in the ensemble: a base per family, darkened 3.3 dB per
/// octave above 8'. Mixtures and mutations sit under the foundations.
fn rank_gain_db(stop: &Stop) -> f32 {
    if unison_referenced(stop) {
        return -11.0;
    }
    let base = match stop.family {
        TonalFamily::Principal => 0.0,
        TonalFamily::Flute => -1.5,
        TonalFamily::String => -4.0,
        TonalFamily::Reed | TonalFamily::Hybrid => -2.0,
        _ => -3.0,
    };
    base - 3.3 * stop.footage.octave_class_from_8() as f32
}

/// A rank's deterministic tuning offset in cents, keyed on (family, footage).
fn rank_detune_cents(family: TonalFamily, footage: Footage) -> f32 {
    let mut h: u32 = 0x9E37_79B9;
    h ^= (family as u32).wrapping_mul(2_654_435_761);
    h ^= (footage.num() as u32).wrapping_mul(40_503);
    h ^= (footage.den() as u32).wrapping_mul(2_246_822_519);
    h ^= h >> 16;
    h = h.wrapping_mul(0x7FEB_352D);
    h ^= h >> 15;
    let unit = (h & 0xFFFF) as f32 / 65535.0;
    (unit - 0.5) * 4.0
}

/// Tune a rank as a unit and give each partial a stable identity and phase.
///
/// The whole rank shifts together, fundamental included; the upper partials
/// get a further half-cent of scatter for a real pipe's slight inharmonicity.
/// `salt` keeps ranks that share a recipe apart: four Reed 8' ranks seeded
/// alike would sum coherently, 6 dB hot.
fn voice_rank(m: &mut SpectralModel, family: TonalFamily, footage: Footage, salt: u32) {
    let place = (((salt.wrapping_mul(2_654_435_761)) >> 20) & 0x3FF) as f32 / 1023.0 - 0.5;
    let rank_ratio = 2.0f32.powf((rank_detune_cents(family, footage) + place * 3.0) / 1200.0);

    let mut h: u32 = 0x85EB_CA6B
        ^ (family as u32).wrapping_mul(2_654_435_761)
        ^ (footage.num() as u32).wrapping_mul(2_246_822_519)
        ^ salt.wrapping_mul(0x9E37_79B9);
    for (i, p) in m.partials.iter_mut().enumerate() {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        // 1.x's own literal, kept so phases match its data bit for bit.
        #[allow(clippy::approx_constant)]
        let two_pi = 6.283_185_3_f32;
        p.phase = (h & 0xFFFF) as f32 / 65535.0 * two_pi;
        p.seed = h | 1;
        let mut ratio = rank_ratio;
        if i > 0 {
            let residual = ((h >> 16) & 0xFFFF) as f32 / 65535.0 - 0.5;
            ratio *= 2.0f32.powf(residual / 1200.0);
        }
        p.ratio *= ratio;
    }

    // The rank's own fundamental, read off the footage: the lowest partial
    // present is not it (a Nazard carries a trace at the unison).
    let feet = footage.feet();
    let base = if feet > 0.0 { (8.0 / feet) as f32 } else { 1.0 };
    for p in &mut m.partials {
        p.rank_base_ratio = base;
    }
}

/// How fast a family speaks, scaled by pipe length. Relative to a Principal:
/// reeds are promptest, narrow strings slowest.
pub fn speech_profile_for(family: TonalFamily, footage: Footage) -> SpeechProfile {
    let (mut attack, mut release) = match family {
        TonalFamily::Reed => (0.45f32, 0.75f32),
        TonalFamily::Flute => (1.35, 1.25),
        TonalFamily::String => (1.90, 1.15),
        TonalFamily::Mixture => (0.55, 0.70),
        _ => (1.0, 1.0),
    };
    let feet = footage.feet();
    if feet > 0.0 {
        let length = (feet / 8.0).powf(0.25) as f32;
        attack *= length;
        release *= length;
    }
    let p = SpeechProfile::default();
    SpeechProfile {
        attack_at_c2_sec: p.attack_at_c2_sec * attack,
        attack_at_c7_sec: p.attack_at_c7_sec * attack,
        release_at_c2_sec: p.release_at_c2_sec * release,
        release_at_c7_sec: p.release_at_c7_sec * release,
    }
}

/// A measured spectrum, peak-normalised so it contributes the SHAPE and the
/// organ keeps its own balance; else the procedural recipe.
fn measured_or_procedural(organ: &Organ, stop: &Stop) -> SpectralModel {
    let rank = organ.rank(stop.rank);
    let stopped = rank.is_none_or(|r| r.stopped);
    match rank.and_then(|r| r.measured_spectrum.as_ref()) {
        Some(m) if !m.partials.is_empty() => {
            let mut m = m.clone();
            let peak = m
                .partials
                .iter()
                .map(|p| p.amp_db)
                .fold(m.partials[0].amp_db, f32::max);
            for p in &mut m.partials {
                p.amp_db -= peak;
            }
            m
        }
        _ => spectral_model_for_stop(stop, stopped),
    }
}

/// Build one stop's voicing. `None` if the stop is not on this organ.
pub fn build_rank_voicing(organ: &Organ, id: StopId) -> Option<RankVoicing> {
    let s = organ.stop(id)?;
    let mut v = RankVoicing {
        stop: s.id,
        division: s.division,
        family: s.family,
        footage: s.footage,
        speech: speech_profile_for(s.family, s.footage),
        wind_sensitivity: 0.5,
        ..RankVoicing::default()
    };
    if let Some(rank) = organ.rank(s.rank) {
        v.chest = rank.windchest;
        v.wind_sensitivity = rank.character.wind_sensitivity;
    }

    let feet = s.footage.feet();
    let fold = if unison_referenced(s) || feet <= 0.0 {
        1.0
    } else {
        8.0 / feet
    };
    let gain_db = rank_gain_db(s);
    let recipe = measured_or_procedural(organ, s);
    v.spectrum.formants = recipe.formants.clone();
    v.spectrum.partials = recipe
        .partials
        .iter()
        .map(|p| {
            let mut t = *p;
            t.ratio = (f64::from(p.ratio) * fold) as f32;
            t.amp_db = p.amp_db + gain_db;
            t
        })
        .collect();
    voice_rank(&mut v.spectrum, s.family, s.footage, u32::from(id.0) + 1);

    let calibration_db = 20.0 * REGISTRATION_CALIBRATION.log10();
    for p in &mut v.spectrum.partials {
        p.amp_db += calibration_db;
    }
    v.spectrum.fundamental_hz = 0.0;
    Some(v)
}
