//! The wind of one block, frozen, for voices rendering on other threads.

use caecilia_model::WindchestId;

use crate::chest::Windchest;
use crate::config::DEFAULT_NOMINAL_PRESSURE_PA;
use crate::tremulant::Tremulant;

/// Chests a snapshot holds.
pub const MAX_SNAPSHOT_CHESTS: usize = 16;

/// A chest's pressure at a frame, tremulant included: the one formula both
/// the live model and its snapshots read through.
pub(crate) fn chest_pressure(
    chest: &Windchest,
    tremulant: Option<&Tremulant>,
    frame: usize,
) -> f32 {
    let mut p = chest.sag_pressure_at(frame);
    if let Some(t) = tremulant {
        p += t.pressure_delta_pa(frame);
    }
    p.max(0.0)
}

/// Every chest's pressure trajectory for the current block, by chest id.
///
/// Plain data and `Copy`: taken once per block after the wind is stepped
/// and handed to each render job, so voices on any thread read the same
/// pressures the live model would give, without sharing it.
#[derive(Clone, Copy, Debug)]
pub struct WindSnapshot {
    chests: [Option<(Windchest, Option<Tremulant>)>; MAX_SNAPSHOT_CHESTS],
}

impl Default for WindSnapshot {
    /// No chests: every pressure reads nominal, so every deviation is zero.
    fn default() -> Self {
        Self {
            chests: [None; MAX_SNAPSHOT_CHESTS],
        }
    }
}

impl WindSnapshot {
    pub(crate) fn set(&mut self, index: usize, chest: Windchest, tremulant: Option<Tremulant>) {
        if let Some(slot) = self.chests.get_mut(index) {
            *slot = Some((chest, tremulant));
        }
    }

    fn get(&self, chest: WindchestId) -> Option<&(Windchest, Option<Tremulant>)> {
        self.chests
            .get(chest.index())?
            .as_ref()
            .filter(|(c, _)| c.id() == chest)
    }

    /// A chest's voiced pressure.
    pub fn nominal_pressure_pa(&self, chest: WindchestId) -> f32 {
        self.get(chest)
            .map_or(DEFAULT_NOMINAL_PRESSURE_PA, |(c, _)| {
                c.nominal_pressure_pa()
            })
    }

    /// A chest's pressure at a frame of the block.
    pub fn pressure_at(&self, chest: WindchestId, frame: usize) -> f32 {
        self.get(chest)
            .map_or(DEFAULT_NOMINAL_PRESSURE_PA, |(c, t)| {
                chest_pressure(c, t.as_ref(), frame)
            })
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
}
