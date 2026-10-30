//! The voice pool: one slot per sounding pipe.

use caecilia_model::{Calibration, DivisionId, MidiNote, PipeId, WindchestId};
use caecilia_synth::AdditiveVoice;

/// One slot: a voice and what the engine knows about the pipe it sounds.
#[derive(Debug)]
pub(crate) struct Slot {
    pub voice: AdditiveVoice,
    pub active: bool,
    pub pipe: PipeId,
    pub start_frame: u64,
    pub sustained: bool,
    /// What this pipe draws from its chest, booked every block it sounds.
    pub flow: f32,
    pub chest: WindchestId,
}

/// Fixed-capacity, allocation-free after construction.
#[derive(Debug)]
pub(crate) struct Pool {
    pub slots: Vec<Slot>,
    free: Vec<u16>,
}

impl Pool {
    pub fn new(sample_rate: f64, capacity: usize, calibration: Calibration) -> Self {
        let slots = (0..capacity)
            .map(|_| Slot {
                voice: AdditiveVoice::new(sample_rate, calibration),
                active: false,
                pipe: PipeId::default(),
                start_frame: 0,
                sustained: false,
                flow: 0.0,
                chest: WindchestId(0),
            })
            .collect();
        // Popped from the end: slot 0 is handed out first.
        let free = (0..capacity as u16).rev().collect();
        Self { slots, free }
    }

    pub fn free_count(&self) -> usize {
        self.free.len()
    }

    pub fn active_count(&self) -> usize {
        self.slots.len() - self.free.len()
    }

    /// A free slot for `pipe`, or `None` when the pool is full.
    pub fn acquire(&mut self, pipe: PipeId, frame: u64) -> Option<usize> {
        let i = usize::from(self.free.pop()?);
        let s = &mut self.slots[i];
        s.active = true;
        s.pipe = pipe;
        s.start_frame = frame;
        s.sustained = false;
        Some(i)
    }

    fn reclaim(&mut self, i: usize) {
        let s = &mut self.slots[i];
        s.active = false;
        s.sustained = false;
        self.free.push(i as u16);
    }

    /// Return finished voices' slots to the free list.
    pub fn refresh(&mut self) {
        for i in 0..self.slots.len() {
            if self.slots[i].active && !self.slots[i].voice.is_active() {
                self.reclaim(i);
            }
        }
    }

    /// Release every voice sounding `pipe`.
    pub fn note_off(&mut self, pipe: PipeId) {
        for s in self.slots.iter_mut().filter(|s| s.active && s.pipe == pipe) {
            s.voice.note_off();
            s.sustained = false;
        }
    }

    /// Mark every voice sounding `pipe` as held by the sustain pedal.
    pub fn hold_for_sustain(&mut self, pipe: PipeId) {
        for s in self.slots.iter_mut().filter(|s| s.active && s.pipe == pipe) {
            s.sustained = true;
        }
    }

    /// Release what the pedal of `division` was holding.
    pub fn release_sustained(&mut self, division: DivisionId) {
        for s in self
            .slots
            .iter_mut()
            .filter(|s| s.active && s.sustained && u16::from(s.pipe.division) == division.0)
        {
            s.voice.note_off();
            s.sustained = false;
        }
    }

    /// A musical all-notes-off: everything releases.
    pub fn all_notes_off(&mut self) {
        for s in self.slots.iter_mut().filter(|s| s.active) {
            s.voice.note_off();
            s.sustained = false;
        }
    }

    /// Stop everything dead and free every slot.
    pub fn silence_all(&mut self) {
        for i in 0..self.slots.len() {
            if self.slots[i].active {
                self.slots[i].voice.silence();
                self.reclaim(i);
            }
        }
    }

    /// Steal a whole KEY -- every voice of the quietest sounding pipe's key and
    /// division, since taking one rank off a chord is more audible than ending a
    /// note early. Ties go to the oldest. Returns how many slots were freed.
    pub fn steal_quietest_key(&mut self) -> usize {
        let mut best: Option<(f32, u64, MidiNote, u8)> = None;
        for s in self.slots.iter().filter(|s| s.active) {
            let level = s.voice.level_estimate();
            if best.is_none_or(|(l, f, _, _)| level < l || (level == l && s.start_frame < f)) {
                best = Some((level, s.start_frame, s.pipe.note, s.pipe.division));
            }
        }
        let Some((_, _, note, division)) = best else {
            return 0;
        };
        let mut n = 0;
        for i in 0..self.slots.len() {
            let s = &self.slots[i];
            if s.active && s.pipe.note == note && s.pipe.division == division {
                self.slots[i].voice.silence();
                self.reclaim(i);
                n += 1;
            }
        }
        n
    }
}
