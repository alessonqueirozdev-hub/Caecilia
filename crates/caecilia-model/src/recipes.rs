//! Procedural spectra per tonal family.
//!
//! Each recipe is a rank's steady spectrum at its own pitch (ratio 1 is the
//! rank's fundamental) -- except the mixture and mutation recipes, which place
//! their partials at the compound or mutation ratios relative to 8' unison, so
//! a unison-pitched voice sounds them.
//!
//! Ported from 1.x as it stands, operation for operation in `f32`: the
//! reference data in `reference/cpp-1.x` checks these against the C++ to a few
//! ulps. Better voicing (per stop, from the organ file) comes on top of this,
//! not instead of it.

use crate::footage::Footage;
use crate::spectrum::{FormantPeak, PartialTrack, SpectralModel};

/// Nominal analysis pitch (about C3); replaced by the key's frequency at note-on.
const NOMINAL_FUNDAMENTAL_HZ: f32 = 130.81;

pub(crate) fn clampf(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// How harmonic development scales with pipe size: small pipes develop fewer
/// usable upper partials and roll off faster. Returns (harmonic-count
/// multiplier, extra roll-off).
fn scaling_for_footage(f: Footage) -> (f32, f32) {
    let feet = f.feet();
    let octaves_up = if feet > 0.0 {
        (8.0 / feet).log2() as f32
    } else {
        0.0
    };
    let c = clampf(octaves_up, -2.0, 3.0);
    (
        clampf(1.0 - 0.18 * c, 0.30, 1.45),
        clampf(1.0 + 0.06 * c, 0.85, 1.30),
    )
}

fn push(
    model: &mut SpectralModel,
    ratio: f32,
    amp_db: f32,
    wind_sensitivity: f32,
    onset_seconds: f32,
    brightness_track: f32,
    rank_ratio: f32,
) {
    model.partials.push(PartialTrack {
        ratio,
        rank_base_ratio: 1.0,
        amp_db,
        phase: 0.0,
        wind_sensitivity,
        onset_seconds,
        brightness_track,
        seed: 0,
        rank_ratio,
    });
}

fn empty() -> SpectralModel {
    SpectralModel {
        fundamental_hz: NOMINAL_FUNDAMENTAL_HZ,
        ..SpectralModel::default()
    }
}

/// A principal: a full harmonic series rolling off at about -6 dB/octave.
pub fn principal(footage: Footage, brightness: f32, num_harmonics: usize) -> SpectralModel {
    let mut m = empty();
    let scale = scaling_for_footage(footage);
    let rolloff = clampf(1.35 - 0.35 * clampf(brightness, 0.0, 2.0), 0.55, 1.6) * scale.1;
    let scaled = (num_harmonics as f32 * scale.0 + 0.5) as usize;
    for n in 1..=scaled.max(1) {
        let fn_ = n as f32;
        let amp_db = -20.0 * rolloff * fn_.log10();
        let wind = clampf(0.30 + 0.03 * fn_, 0.0, 0.60);
        let onset = 0.0015 * (fn_ - 1.0);
        let bright = clampf(0.02 * fn_, 0.0, 0.4);
        push(&mut m, fn_, amp_db, wind, onset, bright, 0.0);
    }
    m
}

/// A flute: a strong, pure fundamental; odd harmonics only when stopped.
pub fn flute(footage: Footage, brightness: f32, stopped: bool) -> SpectralModel {
    let mut m = empty();
    let extra = -6.0 * (scaling_for_footage(footage).1 - 1.0) * 10.0;
    push(&mut m, 1.0, 0.0, 0.20, 0.0, 0.01, 0.0);
    if stopped {
        for (idx, n) in [3.0f32, 5.0, 7.0, 9.0].into_iter().enumerate() {
            let i = idx as f32;
            let amp_db = -16.0 - 6.0 * i + 3.0 * (brightness - 1.0) + extra;
            push(&mut m, n, amp_db, 0.18, 0.002 * i, 0.02, 0.0);
        }
    } else {
        push(
            &mut m,
            2.0,
            -14.0 + 3.0 * (brightness - 1.0) + extra,
            0.18,
            0.001,
            0.02,
            0.0,
        );
        push(&mut m, 3.0, -22.0 + extra, 0.18, 0.002, 0.02, 0.0);
        push(&mut m, 4.0, -30.0 + extra, 0.18, 0.003, 0.02, 0.0);
    }
    m
}

/// A string: slow roll-off, an emphasis band on harmonics 2-6, slow speech; a
/// non-zero detune doubles every partial into a céleste.
pub fn string(footage: Footage, detune_cents: f32, brightness: f32) -> SpectralModel {
    let mut m = empty();
    let scale = scaling_for_footage(footage);
    let harmonics = (16.0 * scale.0 + 0.5) as usize;
    let rolloff = clampf(0.70 - 0.10 * (brightness - 1.0), 0.40, 1.0) * scale.1;
    let detune = 2.0f32.powf(detune_cents / 1200.0);
    let celeste = detune_cents.abs() > 1.0e-3;
    for n in 1..=harmonics {
        let fn_ = n as f32;
        let mut amp_db = -20.0 * rolloff * fn_.log10();
        if (2..=6).contains(&n) {
            amp_db += 2.0;
        }
        let wind = clampf(0.35 + 0.02 * fn_, 0.0, 0.60);
        let onset = 0.004 * (fn_ - 1.0);
        let bright = clampf(0.03 * fn_, 0.0, 0.5);
        push(&mut m, fn_, amp_db, wind, onset, bright, 0.0);
        if celeste {
            push(&mut m, fn_ * detune, amp_db - 3.0, wind, onset, bright, 0.0);
        }
    }
    m
}

/// A reed: strong odd harmonics, an emphasised band on 4-8, prompt speech,
/// and three fixed-Hz formants (body, snarl, brilliance).
pub fn reed(footage: Footage, formant_center_hz: f32, brightness: f32) -> SpectralModel {
    let mut m = empty();
    let scale = scaling_for_footage(footage);
    let harmonics = (16.0 * scale.0 + 0.5) as usize;
    let rolloff = clampf(0.90 - 0.10 * (brightness - 1.0), 0.50, 1.2) * scale.1;
    for n in 1..=harmonics {
        let fn_ = n as f32;
        let mut amp_db = -20.0 * rolloff * fn_.log10();
        amp_db += if n % 2 == 1 { 4.0 } else { -3.0 };
        if (4..=8).contains(&n) {
            amp_db += 6.0;
        }
        let wind = clampf(0.40 + 0.02 * fn_, 0.0, 0.70);
        let onset = (if n == 1 { 0.001 } else { 0.0 }) + 0.003 * (fn_ - 1.0);
        let bright = clampf(0.03 * fn_, 0.0, 0.5);
        push(&mut m, fn_, amp_db, wind, onset, bright, 0.0);
    }
    let snarl = clampf(
        if formant_center_hz > 1.0 {
            formant_center_hz
        } else {
            1200.0
        },
        700.0,
        2000.0,
    );
    m.formants = vec![
        FormantPeak {
            center_hz: 450.0,
            gain_db: 2.5,
            bandwidth_hz: 400.0,
        },
        FormantPeak {
            center_hz: snarl,
            gain_db: 4.5,
            bandwidth_hz: 900.0,
        },
        FormantPeak {
            center_hz: 2600.0,
            gain_db: 2.0,
            bandwidth_hz: 1500.0,
        },
    ];
    m
}

/// A mixture: a faint grounding unison, then each rank and its octave.
pub fn mixture(ranks: &[Footage], brightness: f32) -> SpectralModel {
    let mut m = empty();
    push(&mut m, 1.0, -22.0, 0.40, 0.0, 0.02, 0.0);
    let boost = 2.0 * (brightness - 1.0);
    let mut idx = 0usize;
    for &f in ranks {
        let ratio = f.ratio_to_unison();
        if ratio <= 0.0 {
            continue;
        }
        let i = idx as f32;
        let amp_db = -4.0 - 3.0 * i + boost;
        push(&mut m, ratio, amp_db, 0.45, 0.001 * i, 0.05, ratio);
        // The octave above each rank is that pipe's second harmonic: it names
        // the rank's pitch and moves when the rank breaks.
        push(
            &mut m,
            ratio * 2.0,
            amp_db - 10.0,
            0.50,
            0.002 * i,
            0.06,
            ratio,
        );
        idx += 1;
    }
    m
}

/// A mutation: a faint true unison for blend, then the mutation pitch and two
/// of its own harmonics, all naming the same rank.
pub fn mutation(footage: Footage, brightness: f32) -> SpectralModel {
    let mut m = empty();
    let ratio = footage.ratio_to_unison();
    let boost = 2.0 * (brightness - 1.0);
    push(&mut m, 1.0, -20.0, 0.25, 0.0, 0.01, 0.0);
    if ratio > 0.0 {
        push(&mut m, ratio, 0.0 + boost, 0.30, 0.002, 0.02, ratio);
        push(&mut m, ratio * 2.0, -14.0, 0.30, 0.004, 0.03, ratio);
        push(&mut m, ratio * 3.0, -22.0, 0.35, 0.006, 0.04, ratio);
    }
    m
}
