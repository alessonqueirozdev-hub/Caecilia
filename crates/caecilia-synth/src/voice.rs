// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The additive rank voice: one pipe of one rank, sounded by a partial bank.

use caecilia_model::{Calibration, MAX_PARTIALS, PipeId, RankVoicing, StopId, TonalFamily};
use caecilia_wind::{WindSnapshot, response_for};
use manguebyte_dsp::{PipeVoicing, WindJitter};

use crate::bank::{Liveliness, PartialBank, ReleaseStyle};

/// 2.0's control rate: the wind, drift and speech are re-read every 64 frames
/// (750 Hz at 48 kHz), whatever buffer the host hands over.
pub const CONTROL_FRAMES: usize = 64;

/// Headroom baked into every voice, so dozens sum under the ceiling by static
/// gain alone -- no bus compressor, no polyphony duck. The tutti stays clean
/// because the voices are decorrelated and sum as `sqrt(N)`.
const VOICE_HEADROOM: f32 = 0.25;

/// The living-pipe character of each family.
///
/// 1.x defines these per family but applies them only once, at pool setup, with
/// the pool's family (a Principal): `adoptRank` never re-applies them, so every
/// rank of 1.x speaks with a Principal's drift, glide and tilt. The port keeps
/// that until the references are matched; [`AdditiveVoice::set_per_family_liveliness`]
/// is the 2.0 behaviour.
pub fn liveliness_for(family: TonalFamily) -> Liveliness {
    let (instability_cents, attack_glide_cents, max_bloom_seconds, hf_rolloff_hz, treble_tilt_db) =
        match family {
            TonalFamily::Principal => (5.5, -18.0, 0.012, 7000.0, 6.0),
            TonalFamily::Flute => (3.5, -12.0, 0.011, 8000.0, 7.5),
            TonalFamily::String => (7.5, -8.0, 0.010, 6500.0, 3.0),
            TonalFamily::Reed => (6.0, -22.0, 0.012, 6000.0, 4.0),
            TonalFamily::Mixture => (8.0, -14.0, 0.010, 6000.0, 2.5),
            _ => (5.5, -16.0, 0.011, 7000.0, 6.0),
        };
    Liveliness {
        instability_cents,
        attack_glide_cents,
        max_bloom_seconds,
        hf_rolloff_hz,
        treble_tilt_db,
    }
}

/// How each family speaks and breathes, voiced partial by partial. The flue
/// principal is Manguebyte's starting point; the others are judgements made from
/// it, each moved the way its pipes differ, and all of it wants setting by ear:
/// a flute is breathier and softer in its speech, a string slower and narrow, a
/// reed prompt with hardly any air, a mixture quick and high.
pub fn pipe_voicing(family: TonalFamily) -> PipeVoicing {
    let principal = PipeVoicing::FLUE_PRINCIPAL;
    match family {
        TonalFamily::Flute => PipeVoicing {
            air_db: -32.0,
            air_top_db: -14.0,
            rise_seconds: 0.036,
            overshoot: 0.12,
            breath: 0.3,
            breath_seconds: 0.05,
            ..principal
        },
        TonalFamily::String => PipeVoicing {
            air_db: -42.0,
            rise_seconds: 0.055,
            rise_per_octave: 0.7,
            lag_seconds: 0.006,
            overshoot: 0.2,
            breath: 0.12,
            breath_bandwidth_hz: 400.0,
            pitch_cents: -4.0,
            ..principal
        },
        TonalFamily::Reed => PipeVoicing {
            air_db: -52.0,
            air_top_db: -32.0,
            rise_seconds: 0.012,
            rise_per_octave: 0.85,
            lag_seconds: 0.001,
            overshoot: 0.35,
            settle_ratio: 1.5,
            breath: 0.05,
            pitch_cents: -10.0,
            pitch_seconds: 0.02,
            jitter: WindJitter {
                cents: 0.8,
                db: 0.05,
                rate_hz: 6.0,
            },
            ..principal
        },
        TonalFamily::Mixture => PipeVoicing {
            air_db: -40.0,
            rise_seconds: 0.02,
            breath: 0.15,
            ..principal
        },
        _ => principal,
    }
}

/// One voice of the pool. It becomes a rank with [`adopt_rank`](Self::adopt_rank)
/// and sounds a pipe of it with [`note_on`](Self::note_on).
#[derive(Clone, Debug)]
pub struct AdditiveVoice {
    bank: PartialBank,
    pipe: PipeId,
    stop: StopId,
    family: TonalFamily,
    per_family_liveliness: bool,
    calibration: Calibration,
    voiced: bool,
}

impl AdditiveVoice {
    /// A voice able to become any rank, voiced under `calibration`.
    /// Allocates: off the audio thread.
    pub fn new(sample_rate: f64, calibration: Calibration) -> Self {
        let mut bank = PartialBank::new(sample_rate, MAX_PARTIALS);
        bank.set_liveliness(liveliness_for(TonalFamily::Principal));
        bank.set_wind_coupling(
            Default::default(),
            response_for(TonalFamily::Principal, calibration),
        );
        bank.set_chiff(0.0);
        let v2 = calibration == Calibration::V2;
        if v2 {
            bank.set_control_frames(CONTROL_FRAMES);
            bank.set_release_style(ReleaseStyle::Exponential);
        }
        Self {
            bank,
            pipe: PipeId::default(),
            stop: StopId::default(),
            family: TonalFamily::Principal,
            per_family_liveliness: v2,
            calibration,
            voiced: false,
        }
    }

    /// Give each rank its own family's character instead of 1.x's single one.
    pub fn set_per_family_liveliness(&mut self, on: bool) {
        self.per_family_liveliness = on;
    }

    /// Have it speak and breathe partial by partial, as its family does, from
    /// the next rank it becomes on: air, each partial's attack, the wind's
    /// unsteadiness, and no note the same twice.
    pub fn set_voiced(&mut self, on: bool) {
        self.voiced = on;
    }

    /// How far its partials spread across the stereo image; 0 makes the voice
    /// one point, for placing it in a room as a whole.
    pub fn set_stereo_spread(&mut self, spread: f32) {
        self.bank.set_stereo_spread(spread);
    }

    /// Become one rank: its spectrum, its speech, its chest and its family's
    /// wind response scaled by the rank's own sensitivity (0.5 leaves the
    /// family curve alone). Allocation-free.
    pub fn adopt_rank(&mut self, v: &RankVoicing) {
        self.family = v.family;
        self.stop = v.stop;
        let mut curve = response_for(v.family, self.calibration);
        let k = 2.0 * v.wind_sensitivity.clamp(0.0, 1.0);
        curve.cents_per_deviation *= k;
        curve.db_per_deviation *= k;
        curve.brightness_per_deviation *= k;
        curve.attack_per_deviation *= k;
        self.bank.set_wind_coupling(v.chest, curve);
        self.bank.set_speech_profile(v.speech);
        self.bank
            .set_voicing(self.voiced.then(|| pipe_voicing(v.family)));
        if self.per_family_liveliness {
            self.bank.set_liveliness(liveliness_for(v.family));
        }
        self.bank.seed(&v.spectrum);
    }

    /// Sound `pipe` at `frequency_hz` (the key's 8' frequency: the rank's
    /// footage is already in its spectrum).
    pub fn note_on(&mut self, pipe: PipeId, frequency_hz: f64, wind: Option<&WindSnapshot>) {
        self.pipe = pipe;
        self.bank.set_master_gain(VOICE_HEADROOM);
        self.bank.trigger(pipe, frequency_hz, wind);
    }

    /// Let it go.
    pub fn note_off(&mut self) {
        self.bank.release();
    }

    /// Stop dead.
    pub fn silence(&mut self) {
        self.bank.silence();
    }

    /// This block's swell-shoe ramp.
    pub fn set_expression(&mut self, start_gain: f32, inc_per_sample: f32) {
        self.bank.set_expression(start_gain, inc_per_sample);
    }

    /// Render one block, adding. `frame_in_block` is where this slice starts
    /// in the wind's block.
    pub fn render_add(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        wind: Option<&WindSnapshot>,
        frame_in_block: usize,
    ) {
        self.bank.render_add(left, right, wind, frame_in_block);
    }

    /// Whether it sounds.
    pub fn is_active(&self) -> bool {
        self.bank.is_active()
    }

    /// The pipe it sounds.
    pub fn pipe(&self) -> PipeId {
        self.pipe
    }

    /// The stop whose rank it adopted.
    pub fn stop(&self) -> StopId {
        self.stop
    }

    /// Its rank's family.
    pub fn family(&self) -> TonalFamily {
        self.family
    }

    /// How much would be lost if it stopped.
    pub fn level_estimate(&self) -> f32 {
        self.bank.level_estimate()
    }

    /// Relative render cost, proportional to the partial count.
    pub fn cpu_cost_estimate(&self) -> f32 {
        0.5 + 0.02 * self.bank.partial_count() as f32
    }

    /// The bank, for tests and tools.
    pub fn bank(&self) -> &PartialBank {
        &self.bank
    }
}
