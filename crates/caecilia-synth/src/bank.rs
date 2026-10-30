//! The wind-modulated partial bank: the loop-free modeled sustain.

use caecilia_model::{
    FormantPeak, MAX_FORMANTS, MIXTURE_SERIES, PipeId, SpectralModel, SpeechProfile, WindchestId,
    mixture_series_index,
};
use caecilia_wind::{WindModel, WindResponseCurve};

use crate::math::{
    Phasor, TABLES, cents_to_ratio_approx, db_to_linear, equal_power_pan, fast_sine_turns, hash32,
    next_bipolar, partial_accumulate, random_phasor, smoothstep01,
};

/// Frames one render chunk covers. Fixed rather than the host's block, because
/// this is memory multiplied by the pool.
const CHUNK_FRAMES: usize = 512;

/// The most partials a bank holds unless told otherwise.
pub const DEFAULT_MAX_PARTIALS: usize = 64;

/// Where mixture ranks stop climbing and break back.
const MIXTURE_CEILING_HZ: f64 = 6000.0;

/// dB of extra spectral tilt per octave of harmonic per unit of the wind's
/// brightness coefficient: the same unit the recipes' roll-off is written in.
const WIND_TILT_DB_PER_OCTAVE: f32 = 6.0;

/// The bloom reaches its longest 5.64 octaves above 120 Hz.
const BLOOM_OCTAVE_SPAN: f32 = 5.64;

/// The longest any partial waits to speak: a long stagger sweeps the
/// brightness up through the attack, an artificial "wah".
const MAX_ONSET_SECONDS: f32 = 0.004;

/// 2.0: extra decay of the upper partials while a pipe dies away, dB per
/// second per octave of harmonic. A resonator loses its upper modes faster --
/// wall losses grow with frequency, radiation faster still -- so a real pipe's
/// release darkens as it fades instead of shrinking at one colour.
const RELEASE_DARKEN_DB_PER_S_OCTAVE: f32 = 25.0;

/// 2.0: how far past the release time the exponential tail reaches -60 dB,
/// so it reaches -40 dB about when 1.x's shaped linear fade did.
const RELEASE_T60_FACTOR: f32 = 1.4;

/// Time constant of the slow per-partial pitch drift, in seconds (not blocks,
/// so the character does not depend on the host's buffer size).
const DRIFT_TAU_SECONDS: f32 = 0.15;

#[derive(Clone, Copy, Debug, Default)]
struct Partial {
    ratio: f32,
    amplitude: f32,
    wind_sensitivity: f32,
    onset_seconds: f32,
    osc: Phasor,
    cos_inc: f32,
    sin_inc: f32,
    block_gain: f32,
    cur_gain: f32,
    gain_inc: f32,
    seed: u32,
    rng: u32,
    drift_cents: f32,
    rank_ratio: f32,
    rank_series: Option<usize>,
    sounding_ratio: f32,
    log_ratio: f32,
    log_sounding: f32,
    pan_l: f32,
    pan_r: f32,
    bloom_seconds: f32,
    log_harmonic: f32,
    wind_bright_exp: f32,
    note_level_scale: f32,
    formant_gain: f32,
}

/// How the envelope dies away.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ReleaseStyle {
    /// 1.x: a straight line to zero under a smoothstep.
    #[default]
    Linear,
    /// 2.0: exponential, its upper partials dying faster than its lower.
    Exponential,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Stage {
    Idle,
    Attack,
    Sustain,
    Release,
}

/// How a stop's ranks move when they break back.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum BreakMode {
    /// Nothing breaks.
    None,
    /// Every rank moves the same number of steps down the organ series.
    Series,
    /// Ranks off the series (a tierce's 5) break by octaves.
    Octave,
}

/// The "living pipe" character: drift, speech glide, bloom and tilt.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Liveliness {
    /// Peak slow per-partial pitch drift, cents.
    pub instability_cents: f32,
    /// Pitch the speech starts from, cents (negative is flat).
    pub attack_glide_cents: f32,
    /// Longest per-partial attack bloom, seconds.
    pub max_bloom_seconds: f32,
    /// Corner of the gentle absolute-Hz top roll-off.
    pub hf_rolloff_hz: f32,
    /// Per-octave upper-harmonic roll on high notes, dB.
    pub treble_tilt_db: f32,
}

impl Default for Liveliness {
    fn default() -> Self {
        Self {
            instability_cents: 5.5,
            attack_glide_cents: -18.0,
            max_bloom_seconds: 0.060,
            hf_rolloff_hz: 7000.0,
            treble_tilt_db: 6.0,
        }
    }
}

/// N partials as quadrature phasors, coefficients updated once per block from
/// the wind, the drift and the speech; rendered partial-outer in chunks.
///
/// # Real time
///
/// [`PartialBank::new`] allocates. [`seed`](Self::seed), [`trigger`](Self::trigger),
/// [`release`](Self::release), [`render_add`](Self::render_add) and every setter
/// are allocation-free.
#[derive(Clone, Debug)]
pub struct PartialBank {
    partials: Vec<Partial>,
    count: usize,
    chunk_l: Vec<f32>,
    chunk_r: Vec<f32>,
    chunk_env: Vec<f32>,
    sample_rate: f64,
    fundamental_hz: f64,
    master_gain: f32,
    expr_gain: f32,
    expr_inc: f32,
    velocity_gain: f32,
    break_mode: BreakMode,
    break_top_ratio: f32,
    break_top_series: Option<usize>,
    break_max_shift: i32,
    stage: Stage,
    env_gain: f32,
    attack_step: f32,
    release_step: f32,
    attack_seconds: f32,
    release_seconds: f32,
    note_time: f64,
    live: Liveliness,
    glide_seconds: f32,
    speech: SpeechProfile,
    stereo_spread: f32,
    pan_l: f32,
    pan_r: f32,
    formants: [FormantPeak; MAX_FORMANTS],
    formant_count: usize,
    chiff_amount: f32,
    chiff_env: f32,
    chiff_lp: f32,
    chiff_lp_coef: f32,
    chiff_decay_mul: f32,
    chiff_samp: i32,
    chiff_atk_samp: i32,
    chiff_len_samp: i32,
    chiff_rng: u32,
    chest: WindchestId,
    curve: WindResponseCurve,
    /// Frames between coefficient updates; 0 means once per block (1.x).
    control_frames: usize,
    release_style: ReleaseStyle,
    release_mul: f32,
    /// Exponential release: the envelope already carries its shaping.
    released_shaped: bool,
    release_time: f64,
}

impl PartialBank {
    /// A silent bank able to hold `max_partials`. Allocates: off the audio thread.
    pub fn new(sample_rate: f64, max_partials: usize) -> Self {
        LazyTouch::touch();
        let mut b = Self {
            partials: vec![Partial::default(); max_partials],
            count: 0,
            chunk_l: vec![0.0; CHUNK_FRAMES],
            chunk_r: vec![0.0; CHUNK_FRAMES],
            chunk_env: vec![0.0; CHUNK_FRAMES],
            sample_rate: if sample_rate > 1.0 {
                sample_rate
            } else {
                48000.0
            },
            fundamental_hz: 0.0,
            master_gain: 0.25,
            expr_gain: 1.0,
            expr_inc: 0.0,
            velocity_gain: 1.0,
            break_mode: BreakMode::None,
            break_top_ratio: 0.0,
            break_top_series: None,
            break_max_shift: 0,
            stage: Stage::Idle,
            env_gain: 0.0,
            attack_step: 1.0,
            release_step: 1.0,
            attack_seconds: 0.016,
            release_seconds: 0.150,
            note_time: 0.0,
            live: Liveliness::default(),
            glide_seconds: 0.055,
            speech: SpeechProfile::default(),
            stereo_spread: 0.55,
            pan_l: std::f32::consts::FRAC_1_SQRT_2,
            pan_r: std::f32::consts::FRAC_1_SQRT_2,
            formants: [FormantPeak::default(); MAX_FORMANTS],
            formant_count: 0,
            chiff_amount: 0.0,
            chiff_env: 0.0,
            chiff_lp: 0.0,
            chiff_lp_coef: 0.3,
            chiff_decay_mul: 0.999,
            chiff_samp: 0,
            chiff_atk_samp: 96,
            chiff_len_samp: 0,
            chiff_rng: 0x0123_4567,
            chest: WindchestId(0),
            curve: WindResponseCurve::default(),
            control_frames: 0,
            release_style: ReleaseStyle::Linear,
            release_mul: 1.0,
            released_shaped: false,
            release_time: 0.0,
        };
        b.set_envelope_times(b.attack_seconds, b.release_seconds);
        b
    }

    /// Seed from a spectrum. A bank that is sounding keeps its phasors and
    /// glides its partials to the new levels, so re-voicing under a held note is
    /// a change of colour, not a cut.
    pub fn seed(&mut self, model: &SpectralModel) {
        let count = model.partials.len().min(self.partials.len());
        let sounding = self.stage != Stage::Idle;
        self.derive_break_mode(model, count);

        for (i, t) in model.partials[..count].iter().enumerate() {
            let p = &mut self.partials[i];
            p.ratio = t.ratio;
            p.amplitude = db_to_linear(t.amp_db);
            p.wind_sensitivity = t.wind_sensitivity;
            p.onset_seconds = t.onset_seconds;
            p.rank_ratio = t.rank_ratio;
            p.rank_series = if t.rank_ratio > 0.0 {
                mixture_series_index(t.rank_ratio)
            } else {
                None
            };
            p.sounding_ratio = t.ratio;
            p.log_ratio = t.ratio.max(1.0e-6).log2();
            p.log_sounding = p.log_ratio;
            p.block_gain = 0.0;
            p.gain_inc = 0.0;
            if !sounding || i >= self.count {
                let turns = t.phase * (1.0 / std::f32::consts::TAU);
                p.cur_gain = 0.0;
                p.osc = Phasor {
                    cos: fast_sine_turns(turns + 0.25),
                    sin: fast_sine_turns(turns),
                };
                p.cos_inc = 1.0;
                p.sin_inc = 0.0;
            }
            // The identity comes from the spectrum, not the array index, so a
            // registration sounds the same whatever order its stops were drawn.
            p.seed = if t.seed != 0 {
                t.seed
            } else {
                0x9E37_79B9 ^ (i as u32).wrapping_mul(2_654_435_761)
            };
            p.rng = if p.seed == 0 { 0x2545_F491 } else { p.seed };
            p.drift_cents = 0.0;
            p.bloom_seconds = 0.010;
            // Harmonic number counted from the RANK's fundamental: a 2' rank's
            // fundamental arrives as ratio 4 and is not a fourth harmonic.
            let base = if t.rank_base_ratio > 1.0e-6 {
                t.rank_base_ratio
            } else {
                1.0
            };
            let h = ((p.ratio / base + 0.5) as i32).max(1);
            p.log_harmonic = (h as f32).log2();
            p.wind_bright_exp = p.log_harmonic * (1.0 + t.brightness_track);
            p.note_level_scale = 1.0;
            p.formant_gain = 1.0;
        }
        // Partials the new spectrum no longer has fall silent.
        for p in &mut self.partials[count..self.count.max(count)] {
            p.block_gain = 0.0;
            p.amplitude = 0.0;
            p.cur_gain = 0.0;
        }
        self.count = count;
        if !sounding {
            self.fundamental_hz = f64::from(model.fundamental_hz);
        }
        self.formant_count = model.formants.len().min(MAX_FORMANTS);
        self.formants[..self.formant_count].copy_from_slice(&model.formants[..self.formant_count]);
    }

    /// A compound of two or more ranks all on the organ series walks the series
    /// a row at a time; anything else breaks by octaves, which keeps a tierce a
    /// tierce. Either way it stops when the lowest rank reaches the unison.
    fn derive_break_mode(&mut self, model: &SpectralModel, count: usize) {
        self.break_mode = BreakMode::None;
        self.break_top_ratio = 0.0;
        self.break_top_series = None;
        self.break_max_shift = 0;

        const MAX_RANKS: usize = 12;
        let mut ranks = [0.0f32; MAX_RANKS];
        let mut rank_count = 0;
        let mut all_on_series = true;
        let mut lowest = 0.0f32;
        for t in &model.partials[..count] {
            let rr = t.rank_ratio;
            if rr.is_nan() || rr <= 0.0 {
                continue;
            }
            if ranks[..rank_count]
                .iter()
                .any(|&k| k > rr * 0.995 && k < rr * 1.005)
            {
                continue;
            }
            if rank_count < MAX_RANKS {
                ranks[rank_count] = rr;
                rank_count += 1;
            }
            self.break_top_ratio = self.break_top_ratio.max(rr);
            if lowest <= 0.0 || rr < lowest {
                lowest = rr;
            }
            if mixture_series_index(rr).is_none() {
                all_on_series = false;
            }
        }
        if rank_count >= 2 && all_on_series {
            self.break_mode = BreakMode::Series;
            self.break_top_series = mixture_series_index(self.break_top_ratio);
            self.break_max_shift = mixture_series_index(lowest).map_or(-1, |i| i as i32);
        } else if rank_count >= 1 {
            self.break_mode = BreakMode::Octave;
            self.break_max_shift = if lowest > 1.0 {
                lowest.log2().floor() as i32
            } else {
                0
            };
            if self.break_max_shift <= 0 {
                self.break_mode = BreakMode::None;
            }
        }
    }

    fn broken_rank_ratio(&self, rank_ratio: f32, series: Option<usize>, shift: i32) -> f32 {
        if shift <= 0 || rank_ratio.is_nan() || rank_ratio <= 0.0 {
            return rank_ratio;
        }
        if self.break_mode == BreakMode::Series {
            return match series {
                Some(s) if s as i32 - shift >= 0 => MIXTURE_SERIES[(s as i32 - shift) as usize],
                _ => rank_ratio,
            };
        }
        rank_ratio * (-shift as f32).exp2()
    }

    /// How many steps this stop breaks back at `note`. Breaks land at a C or an
    /// F sharp and hold for the six semitones they open, equal-tempered: a
    /// break is a fact about the compass, not about the temperament.
    fn break_shift_for_note(&self, note: u8) -> i32 {
        if self.break_mode == BreakMode::None || self.break_max_shift <= 0 {
            return 0;
        }
        let span_top = (i32::from(note) / 6) * 6 + 5;
        let f = 440.0 * (f64::from(span_top - 69) / 12.0).exp2();
        let mut shift = 0;
        while shift < self.break_max_shift
            && f * f64::from(self.broken_rank_ratio(
                self.break_top_ratio,
                self.break_top_series,
                shift,
            )) > MIXTURE_CEILING_HZ
        {
            shift += 1;
        }
        shift
    }

    /// Start a note at `frequency_hz`. A bank stolen while sounding keeps its
    /// envelope and phasors, so a steal is a pitch change under a gain ramp
    /// rather than a click. Reads the wind once: a pipe on slack wind speaks
    /// more slowly.
    pub fn trigger(&mut self, pipe: PipeId, frequency_hz: f64, wind: Option<&WindModel>) {
        self.fundamental_hz = frequency_hz;
        self.note_time = 0.0;
        // Per-voice salt, so two voices of one pitch (a unison coupler, the same
        // key on two divisions) are independent and sum at +3 dB, not +6.
        let salt = u32::from(pipe.rank).wrapping_mul(2_654_435_761)
            ^ u32::from(pipe.note).wrapping_mul(40_503)
            ^ u32::from(pipe.division).wrapping_mul(2_246_822_519)
            ^ 0x85EB_CA6B;
        // An organ is velocity-flat: a pipe speaks at its one voiced loudness.
        self.velocity_gain = 1.0;
        let retrigger = self.stage != Stage::Idle;
        let f0 = if self.fundamental_hz > 1.0 {
            self.fundamental_hz
        } else {
            120.0
        };

        let log_f0 = f0.log2() as f32;
        let height = ((log_f0 - 6.0311) * 0.2).clamp(0.0, 1.0);
        let tilt_per_log = -self.live.treble_tilt_db * height;
        let log_f0_over_120 = log_f0 - 6.9069;

        let mut stretch = 1.0f32;
        if let Some(w) = wind {
            let dev = w.deviation_at(self.chest, 0);
            stretch = (1.0 - self.curve.attack(dev)).clamp(0.5, 2.0);
        }
        let lerp = |lo: f32, hi: f32| lo + (hi - lo) * height;
        self.set_envelope_times(
            stretch * lerp(self.speech.attack_at_c2_sec, self.speech.attack_at_c7_sec),
            lerp(self.speech.release_at_c2_sec, self.speech.release_at_c7_sec),
        );

        // C-side / C#-side of the case, opening out up the compass.
        let side = if pipe.note & 1 != 0 { 1.0 } else { -1.0 };
        let note_pan = side * self.stereo_spread * 0.35 * (0.6 + 0.4 * height);
        (self.pan_l, self.pan_r) = equal_power_pan(note_pan);

        let bloom_span = (self.live.max_bloom_seconds - 0.008) * stretch;
        let break_shift = self.break_shift_for_note(pipe.note);

        let mut formant_lin = [0.0f32; MAX_FORMANTS];
        let mut formant_inv_half_bw = [0.0f32; MAX_FORMANTS];
        for k in 0..self.formant_count {
            let pk = self.formants[k];
            formant_lin[k] = db_to_linear(pk.gain_db);
            formant_inv_half_bw[k] = if pk.bandwidth_hz > 0.0 {
                2.0 / pk.bandwidth_hz
            } else {
                0.0
            };
        }

        for i in 0..self.count {
            let p = self.partials[i];
            let h = hash32(salt ^ p.seed.wrapping_mul(2_246_822_519));
            let mut p = Partial {
                rng: h | 1,
                drift_cents: 0.0,
                gain_inc: 0.0,
                ..p
            };
            if !retrigger {
                p.osc = random_phasor(h >> 8);
                p.cur_gain = 0.0;
            }

            // Mixture break-back moves the whole rank, harmonics included.
            p.sounding_ratio = p.ratio;
            p.log_sounding = p.log_ratio;
            if break_shift > 0 && p.rank_ratio > 0.0 {
                let scale =
                    self.broken_rank_ratio(p.rank_ratio, p.rank_series, break_shift) / p.rank_ratio;
                p.sounding_ratio *= scale;
                p.log_sounding += scale.log2();
            }
            let freq = f0 * f64::from(p.sounding_ratio);

            let oct = (log_f0_over_120 + p.log_sounding) * (1.0 / BLOOM_OCTAVE_SPAN);
            p.bloom_seconds = 0.008 * stretch + bloom_span * oct.clamp(0.0, 1.0);
            p.note_level_scale = db_to_linear(tilt_per_log * p.log_harmonic);

            let mut fg = 1.0f32;
            for k in 0..self.formant_count {
                if formant_inv_half_bw[k] == 0.0 {
                    continue;
                }
                let d = (freq as f32 - self.formants[k].center_hz) * formant_inv_half_bw[k];
                fg += (formant_lin[k] - 1.0) / (1.0 + d * d);
            }
            p.formant_gain = fg.clamp(0.05, 3.0);

            // Ranks stand apart in the case: two unison stops are two sources.
            let rank_offset =
                (((p.seed >> 8) & 0xFFFF) as f32 / 65535.0 - 0.5) * self.stereo_spread * 1.1;
            (p.pan_l, p.pan_r) = equal_power_pan((note_pan + rank_offset).clamp(-1.0, 1.0));
            self.partials[i] = p;
        }

        if self.chiff_amount > 0.0 {
            let sr = self.sample_rate;
            let fc = (f0 * 4.0).max(1500.0).min(0.45 * sr);
            self.chiff_lp_coef = (std::f64::consts::TAU * fc / sr).min(1.0) as f32;
            self.chiff_atk_samp = ((0.003 * sr) as i32).max(1);
            let decay_tau = 0.045 * sr;
            self.chiff_decay_mul = (-1.0 / decay_tau).exp() as f32;
            self.chiff_len_samp = self.chiff_atk_samp + (decay_tau * 4.0) as i32;
            self.chiff_samp = 0;
            self.chiff_env = 0.0;
            self.chiff_lp = 0.0;
        } else {
            self.chiff_len_samp = 0;
        }

        if self.released_shaped {
            // A steal out of an exponential release: back into the shaped domain
            // the attack ramps in, at the same audible level.
            self.env_gain = inverse_smoothstep01(self.env_gain);
            self.released_shaped = false;
        }
        self.stage = Stage::Attack;
        if !retrigger {
            self.env_gain = 0.0;
        }
    }

    /// Let the note go.
    pub fn release(&mut self) {
        if self.stage == Stage::Idle {
            return;
        }
        if self.release_style == ReleaseStyle::Exponential && !self.released_shaped {
            // Carry the attack's shaping into the envelope, so the decay starts
            // exactly where the sound is.
            self.env_gain = smoothstep01(self.env_gain);
            self.released_shaped = true;
            self.release_time = self.note_time;
        }
        self.stage = Stage::Release;
    }

    /// Stop dead. The phasors keep turning: they cost nothing at zero gain.
    pub fn silence(&mut self) {
        self.stage = Stage::Idle;
        self.env_gain = 0.0;
        self.released_shaped = false;
        self.expr_inc = 0.0;
        self.chiff_env = 0.0;
        self.chiff_lp = 0.0;
        self.chiff_len_samp = 0;
        self.note_time = 0.0;
        for p in &mut self.partials[..self.count] {
            p.cur_gain = 0.0;
            p.block_gain = 0.0;
            p.gain_inc = 0.0;
        }
    }

    /// Whether it is sounding (including its release).
    pub fn is_active(&self) -> bool {
        self.stage != Stage::Idle
    }

    /// Partials seeded.
    pub fn partial_count(&self) -> usize {
        self.count
    }

    /// The chest it reads and how it answers it.
    pub fn set_wind_coupling(&mut self, chest: WindchestId, curve: WindResponseCurve) {
        self.chest = chest;
        self.curve = curve;
    }

    /// The chest it reads.
    pub fn chest(&self) -> WindchestId {
        self.chest
    }

    /// Base level.
    pub fn set_master_gain(&mut self, gain: f32) {
        self.master_gain = gain;
    }

    /// This block's swell-shoe ramp.
    pub fn set_expression(&mut self, start_gain: f32, inc_per_sample: f32) {
        self.expr_gain = start_gain;
        self.expr_inc = inc_per_sample;
    }

    /// How fast this rank speaks.
    pub fn set_speech_profile(&mut self, speech: SpeechProfile) {
        self.speech = speech;
    }

    /// Attack and release, in seconds.
    pub fn set_envelope_times(&mut self, attack: f32, release: f32) {
        self.attack_seconds = if attack > 0.0 { attack } else { 0.0005 };
        self.release_seconds = if release > 0.0 { release } else { 0.0005 };
        self.attack_step = (1.0 / (f64::from(self.attack_seconds) * self.sample_rate)) as f32;
        self.release_step = (1.0 / (f64::from(self.release_seconds) * self.sample_rate)) as f32;
        let t60 = f64::from(RELEASE_T60_FACTOR * self.release_seconds);
        self.release_mul = (1.0e-3f64.ln() / (t60 * self.sample_rate)).exp() as f32;
    }

    /// Update the wind, drift and speech every `frames` frames rather than
    /// once per block (0, 1.x's behaviour). A tremulant read once per host
    /// buffer is sampled four times a cycle at 2048 frames: the instrument
    /// would sound different in every DAW.
    pub fn set_control_frames(&mut self, frames: usize) {
        self.control_frames = frames;
    }

    /// How the envelope dies away.
    pub fn set_release_style(&mut self, style: ReleaseStyle) {
        self.release_style = style;
    }

    /// The living-pipe character in force.
    pub fn liveliness(&self) -> Liveliness {
        self.live
    }

    /// The living-pipe character.
    pub fn set_liveliness(&mut self, live: Liveliness) {
        self.live = Liveliness {
            instability_cents: live.instability_cents.max(0.0),
            attack_glide_cents: live.attack_glide_cents,
            max_bloom_seconds: live.max_bloom_seconds.max(0.008),
            hf_rolloff_hz: live.hf_rolloff_hz.max(500.0),
            treble_tilt_db: live.treble_tilt_db.max(0.0),
        };
    }

    /// Stereo spread of the case, 0..1.
    pub fn set_stereo_spread(&mut self, spread: f32) {
        self.stereo_spread = spread.clamp(0.0, 1.0);
    }

    /// Level of the attack chiff (0 disables it).
    pub fn set_chiff(&mut self, amount: f32) {
        self.chiff_amount = amount.max(0.0);
    }

    /// How much would be lost if this voice stopped: every gain the render
    /// applies, for choosing what to steal.
    pub fn level_estimate(&self) -> f32 {
        if self.stage == Stage::Idle {
            0.0
        } else {
            self.env_gain * self.master_gain * self.velocity_gain * self.expr_gain
        }
    }

    fn recompute_block_coefficients(
        &mut self,
        frames: usize,
        wind: Option<&WindModel>,
        wind_frame: usize,
    ) {
        let sr = self.sample_rate;
        let nyquist = 0.5 * sr;
        let deviation = wind.map_or(0.0, |w| w.deviation_at(self.chest, wind_frame));
        let darken_db = if self.released_shaped {
            -RELEASE_DARKEN_DB_PER_S_OCTAVE * (self.note_time - self.release_time) as f32
        } else {
            0.0
        };
        let pitch_ratio = (f64::from(self.curve.pitch_cents(deviation)) / 1200.0).exp2();
        let level_lin = db_to_linear(self.curve.level_db(deviation));
        let wind_tilt_db = WIND_TILT_DB_PER_OCTAVE * self.curve.brightness(deviation);
        let wind_tilting = wind_tilt_db.abs() > 0.001;

        // A labial pipe starts a touch flat and pulls up to pitch as it speaks.
        let mut glide_ratio = 1.0f32;
        if self.note_time < f64::from(self.glide_seconds) && self.live.attack_glide_cents != 0.0 {
            let frac = self.note_time as f32 / self.glide_seconds;
            glide_ratio =
                cents_to_ratio_approx(self.live.attack_glide_cents * (1.0 - smoothstep01(frac)));
        }
        let drift_coef = 1.0 - (-(frames as f32) / (DRIFT_TAU_SECONDS * sr as f32)).exp();

        let (instability, hf, note_time, f0) = (
            self.live.instability_cents,
            self.live.hf_rolloff_hz,
            self.note_time,
            self.fundamental_hz,
        );
        for p in &mut self.partials[..self.count] {
            p.drift_cents += drift_coef * (next_bipolar(&mut p.rng) * instability - p.drift_cents);
            let drift_ratio = cents_to_ratio_approx(p.drift_cents);
            let partial_pitch = 1.0 + (pitch_ratio - 1.0) * f64::from(p.wind_sensitivity + 1.0);
            let freq = f0
                * f64::from(p.sounding_ratio)
                * partial_pitch
                * f64::from(glide_ratio)
                * f64::from(drift_ratio);

            // Fade the top toward Nyquist with a smoothstep; freeze what is past it.
            let aa = smoothstep01(((nyquist - freq) / (nyquist * 0.4)) as f32);
            if freq >= nyquist || aa <= 0.0 {
                p.cos_inc = 1.0;
                p.sin_inc = 0.0;
                p.block_gain = 0.0;
                continue;
            }
            let hf_gain = if freq > f64::from(hf) {
                hf / freq as f32
            } else {
                1.0
            };

            let onset = p.onset_seconds.min(MAX_ONSET_SECONDS);
            let dt = note_time - f64::from(onset);
            let bloom = smoothstep01(dt as f32 / p.bloom_seconds);

            // Two transcendentals per partial per BLOCK. 2 pi stays an f32 on
            // purpose: it is what 1.x rotated by.
            let w = (f64::from(std::f32::consts::TAU) * freq / sr) as f32;
            p.cos_inc = w.cos();
            p.sin_inc = w.sin();

            // The recursive rotation drifts off the unit circle in f32; one Newton
            // step a block pins it back.
            let mag = p.osc.cos * p.osc.cos + p.osc.sin * p.osc.sin;
            let renorm = 1.5 - 0.5 * mag;
            p.osc.cos *= renorm;
            p.osc.sin *= renorm;

            p.block_gain = p.amplitude
                * aa
                * hf_gain
                * bloom
                * level_lin
                * p.note_level_scale
                * p.formant_gain;
            if wind_tilting {
                p.block_gain *= db_to_linear(wind_tilt_db * p.wind_bright_exp);
            }
            if darken_db < 0.0 {
                p.block_gain *= db_to_linear(darken_db * p.log_harmonic);
            }
        }
    }

    /// Render one block, ADDING into `left` and `right` (equal lengths).
    /// `frame_in_block` is where this slice starts in the wind's block, so the
    /// pressure is read where the sound is.
    pub fn render_add(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        wind: Option<&WindModel>,
        frame_in_block: usize,
    ) {
        let frames = left.len().min(right.len());
        if self.stage == Stage::Idle || frames == 0 {
            return;
        }
        let segment = if self.control_frames == 0 {
            frames
        } else {
            self.control_frames
        };
        let mut pos = 0;
        while pos < frames && self.stage != Stage::Idle {
            let n = (frames - pos).min(segment);
            // 1.x read the wind at the block's first frame whatever it rendered.
            let wind_frame = if self.control_frames == 0 {
                0
            } else {
                frame_in_block + pos
            };
            self.recompute_block_coefficients(n, wind, wind_frame);
            let inv = 1.0 / n as f32;
            for p in &mut self.partials[..self.count] {
                p.gain_inc = (p.block_gain - p.cur_gain) * inv;
            }
            self.render_segment(&mut left[pos..pos + n], &mut right[pos..pos + n]);
            self.note_time += n as f64 / self.sample_rate;
            pos += n;
        }
    }

    fn render_segment(&mut self, left: &mut [f32], right: &mut [f32]) {
        let frames = left.len();
        let mut pos = 0;
        while pos < frames {
            let want = (frames - pos).min(CHUNK_FRAMES);
            let mut count = 0;
            while count < want {
                match self.stage {
                    Stage::Attack => {
                        self.env_gain += self.attack_step;
                        if self.env_gain >= 1.0 {
                            self.env_gain = 1.0;
                            self.stage = Stage::Sustain;
                        }
                    }
                    Stage::Release if self.released_shaped => {
                        self.env_gain *= self.release_mul;
                        if self.env_gain <= 1.0e-4 {
                            self.env_gain = 0.0;
                            self.stage = Stage::Idle;
                        }
                    }
                    Stage::Release => {
                        self.env_gain -= self.release_step;
                        if self.env_gain <= 0.0 {
                            self.env_gain = 0.0;
                            self.stage = Stage::Idle;
                        }
                    }
                    Stage::Sustain | Stage::Idle => {}
                }
                self.expr_gain += self.expr_inc;
                let shaped = if self.released_shaped {
                    self.env_gain
                } else {
                    smoothstep01(self.env_gain)
                };
                self.chunk_env[count] =
                    shaped * self.master_gain * self.velocity_gain * self.expr_gain;
                count += 1;
                if self.stage == Stage::Idle {
                    break;
                }
            }
            if count == 0 {
                break;
            }

            let (cl, cr) = (&mut self.chunk_l[..count], &mut self.chunk_r[..count]);
            cl.fill(0.0);
            cr.fill(0.0);
            for p in &mut self.partials[..self.count] {
                partial_accumulate(
                    cl,
                    cr,
                    &mut p.osc,
                    p.cos_inc,
                    p.sin_inc,
                    &mut p.cur_gain,
                    p.gain_inc,
                    p.pan_l,
                    p.pan_r,
                );
            }

            for k in 0..count {
                let env = self.chunk_env[k];
                let mut l = self.chunk_l[k] * env;
                let mut r = self.chunk_r[k] * env;
                if self.chiff_len_samp > 0 && self.chiff_samp < self.chiff_len_samp {
                    let noise = next_bipolar(&mut self.chiff_rng);
                    self.chiff_lp += self.chiff_lp_coef * (noise - self.chiff_lp);
                    if self.chiff_samp < self.chiff_atk_samp {
                        self.chiff_env = self.chiff_samp as f32 / self.chiff_atk_samp as f32;
                    } else {
                        self.chiff_env *= self.chiff_decay_mul;
                    }
                    let chiff = self.chiff_lp
                        * self.chiff_env
                        * self.chiff_amount
                        * self.velocity_gain
                        * self.master_gain;
                    l += chiff * self.pan_l;
                    r += chiff * self.pan_r;
                    self.chiff_samp += 1;
                }
                left[pos + k] += l;
                right[pos + k] += r;
            }
            pos += count;
            if self.stage == Stage::Idle {
                break;
            }
        }
    }
}

/// The `x` in [0, 1] whose smoothstep is `y`, by Newton's method.
fn inverse_smoothstep01(y: f32) -> f32 {
    let y = y.clamp(0.0, 1.0);
    let mut x = y;
    for _ in 0..6 {
        let f = x * x * (3.0 - 2.0 * x) - y;
        let d = 6.0 * x * (1.0 - x);
        if d.abs() < 1.0e-6 {
            break;
        }
        x = (x - f / d).clamp(0.0, 1.0);
    }
    x
}

/// Forces the shared tables into existence off the audio thread.
struct LazyTouch;

impl LazyTouch {
    fn touch() {
        let _ = &*TABLES;
    }
}
