// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The timbre a voice is seeded from.

/// One partial of a rank's steady spectrum.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PartialTrack {
    /// Frequency as a ratio to the 8' key frequency.
    pub ratio: f32,
    /// Ratio of the fundamental of THIS partial's own rank: a 2' rank's
    /// fundamental arrives as ratio 4, and anything reasoning about harmonic
    /// number divides by this first. 1 means unison or unknown.
    pub rank_base_ratio: f32,
    /// Steady level in dB.
    pub amp_db: f32,
    /// Start phase in radians.
    pub phase: f32,
    /// How strongly this partial's pitch tracks the wind.
    pub wind_sensitivity: f32,
    /// Delay before this partial speaks.
    pub onset_seconds: f32,
    /// How much more (or less) than its family this partial dulls as the wind
    /// gives way, applied as `1 + brightness_track`.
    pub brightness_track: f32,
    /// Stable identity, independent of where the partial lands in a composite:
    /// voices derive their start phase and drift stream from it.
    pub seed: u32,
    /// Sounding pitch of the breaking rank this partial belongs to, as a ratio
    /// to 8'; zero when it does not belong to one. A harmonic of a mixture rank
    /// carries the RANK's pitch, so it moves when the pipe does.
    pub rank_ratio: f32,
}

/// One resonant peak of a fixed-frequency formant envelope (reeds).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FormantPeak {
    /// Centre in Hz.
    pub center_hz: f32,
    /// Peak gain in dB.
    pub gain_db: f32,
    /// -3 dB bandwidth in Hz.
    pub bandwidth_hz: f32,
}

/// Maximum formant peaks in an envelope.
pub const MAX_FORMANTS: usize = 4;

/// A rank's steady spectrum.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct SpectralModel {
    /// The partials. Off the audio thread only.
    pub partials: Vec<PartialTrack>,
    /// Fixed-Hz formant peaks, in order; empty for most families.
    pub formants: Vec<FormantPeak>,
    /// Fundamental the analysis was taken at; zero once calibrated per note.
    pub fundamental_hz: f32,
}

/// The pitches mixture ranks are drawn from, as ratios to 8': unison, octave,
/// twelfth, fifteenth, nineteenth, twenty-second... A composition is always a
/// run of consecutive entries, which is what lets a break be arithmetic: it
/// moves the whole run one entry down.
pub const MIXTURE_SERIES: [f32; 10] = [1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0];

/// Where `ratio` sits in [`MIXTURE_SERIES`], or `None` if it is not a member.
pub fn mixture_series_index(ratio: f32) -> Option<usize> {
    MIXTURE_SERIES
        .iter()
        .position(|&s| ratio > s * 0.995 && ratio < s * 1.005)
}

/// How fast a rank speaks and dies away, at the two ends of the compass; the
/// voice interpolates by pitch in between.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SpeechProfile {
    /// Attack of a ~65 Hz pipe.
    pub attack_at_c2_sec: f32,
    /// Attack of a ~2 kHz pipe.
    pub attack_at_c7_sec: f32,
    /// Release of a ~65 Hz pipe.
    pub release_at_c2_sec: f32,
    /// Release of a ~2 kHz pipe.
    pub release_at_c7_sec: f32,
}

impl Default for SpeechProfile {
    /// A Principal's.
    fn default() -> Self {
        Self {
            attack_at_c2_sec: 0.055,
            attack_at_c7_sec: 0.010,
            release_at_c2_sec: 0.300,
            release_at_c7_sec: 0.110,
        }
    }
}
