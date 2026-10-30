//! The whole wind graph.

use caecilia_model::{RankId, WindchestId};

use crate::bellows::Bellows;
use crate::chest::Windchest;
use crate::config::{DEFAULT_NOMINAL_PRESSURE_PA, WindModelConfig};
use crate::snapshot::{WindSnapshot, chest_pressure};
use crate::tremulant::Tremulant;

/// Reservoirs, chests, tremulants and the rank-to-chest routing.
///
/// Per block: voices book their flow with [`register_demand`](Self::register_demand),
/// the engine calls [`step`](Self::step), and voices then read
/// [`pressure_at`](Self::pressure_at) through a shared reference. Booked demand
/// feeds the NEXT step, so booking never disturbs what is being read.
#[derive(Clone, Debug, Default)]
pub struct WindModel {
    sample_rate: f64,
    bellows: Vec<Bellows>,
    chests: Vec<Windchest>,
    tremulants: Vec<Tremulant>,
    rank_chests: Vec<(RankId, WindchestId)>,
}

impl WindModel {
    /// Build from a configuration. Allocates: off the audio thread.
    pub fn new(sample_rate: f64, config: &WindModelConfig) -> Self {
        let chests: Vec<Windchest> = config.chests.iter().map(|&c| Windchest::new(c)).collect();
        // A tremulant's depth fraction maps to pascals on the first chest it serves.
        let tremulants = config
            .tremulants
            .iter()
            .enumerate()
            .map(|(t, &tc)| {
                let nominal = config
                    .chests
                    .iter()
                    .find(|c| c.tremulant == Some(t as u16))
                    .map_or(DEFAULT_NOMINAL_PRESSURE_PA, |c| c.nominal_pressure_pa);
                Tremulant::new(tc, nominal)
            })
            .collect();
        Self {
            sample_rate: if sample_rate > 0.0 {
                sample_rate
            } else {
                48000.0
            },
            bellows: config.bellows.iter().map(|&b| Bellows::new(b)).collect(),
            chests,
            tremulants,
            rank_chests: config.rank_chests.clone(),
        }
    }

    /// Everything back to rest.
    pub fn reset(&mut self) {
        self.bellows.iter_mut().for_each(Bellows::reset);
        self.chests.iter_mut().for_each(Windchest::reset);
        self.tremulants.iter_mut().for_each(Tremulant::reset);
    }

    /// Integrate one block of `frames` under the demand booked since the last.
    pub fn step(&mut self, frames: usize) {
        let dt = frames as f64 / self.sample_rate;
        for b in &mut self.bellows {
            b.begin_step();
        }
        for c in &self.chests {
            if let Some(b) = self.bellows.get_mut(usize::from(c.bellows())) {
                b.add_demand(c.pending_demand());
            }
        }
        for b in &mut self.bellows {
            b.integrate(dt);
        }
        for t in &mut self.tremulants {
            t.advance_block(frames, self.sample_rate);
        }
        for c in &mut self.chests {
            let (mut start, mut end) = (1.0, 1.0);
            if let Some(b) = self.bellows.get(usize::from(c.bellows())) {
                let n = b.nominal_pressure_pa();
                if n > 0.0 {
                    start = b.pressure_start_pa() / n;
                    end = b.pressure_end_pa() / n;
                }
            }
            c.update_block(start, end, frames);
            c.reset_demand();
        }
    }

    fn index(&self, chest: WindchestId) -> Option<usize> {
        let i = chest.index();
        if self.chests.get(i).is_some_and(|c| c.id() == chest) {
            return Some(i);
        }
        self.chests.iter().position(|c| c.id() == chest)
    }

    /// Number of chests.
    pub fn chest_count(&self) -> usize {
        self.chests.len()
    }

    /// A chest's voiced pressure.
    pub fn nominal_pressure_pa(&self, chest: WindchestId) -> f32 {
        self.index(chest).map_or(DEFAULT_NOMINAL_PRESSURE_PA, |i| {
            self.chests[i].nominal_pressure_pa()
        })
    }

    /// A chest's pressure at a frame of the current block, tremulant included.
    pub fn pressure_at(&self, chest: WindchestId, frame: usize) -> f32 {
        let Some(i) = self.index(chest) else {
            return DEFAULT_NOMINAL_PRESSURE_PA;
        };
        let c = &self.chests[i];
        let t = c
            .tremulant()
            .and_then(|t| self.tremulants.get(usize::from(t)));
        chest_pressure(c, t, frame)
    }

    /// The block's pressures, frozen: what a voice on another thread reads.
    pub fn snapshot(&self) -> WindSnapshot {
        let mut s = WindSnapshot::default();
        for (i, c) in self.chests.iter().enumerate() {
            let t = c
                .tremulant()
                .and_then(|t| self.tremulants.get(usize::from(t)))
                .copied();
            s.set(i, *c, t);
        }
        s
    }

    /// `(pressure - nominal) / nominal` at a frame.
    pub fn deviation_at(&self, chest: WindchestId, frame: usize) -> f32 {
        let n = self.nominal_pressure_pa(chest);
        if n <= 0.0 {
            0.0
        } else {
            (self.pressure_at(chest, frame) - n) / n
        }
    }

    /// The chest a rank stands on; unrouted ranks stand on the first.
    pub fn chest_for_rank(&self, rank: RankId) -> WindchestId {
        self.rank_chests
            .iter()
            .find(|(r, _)| *r == rank)
            .map(|&(_, c)| c)
            .or_else(|| self.chests.first().map(Windchest::id))
            .unwrap_or_default()
    }

    /// Book flow drawn from a chest; it loads the next step.
    pub fn register_demand(&mut self, chest: WindchestId, flow: f32) {
        if let Some(i) = self.index(chest) {
            self.chests[i].register_demand(flow);
        }
    }

    /// Draw or retire the tremulant of a chest. A chest without one ignores it,
    /// as a console without that tremulant would.
    pub fn set_chest_tremulant_enabled(&mut self, chest: WindchestId, on: bool) {
        if let Some(t) = self.index(chest).and_then(|i| self.chests[i].tremulant()) {
            if let Some(t) = self.tremulants.get_mut(usize::from(t)) {
                t.set_enabled(on);
            }
        }
    }

    /// Change the rate and depth of a chest's tremulant.
    pub fn set_chest_tremulant_shape(
        &mut self,
        chest: WindchestId,
        rate_hz: f32,
        depth_fraction: f32,
    ) {
        if let Some(i) = self.index(chest) {
            let nominal = self.chests[i].nominal_pressure_pa();
            if let Some(t) = self.chests[i]
                .tremulant()
                .and_then(|t| self.tremulants.get_mut(usize::from(t)))
            {
                t.set_shape(rate_hz, depth_fraction, nominal);
            }
        }
    }

    /// The tremulant of a chest, if it has one.
    pub fn chest_tremulant(&self, chest: WindchestId) -> Option<&Tremulant> {
        let t = self.chests.get(self.index(chest)?)?.tremulant()?;
        self.tremulants.get(usize::from(t))
    }

    /// Sag of the first reservoir as a fraction of nominal: 0 at rest, negative
    /// as it droops.
    pub fn sag_norm(&self) -> f32 {
        self.bellows.first().map_or(0.0, |b| {
            let n = b.nominal_pressure_pa();
            if n > 0.0 {
                (b.pressure_end_pa() - n) / n
            } else {
                0.0
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::config_from_organ;
    use caecilia_model::demo_organ;

    fn demo() -> WindModel {
        WindModel::new(48000.0, &config_from_organ(&demo_organ()))
    }

    #[test]
    fn each_chest_keeps_the_pressure_it_was_voiced_at() {
        let mut w = demo();
        w.register_demand(WindchestId(0), 20.0);
        w.step(256);
        for _ in 0..400 {
            w.register_demand(WindchestId(0), 20.0);
            w.step(256);
        }
        // The pedal chord moves every chest by the same FRACTION; the pedal chest
        // also loses its own trunk drop.
        let frac =
            |c: u16| w.pressure_at(WindchestId(c), 0) / w.nominal_pressure_pa(WindchestId(c));
        assert!((frac(1) - frac(2)).abs() < 1e-5);
        assert!(frac(1) < 1.0);
        assert!(frac(0) < frac(1));
        assert!((w.pressure_at(WindchestId(2), 0) - 735.0 * frac(2)).abs() < 1e-3);
    }

    #[test]
    fn only_the_recit_has_a_tremulant_and_it_shakes_only_the_recit() {
        let mut w = demo();
        for c in 0..3 {
            w.set_chest_tremulant_shape(WindchestId(c), 5.5, 0.15);
            w.set_chest_tremulant_enabled(WindchestId(c), true);
        }
        let mut swing = [0.0f32; 3];
        for _ in 0..400 {
            w.step(256);
            for c in 0..3u16 {
                for f in (0..256).step_by(32) {
                    let d = (w.pressure_at(WindchestId(c), f)
                        - w.nominal_pressure_pa(WindchestId(c)))
                    .abs();
                    swing[usize::from(c)] = swing[usize::from(c)].max(d);
                }
            }
        }
        assert_eq!(swing[0], 0.0);
        assert_eq!(swing[1], 0.0);
        assert!(
            (swing[2] - 0.15 * 735.0).abs() < 1.0,
            "Récit swing {}",
            swing[2]
        );
    }
}
