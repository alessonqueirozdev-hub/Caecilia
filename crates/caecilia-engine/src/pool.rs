//! The voice pool: one slot per sounding pipe, in groups that render on
//! several cores.

use caecilia_model::{Calibration, DivisionId, MidiNote, PipeId, WindchestId};
use caecilia_synth::AdditiveVoice;
use caecilia_wind::WindSnapshot;
use manguebyte_core::pool::{BlockReport, Job, PoolConfig, RenderPool};

use crate::{MAX_CHESTS, MAX_DIVISIONS};

/// Voices a render job holds. 512 voices make eight jobs: enough to spread
/// over the cores a machine has, few enough that each is worth waking a
/// worker for.
pub(crate) const GROUP_SIZE: usize = 64;

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

/// A render job: a group of voices and its own bus per chest. It renders
/// whole on one thread; the engine sums the groups' buses in index order,
/// so the output is the same on any number of cores.
pub(crate) struct VoiceGroup {
    slots: Vec<Slot>,
    free: Vec<u16>,
    bus_l: Vec<Vec<f32>>,
    bus_r: Vec<Vec<f32>>,
    chests: usize,
    frames: usize,
    offset: usize,
    wind: WindSnapshot,
    expr: [(f32, f32); MAX_DIVISIONS],
}

impl VoiceGroup {
    fn reclaim(&mut self, i: usize) {
        let s = &mut self.slots[i];
        s.active = false;
        s.sustained = false;
        self.free.push(i as u16);
    }

    fn active(&self) -> usize {
        self.slots.len() - self.free.len()
    }
}

impl Job for VoiceGroup {
    fn render(&mut self) {
        let n = self.frames;
        for c in 0..self.chests {
            self.bus_l[c][..n].fill(0.0);
            self.bus_r[c][..n].fill(0.0);
        }
        for i in 0..self.slots.len() {
            if self.slots[i].active && !self.slots[i].voice.is_active() {
                self.reclaim(i);
            }
        }
        for s in self.slots.iter_mut().filter(|s| s.active) {
            let (start, inc) = self
                .expr
                .get(usize::from(s.pipe.division))
                .copied()
                .unwrap_or((1.0, 0.0));
            s.voice.set_expression(start, inc);
            let c = if s.chest.index() < self.chests {
                s.chest.index()
            } else {
                0
            };
            s.voice.render_add(
                &mut self.bus_l[c][..n],
                &mut self.bus_r[c][..n],
                Some(&self.wind),
                self.offset,
            );
        }
    }
}

/// Fixed-capacity and allocation-free after construction. Operations reach
/// every group the pool can lend; a group a late worker still renders (only
/// after a missed block) sits that operation out.
pub(crate) struct Pool {
    groups: RenderPool<VoiceGroup>,
    count: usize,
}

impl std::fmt::Debug for Pool {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Pool").field("groups", &self.count).finish()
    }
}

impl Pool {
    pub fn new(
        sample_rate: f64,
        capacity: usize,
        calibration: Calibration,
        chests: usize,
        max_block: usize,
        config: PoolConfig,
    ) -> Self {
        let count = capacity.div_ceil(GROUP_SIZE).max(1);
        let groups = (0..count)
            .map(|g| {
                let size = GROUP_SIZE.min(capacity - g * GROUP_SIZE);
                VoiceGroup {
                    slots: (0..size)
                        .map(|_| Slot {
                            voice: AdditiveVoice::new(sample_rate, calibration),
                            active: false,
                            pipe: PipeId::default(),
                            start_frame: 0,
                            sustained: false,
                            flow: 0.0,
                            chest: WindchestId(0),
                        })
                        .collect(),
                    // Popped from the end: slot 0 is handed out first.
                    free: (0..size as u16).rev().collect(),
                    bus_l: vec![vec![0.0; max_block]; chests],
                    bus_r: vec![vec![0.0; max_block]; chests],
                    chests,
                    frames: 0,
                    offset: 0,
                    wind: WindSnapshot::default(),
                    expr: [(1.0, 0.0); MAX_DIVISIONS],
                }
            })
            .collect();
        Self {
            groups: RenderPool::new(groups, config),
            count,
        }
    }

    fn each(&mut self, mut f: impl FnMut(&mut VoiceGroup)) {
        for g in 0..self.count {
            if let Some(job) = self.groups.job_mut(g) {
                f(job);
            }
        }
    }

    pub fn free_count(&self) -> usize {
        (0..self.count)
            .filter_map(|g| self.groups.job(g))
            .map(|j| j.free.len())
            .sum()
    }

    pub fn active_count(&self) -> usize {
        (0..self.count)
            .filter_map(|g| self.groups.job(g))
            .map(VoiceGroup::active)
            .sum()
    }

    /// Flow booked per chest by every sounding pipe.
    pub fn flows(&self, chests: usize) -> [f32; MAX_CHESTS] {
        let mut per = [0.0f32; MAX_CHESTS];
        for job in (0..self.count).filter_map(|g| self.groups.job(g)) {
            for s in job.slots.iter().filter(|s| s.active) {
                let c = if s.chest.index() < chests {
                    s.chest.index()
                } else {
                    0
                };
                per[c] += s.flow;
            }
        }
        per
    }

    /// A free slot for `pipe` in the least busy group, so the work spreads
    /// over the cores; `None` when the pool is full.
    pub fn acquire(&mut self, pipe: PipeId, frame: u64) -> Option<&mut Slot> {
        let g = (0..self.count)
            .filter_map(|g| self.groups.job(g).map(|j| (g, j.free.len())))
            .filter(|&(_, free)| free > 0)
            .max_by_key(|&(g, free)| (free, std::cmp::Reverse(g)))?
            .0;
        let job = self.groups.job_mut(g)?;
        let i = usize::from(job.free.pop()?);
        let s = &mut job.slots[i];
        s.active = true;
        s.pipe = pipe;
        s.start_frame = frame;
        s.sustained = false;
        Some(s)
    }

    /// Release every voice sounding `pipe`.
    pub fn note_off(&mut self, pipe: PipeId) {
        self.each(|job| {
            for s in job.slots.iter_mut().filter(|s| s.active && s.pipe == pipe) {
                s.voice.note_off();
                s.sustained = false;
            }
        });
    }

    /// Mark every voice sounding `pipe` as held by the sustain pedal.
    pub fn hold_for_sustain(&mut self, pipe: PipeId) {
        self.each(|job| {
            for s in job.slots.iter_mut().filter(|s| s.active && s.pipe == pipe) {
                s.sustained = true;
            }
        });
    }

    /// Release what the pedal of `division` was holding.
    pub fn release_sustained(&mut self, division: DivisionId) {
        self.each(|job| {
            for s in job
                .slots
                .iter_mut()
                .filter(|s| s.active && s.sustained && u16::from(s.pipe.division) == division.0)
            {
                s.voice.note_off();
                s.sustained = false;
            }
        });
    }

    /// A musical all-notes-off: everything releases.
    pub fn all_notes_off(&mut self) {
        self.each(|job| {
            for s in job.slots.iter_mut().filter(|s| s.active) {
                s.voice.note_off();
                s.sustained = false;
            }
        });
    }

    /// Stop everything dead and free every slot.
    pub fn silence_all(&mut self) {
        self.each(|job| {
            for i in 0..job.slots.len() {
                if job.slots[i].active {
                    job.slots[i].voice.silence();
                    job.reclaim(i);
                }
            }
        });
    }

    /// Steal a whole KEY -- every voice of the quietest sounding pipe's key and
    /// division, since taking one rank off a chord is more audible than ending
    /// a note early. Ties go to the oldest. Returns how many slots were freed.
    pub fn steal_quietest_key(&mut self) -> usize {
        let mut best: Option<(f32, u64, MidiNote, u8)> = None;
        for job in (0..self.count).filter_map(|g| self.groups.job(g)) {
            for s in job.slots.iter().filter(|s| s.active) {
                let level = s.voice.level_estimate();
                if best.is_none_or(|(l, f, _, _)| level < l || (level == l && s.start_frame < f)) {
                    best = Some((level, s.start_frame, s.pipe.note, s.pipe.division));
                }
            }
        }
        let Some((_, _, note, division)) = best else {
            return 0;
        };
        let mut n = 0;
        self.each(|job| {
            for i in 0..job.slots.len() {
                let s = &job.slots[i];
                if s.active && s.pipe.note == note && s.pipe.division == division {
                    job.slots[i].voice.silence();
                    job.reclaim(i);
                    n += 1;
                }
            }
        });
        n
    }

    /// Render every group for a slice of `frames` starting at `offset` in the
    /// wind's block.
    pub fn render(
        &mut self,
        frames: usize,
        offset: usize,
        wind: &WindSnapshot,
        expr: &[(f32, f32); MAX_DIVISIONS],
    ) -> BlockReport {
        self.each(|job| {
            job.frames = frames;
            job.offset = offset;
            job.wind = *wind;
            job.expr = *expr;
        });
        self.groups.run(self.count)
    }

    /// Add chest `c`'s bus of every group rendered, in index order.
    pub fn mix_chest(&self, c: usize, left: &mut [f32], right: &mut [f32]) {
        for (_, job) in self.groups.rendered() {
            let n = left.len().min(job.frames);
            for (o, b) in left[..n].iter_mut().zip(&job.bus_l[c][..n]) {
                *o += b;
            }
            for (o, b) in right[..n].iter_mut().zip(&job.bus_r[c][..n]) {
                *o += b;
            }
        }
    }
}
