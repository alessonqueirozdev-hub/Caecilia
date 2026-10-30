//! The organ engine.
//!
//! A key is ONE event; the engine expands it into one voice per engaged rank of
//! its division and of every division a drawn coupler lends it. Each voice
//! renders into the bus of the chest its rank stands on; enclosed chests pass
//! through their swell box; the buses sum to the output. The wind is stepped
//! once per block under the flow every sounding pipe draws, before any voice
//! reads it.
//!
//! # Real time
//!
//! [`OrganEngine::new`] allocates everything. [`OrganEngine::process`],
//! [`OrganEngine::set_engaged`], [`OrganEngine::reset`] and every read are
//! allocation-free, lock-free and wait-free -- `tests/realtime.rs` proves it
//! with a counting allocator.

mod engaged;
mod pool;
mod shutter;

use caecilia_model::{
    Calibration, DivisionId, MidiNote, Organ, PipeId, RankVoicing, StopId, WindchestId,
    build_rank_voicing,
};
use caecilia_wind::{WindModel, WindSnapshot, config_from_organ, flow_per_voice};
pub use manguebyte_core::pool::PoolConfig;
pub use manguebyte_dsp::Tuning;

pub use engaged::{EngagedCoupler, EngagedRank, EngagedTable, MAX_COUPLERS, MAX_RANKS};
use pool::Pool;
use shutter::OnePole;

/// Default polyphony: as many pipes as can sound at once.
pub const DEFAULT_POLYPHONY: usize = 512;

/// Divisions that can carry a swell shoe or a sustain pedal.
pub const MAX_DIVISIONS: usize = 16;

/// Chests the engine keeps buses for.
pub const MAX_CHESTS: usize = 16;

/// Keys that can be held at once.
const MAX_HELD_KEYS: usize = 128;

/// Voices one key can expand into.
const MAX_KEY_VOICES: usize = 128;

/// Gain a fully shut box leaves: -13 dB. Shut is not silent.
const SHUT_GAIN: f32 = 0.2239;
/// Shutter low-pass corner, box open: transparent rather than bypassed, so the
/// shoe leaving the top of its travel glides instead of switching.
const SHUTTER_OPEN_HZ: f32 = 18000.0;
/// Corner with the box shut: a closed swell dulls as well as quietens.
const SHUTTER_CLOSED_HZ: f32 = 800.0;
/// log2(18000 / 800).
const SHUTTER_OCTAVE_SPAN: f32 = 4.491_853;
/// The swell shoe's glide.
const EXPRESSION_GLIDE_SECONDS: f64 = 0.030;

/// What reaches the engine from the player.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Command {
    /// A key goes down.
    NoteOn {
        /// The keyboard.
        division: DivisionId,
        /// The key.
        note: MidiNote,
    },
    /// A key comes up.
    NoteOff {
        /// The keyboard.
        division: DivisionId,
        /// The key.
        note: MidiNote,
    },
    /// Every note releases.
    Panic,
    /// A division's sustain pedal.
    Sustain {
        /// The keyboard.
        division: DivisionId,
        /// Down or up.
        down: bool,
    },
    /// A swell shoe, 0 shut to 1 open.
    Expression {
        /// The division whose box it moves.
        division: DivisionId,
        /// Shoe position.
        position: f32,
    },
    /// A chest's tremulant.
    Tremulant {
        /// The chest it shakes.
        chest: WindchestId,
        /// Drawn or not.
        enabled: bool,
        /// Rate.
        rate_hz: f32,
        /// Depth as a fraction of chest pressure.
        depth_fraction: f32,
    },
}

/// A command at a frame of the block.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Timed {
    /// Frame within the block. Past the end lands on the last frame (a
    /// note-off a block late is a stuck note); earlier than where the render
    /// already is applies at once rather than rewinding.
    pub offset: u32,
    /// What happens.
    pub command: Command,
}

/// What the engine publishes after each block.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Meters {
    /// Frames rendered since the last reset.
    pub frame: u64,
    /// Peak of the block.
    pub peak: f32,
    /// RMS of the block.
    pub rms: f32,
    /// Most voices sounding at once during the block.
    pub active_voices: u32,
    /// Chest 0's pressure at the end of the block.
    pub wind_pressure_pa: f32,
    /// Chest 0's deviation from nominal: 0 at rest, negative as it sags.
    pub wind_sag_norm: f32,
}

#[derive(Clone, Copy, Debug, Default)]
struct HeldKey {
    note: MidiNote,
    division: DivisionId,
}

/// One pipe a key calls for: a rank of the engaged table at a sounding note.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct KeyVoice {
    rank: usize,
    note: MidiNote,
}

type KeyVoices = [KeyVoice; MAX_KEY_VOICES];

/// The organ engine. See the crate documentation.
#[derive(Debug)]
pub struct OrganEngine {
    calibration: Calibration,
    flow_per_voice: f32,
    sample_rate: f64,
    max_block: usize,
    chest_count: usize,
    voicings: Vec<RankVoicing>,
    tuning: Tuning,
    /// Every frequency times this: reference pitch over the table's.
    pitch_scale: f64,
    wind: WindModel,
    /// The wind of this block, frozen after the step, for every voice to read.
    snapshot: WindSnapshot,
    pool: Pool,
    missed_jobs: u64,
    table: EngagedTable,
    held: [HeldKey; MAX_HELD_KEYS],
    held_count: usize,
    sustain_down: [bool; MAX_DIVISIONS],
    expr_target: [f32; MAX_DIVISIONS],
    expr_current: [f32; MAX_DIVISIONS],
    expr_ramp: [(f32, f32); MAX_DIVISIONS],
    shoe_for_chest: [Option<u8>; MAX_CHESTS],
    /// Each division's keyboard: a key off it is not a key.
    compass: [(MidiNote, MidiNote); MAX_DIVISIONS],
    bus_l: Vec<Vec<f32>>,
    bus_r: Vec<Vec<f32>>,
    shutters: Vec<[OnePole; 2]>,
    frame: u64,
    meters: Meters,
    pending_meters: Meters,
    meter_sum_sq: f64,
    meter_samples: u64,
}

impl OrganEngine {
    /// An engine for `organ`, voiced as 2.0. Allocates every buffer, voice and
    /// voicing it will ever use: off the audio thread.
    pub fn new(organ: &Organ, sample_rate: f64, max_block: usize, polyphony: usize) -> Self {
        Self::with_calibration(organ, sample_rate, max_block, polyphony, Calibration::V2)
    }

    /// An engine for `organ` under a chosen calibration (`OneX` reproduces 1.x),
    /// rendering on the calling thread alone.
    pub fn with_calibration(
        organ: &Organ,
        sample_rate: f64,
        max_block: usize,
        polyphony: usize,
        calibration: Calibration,
    ) -> Self {
        Self::with_config(
            organ,
            sample_rate,
            max_block,
            polyphony,
            calibration,
            PoolConfig::offline(),
        )
    }

    /// An engine whose voices render on a pool of threads as well as the
    /// calling one. The output is bit for bit the single-threaded output:
    /// voices render in fixed groups and the groups are summed in order.
    pub fn with_config(
        organ: &Organ,
        sample_rate: f64,
        max_block: usize,
        polyphony: usize,
        calibration: Calibration,
        threads: PoolConfig,
    ) -> Self {
        let sample_rate = if sample_rate > 0.0 {
            sample_rate
        } else {
            48000.0
        };
        let max_block = max_block.max(1);
        let chest_count = organ.windchests().len().clamp(1, MAX_CHESTS);
        let voicings = organ
            .stops()
            .iter()
            .map(|s| build_rank_voicing(organ, s.id, calibration).unwrap_or_default())
            .collect();

        // Which chests stand in a swell box, and whose shoe closes them.
        let mut shoe_for_chest = [None; MAX_CHESTS];
        for d in organ
            .divisions()
            .iter()
            .filter(|d| d.enclosed && d.id.index() < MAX_DIVISIONS)
        {
            for c in &d.windchests {
                if let Some(slot) = shoe_for_chest.get_mut(c.index()) {
                    *slot = Some(d.id.0 as u8);
                }
            }
        }

        let mut compass = [(0, 127); MAX_DIVISIONS];
        for d in organ
            .divisions()
            .iter()
            .filter(|d| d.id.index() < MAX_DIVISIONS)
        {
            compass[d.id.index()] = (d.low_note, d.high_note);
        }

        let shutters = (0..chest_count)
            .map(|_| {
                let mut f = [OnePole::default(), OnePole::default()];
                for p in &mut f {
                    p.set_lowpass(sample_rate, SHUTTER_OPEN_HZ);
                }
                f
            })
            .collect();

        Self {
            calibration,
            flow_per_voice: flow_per_voice(calibration),
            sample_rate,
            max_block,
            chest_count,
            voicings,
            tuning: Tuning::default(),
            pitch_scale: 1.0,
            wind: WindModel::new(sample_rate, &config_from_organ(organ)),
            snapshot: WindSnapshot::default(),
            pool: Pool::new(
                sample_rate,
                polyphony,
                calibration,
                chest_count,
                max_block,
                threads,
            ),
            missed_jobs: 0,
            table: EngagedTable::default(),
            held: [HeldKey::default(); MAX_HELD_KEYS],
            held_count: 0,
            sustain_down: [false; MAX_DIVISIONS],
            expr_target: [1.0; MAX_DIVISIONS],
            expr_current: [1.0; MAX_DIVISIONS],
            expr_ramp: [(1.0, 0.0); MAX_DIVISIONS],
            shoe_for_chest,
            compass,
            bus_l: vec![vec![0.0; max_block]; chest_count],
            bus_r: vec![vec![0.0; max_block]; chest_count],
            shutters,
            frame: 0,
            meters: Meters::default(),
            pending_meters: Meters::default(),
            meter_sum_sq: 0.0,
            meter_samples: 0,
        }
    }

    /// The calibration it plays under.
    pub fn calibration(&self) -> Calibration {
        self.calibration
    }

    /// The voicing of every stop, by stop id.
    pub fn voicings(&self) -> &[RankVoicing] {
        &self.voicings
    }

    /// The wind, for meters and tests.
    pub fn wind(&self) -> &WindModel {
        &self.wind
    }

    /// The frequency the engine sounds `note` at (8').
    pub fn key_frequency(&self, note: MidiNote) -> f64 {
        self.tuning.key_frequency(note & 0x7F) * self.pitch_scale
    }

    /// Play in `tuning` from the next note on (held notes keep their pitch,
    /// as a pipe does). Real-time safe: a tuning is plain data.
    pub fn set_tuning(&mut self, tuning: Tuning) {
        self.tuning = tuning;
    }

    /// Scale every frequency by `scale`: a reference pitch moved from the
    /// table's (A4 = 415 on a table built at 440 is 415/440). Exact, since a
    /// table is anchored at its reference key. Real-time safe.
    pub fn set_pitch_scale(&mut self, scale: f64) {
        if scale.is_finite() && scale > 0.0 {
            self.pitch_scale = scale;
        }
    }

    /// The tuning in force.
    pub fn tuning(&self) -> &Tuning {
        &self.tuning
    }

    /// Render jobs that missed their block since the engine was made: a
    /// worker held one past the wait limit, and its voices were silent for
    /// that block.
    pub fn missed_jobs(&self) -> u64 {
        self.missed_jobs
    }

    /// The last block's meters.
    pub fn meters(&self) -> Meters {
        self.meters
    }

    /// Voices sounding now.
    pub fn active_voices(&self) -> usize {
        self.pool.active_count()
    }

    /// Everything stops dead; the wind and the boxes come to rest.
    pub fn reset(&mut self) {
        self.pool.silence_all();
        self.held_count = 0;
        self.sustain_down = [false; MAX_DIVISIONS];
        for b in self.bus_l.iter_mut().chain(self.bus_r.iter_mut()) {
            b.fill(0.0);
        }
        for f in self.shutters.iter_mut().flatten() {
            f.reset();
        }
        self.wind.reset();
        self.snapshot = self.wind.snapshot();
        self.frame = 0;
        self.meters = Meters::default();
    }

    /// Adopt a new registration at a block boundary. Keys already down are
    /// reconciled: pipes the new registration drops release as a pipe does;
    /// pipes it adds start with their own speech; the rest are not touched.
    pub fn set_engaged(&mut self, next: EngagedTable) {
        self.reconcile_held_keys(&next);
        self.table = next;
    }

    /// Render one block into `left` and `right` (overwritten), applying
    /// `events` at their frames.
    pub fn process(&mut self, left: &mut [f32], right: &mut [f32], events: &[Timed]) {
        let total = left.len().min(right.len());
        if total == 0 {
            return;
        }
        self.step_wind(total);
        self.pending_meters = Meters::default();
        self.meter_sum_sq = 0.0;
        self.meter_samples = 0;

        let mut next_event = 0;
        let mut done = 0;
        while done < total {
            // Apply everything due at or before `done`; render to the next one.
            let mut until = total;
            while let Some(e) = events.get(next_event) {
                let at = (e.offset as usize).min(total - 1);
                if at > done {
                    until = at;
                    break;
                }
                self.apply(e.command);
                next_event += 1;
            }
            let n = (until - done).min(self.max_block);
            self.render_slice(&mut left[done..done + n], &mut right[done..done + n], done);
            done += n;
        }
        // Events past every slice still land (on the last frame, applied now).
        for e in &events[next_event..] {
            self.apply(e.command);
        }

        let m = &mut self.pending_meters;
        m.frame = self.frame;
        m.rms = if self.meter_samples > 0 {
            (self.meter_sum_sq / self.meter_samples as f64).sqrt() as f32
        } else {
            0.0
        };
        m.wind_pressure_pa = self.wind.pressure_at(WindchestId(0), 0);
        m.wind_sag_norm = self.wind.deviation_at(WindchestId(0), 0);
        self.meters = *m;
    }

    fn apply(&mut self, command: Command) {
        match command {
            Command::NoteOn { division, note } => self.note_on(division, note),
            Command::NoteOff { division, note } => self.note_off(division, note),
            Command::Panic => {
                self.pool.all_notes_off();
                self.held_count = 0;
                self.sustain_down = [false; MAX_DIVISIONS];
            }
            Command::Sustain { division, down } => {
                if let Some(s) = self.sustain_down.get_mut(division.index()) {
                    *s = down;
                    if !down {
                        self.pool.release_sustained(division);
                    }
                }
            }
            Command::Expression { division, position } => {
                if let Some(t) = self.expr_target.get_mut(division.index()) {
                    *t = SHUT_GAIN + (1.0 - SHUT_GAIN) * position.clamp(0.0, 1.0);
                }
            }
            Command::Tremulant {
                chest,
                enabled,
                rate_hz,
                depth_fraction,
            } => {
                // Shape first, so a tremulant engaged by this command starts at the
                // depth it was asked for.
                self.wind
                    .set_chest_tremulant_shape(chest, rate_hz, depth_fraction);
                self.wind.set_chest_tremulant_enabled(chest, enabled);
            }
        }
    }

    /// Book the flow of every sounding pipe on its chest, then integrate.
    fn step_wind(&mut self, frames: usize) {
        let per_chest = self.pool.flows(self.chest_count);
        for (c, &flow) in per_chest[..self.chest_count].iter().enumerate() {
            if flow > 0.0 {
                self.wind.register_demand(WindchestId(c as u16), flow);
            }
        }
        self.wind.step(frames);
        self.snapshot = self.wind.snapshot();
    }

    fn step_expression(&mut self, frames: usize) {
        let alpha =
            (1.0 - (-(frames as f64) / (EXPRESSION_GLIDE_SECONDS * self.sample_rate)).exp()) as f32;
        let inv = 1.0 / frames as f32;
        for d in 0..MAX_DIVISIONS {
            let start = self.expr_current[d];
            let end = start + alpha * (self.expr_target[d] - start);
            self.expr_current[d] = end;
            self.expr_ramp[d] = (start, (end - start) * inv);
        }
    }

    fn render_slice(&mut self, left: &mut [f32], right: &mut [f32], offset: usize) {
        let frames = left.len();
        for c in 0..self.chest_count {
            self.bus_l[c][..frames].fill(0.0);
            self.bus_r[c][..frames].fill(0.0);
        }
        self.step_expression(frames);

        let report = self
            .pool
            .render(frames, offset, &self.snapshot, &self.expr_ramp);
        self.missed_jobs += report.missed as u64;
        for c in 0..self.chest_count {
            self.pool.mix_chest(
                c,
                &mut self.bus_l[c][..frames],
                &mut self.bus_r[c][..frames],
            );
        }

        // The spectral half of the swell box: a low-pass that closes with the shoe.
        for c in 0..self.chest_count {
            let Some(shoe) = self.shoe_for_chest[c] else {
                continue;
            };
            let gain = self.expr_current[usize::from(shoe)];
            let position = ((gain - SHUT_GAIN) / (1.0 - SHUT_GAIN)).clamp(0.0, 1.0);
            let cutoff = SHUTTER_CLOSED_HZ * (position * SHUTTER_OCTAVE_SPAN).exp2();
            let [fl, fr] = &mut self.shutters[c];
            fl.set_lowpass(self.sample_rate, cutoff);
            fr.set_lowpass(self.sample_rate, cutoff);
            fl.process_in_place(&mut self.bus_l[c][..frames]);
            fr.process_in_place(&mut self.bus_r[c][..frames]);
        }

        left.fill(0.0);
        right.fill(0.0);
        for c in 0..self.chest_count {
            for (o, b) in left.iter_mut().zip(&self.bus_l[c][..frames]) {
                *o += b;
            }
            for (o, b) in right.iter_mut().zip(&self.bus_r[c][..frames]) {
                *o += b;
            }
        }
        self.frame += frames as u64;

        let m = &mut self.pending_meters;
        for &s in left.iter().chain(right.iter()) {
            m.peak = m.peak.max(s.abs());
            self.meter_sum_sq += f64::from(s) * f64::from(s);
        }
        self.meter_samples += 2 * frames as u64;
        m.active_voices = m.active_voices.max(self.pool.active_count() as u32);
    }

    /// The pipes a key calls for under `table`: its own division's ranks, then
    /// every coupler whose keys are this division. A pipe reached twice is
    /// sounded once; a key transposed off the compass is silent, not clamped.
    fn expand_key(
        note: MidiNote,
        division: DivisionId,
        table: &EngagedTable,
        out: &mut KeyVoices,
    ) -> usize {
        let mut n = 0;
        let mut push = |rank: usize, sounding: MidiNote, out: &mut KeyVoices| {
            if n >= MAX_KEY_VOICES {
                return;
            }
            let stop = table.ranks[rank].stop;
            if out[..n]
                .iter()
                .any(|k| table.ranks[k.rank].stop == stop && k.note == sounding)
            {
                return;
            }
            out[n] = KeyVoice {
                rank,
                note: sounding,
            };
            n += 1;
        };
        for (i, r) in table.ranks().iter().enumerate() {
            if r.division == division {
                push(i, note, out);
            }
        }
        for c in table.couplers().iter().filter(|c| c.to == division) {
            let shifted = i32::from(note) + i32::from(c.semitones);
            if !(0..=127).contains(&shifted) {
                continue;
            }
            for (i, r) in table.ranks().iter().enumerate() {
                if r.division == c.from {
                    push(i, shifted as MidiNote, out);
                }
            }
        }
        n
    }

    /// Whether a held key other than (`except_note`, `except_division`) calls
    /// for the same pipe under `table`: a pallet two keys open opens once.
    fn held_elsewhere(
        &self,
        v: KeyVoice,
        v_table: &EngagedTable,
        except_note: MidiNote,
        except_division: DivisionId,
        table: &EngagedTable,
    ) -> bool {
        let stop = v_table.ranks[v.rank].stop;
        let mut scratch = [KeyVoice::default(); MAX_KEY_VOICES];
        self.held[..self.held_count]
            .iter()
            .filter(|k| !(k.note == except_note && k.division == except_division))
            .any(|k| {
                let n = Self::expand_key(k.note, k.division, table, &mut scratch);
                scratch[..n]
                    .iter()
                    .any(|s| table.ranks[s.rank].stop == stop && s.note == v.note)
            })
    }

    fn pipe_of(table: &EngagedTable, v: KeyVoice) -> PipeId {
        let r = &table.ranks[v.rank];
        PipeId {
            rank: r.stop.0,
            note: v.note,
            division: r.division.0 as u8,
        }
    }

    fn start_voice(&mut self, rank: EngagedRank, note: MidiNote) -> bool {
        let pipe = PipeId {
            rank: rank.stop.0,
            note,
            division: rank.division.0 as u8,
        };
        // A pipe draws in proportion to its length: twice an 8' for a 16', and
        // an octave lower draws twice again. Referenced to middle C on an 8'.
        let octaves_below_middle_c = (60.0 - f32::from(note)) * (1.0 / 12.0);
        // A key the tuning leaves unmapped (a Scala keyboard mapping can)
        // sounds nothing.
        if !self.tuning.is_mapped(note & 0x7F) {
            return false;
        }
        let freq = self.key_frequency(note);
        let Some(slot) = self.pool.acquire(pipe, self.frame) else {
            return false;
        };
        slot.flow = rank.wind_flow * octaves_below_middle_c.exp2() * self.flow_per_voice;
        slot.chest = rank.chest;
        if let Some(v) = self.voicings.get(rank.stop.index()) {
            slot.voice.adopt_rank(v);
        }
        slot.voice.note_on(pipe, freq, Some(&self.snapshot));
        true
    }

    fn remember_key(&mut self, note: MidiNote, division: DivisionId) {
        if self.held[..self.held_count]
            .iter()
            .any(|k| k.note == note && k.division == division)
        {
            return;
        }
        if self.held_count < MAX_HELD_KEYS {
            self.held[self.held_count] = HeldKey { note, division };
            self.held_count += 1;
        }
    }

    fn forget_key(&mut self, note: MidiNote, division: DivisionId) {
        if let Some(i) = self.held[..self.held_count]
            .iter()
            .position(|k| k.note == note && k.division == division)
        {
            self.held_count -= 1;
            self.held[i] = self.held[self.held_count];
        }
    }

    /// Whether `note` is on `division`'s keyboard. A note off it is dropped
    /// rather than sounded, and does not take a voice with it.
    fn on_keyboard(&self, division: DivisionId, note: MidiNote) -> bool {
        self.compass
            .get(division.index())
            .is_none_or(|&(lo, hi)| (lo..=hi).contains(&note))
    }

    fn note_on(&mut self, division: DivisionId, note: MidiNote) {
        if !self.on_keyboard(division, note) {
            return;
        }
        let table = self.table;
        let mut wanted = [KeyVoice::default(); MAX_KEY_VOICES];
        let want = Self::expand_key(note, division, &table, &mut wanted);
        if want == 0 {
            return;
        }
        let needed = wanted[..want]
            .iter()
            .filter(|&&w| !self.held_elsewhere(w, &table, note, division, &table))
            .count();
        // Admission: the whole key or none of it. A chord with a hole in its
        // registration sounds broken; a key that does not speak sounds unpressed.
        while needed > 0 && self.pool.free_count() < needed {
            if self.pool.steal_quietest_key() == 0 {
                return;
            }
        }
        self.remember_key(note, division);
        for &w in &wanted[..want] {
            if !self.held_elsewhere(w, &table, note, division, &table) {
                self.start_voice(table.ranks[w.rank], w.note);
            }
        }
    }

    fn note_off(&mut self, division: DivisionId, note: MidiNote) {
        if !self.on_keyboard(division, note) {
            return;
        }
        self.forget_key(note, division);
        let table = self.table;
        let mut going = [KeyVoice::default(); MAX_KEY_VOICES];
        let n = Self::expand_key(note, division, &table, &mut going);
        let sustained = self
            .sustain_down
            .get(division.index())
            .copied()
            .unwrap_or(false);
        for &g in &going[..n] {
            if self.held_elsewhere(g, &table, note, division, &table) {
                continue;
            }
            let pipe = Self::pipe_of(&table, g);
            if sustained {
                self.pool.hold_for_sustain(pipe);
            } else {
                self.pool.note_off(pipe);
            }
        }
    }

    fn reconcile_held_keys(&mut self, next: &EngagedTable) {
        let old = self.table;
        let same = |a: &EngagedTable, x: KeyVoice, b: &EngagedTable, set: &[KeyVoice]| {
            set.iter()
                .any(|y| b.ranks[y.rank].stop == a.ranks[x.rank].stop && y.note == x.note)
        };
        let mut before = [KeyVoice::default(); MAX_KEY_VOICES];
        let mut after = [KeyVoice::default(); MAX_KEY_VOICES];
        for k in 0..self.held_count {
            let key = self.held[k];
            let nb = Self::expand_key(key.note, key.division, &old, &mut before);
            let na = Self::expand_key(key.note, key.division, next, &mut after);
            for &b in &before[..nb] {
                if same(&old, b, next, &after[..na])
                    || self.held_elsewhere(b, &old, key.note, key.division, next)
                {
                    continue;
                }
                self.pool.note_off(Self::pipe_of(&old, b));
            }
            for &a in &after[..na] {
                if same(next, a, &old, &before[..nb])
                    || self.held_elsewhere(a, next, key.note, key.division, &old)
                {
                    continue;
                }
                if self.pool.free_count() == 0 && self.pool.steal_quietest_key() == 0 {
                    break;
                }
                self.start_voice(next.ranks[a.rank], a.note);
            }
        }
    }

    /// The table for a set of drawn stops and couplers on this engine's organ.
    pub fn table_for(
        &self,
        organ: &Organ,
        stops: &[StopId],
        couplers: &[caecilia_model::CouplerId],
    ) -> EngagedTable {
        EngagedTable::build(organ, &self.voicings, stops, couplers)
    }
}
