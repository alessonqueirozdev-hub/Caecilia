//! A windchest.

use caecilia_model::WindchestId;

use crate::config::WindchestConfig;

/// A distribution node: it follows the reservoir PROPORTIONALLY behind its own
/// regulator, and loses a little more to its own trunk in proportion to the
/// flow it draws.
///
/// Proportionally, because organ builders wind divisions at different
/// pressures on purpose (here 980 Pa on the Pédale, 812 on the Grand-Orgue,
/// 735 on the Récit): a heavy pedal chord makes the Récit flinch without
/// dragging it down to the Pédale's wind.
#[derive(Clone, Copy, Debug)]
pub struct Windchest {
    config: WindchestConfig,
    start_pa: f32,
    end_pa: f32,
    frames: usize,
    demand: f32,
}

impl Windchest {
    /// A chest at its nominal pressure.
    pub fn new(config: WindchestConfig) -> Self {
        let mut c = Self {
            config,
            start_pa: 0.0,
            end_pa: 0.0,
            frames: 1,
            demand: 0.0,
        };
        c.reset();
        c
    }

    /// Back to nominal, no demand.
    pub fn reset(&mut self) {
        self.start_pa = self.config.nominal_pressure_pa;
        self.end_pa = self.config.nominal_pressure_pa;
        self.frames = 1;
        self.demand = 0.0;
    }

    /// Book a pipe's flow for this block.
    pub fn register_demand(&mut self, flow: f32) {
        if flow > 0.0 {
            self.demand += flow;
        }
    }

    /// Flow booked since the last block.
    pub fn pending_demand(&self) -> f32 {
        self.demand
    }

    /// Clear the booked flow.
    pub fn reset_demand(&mut self) {
        self.demand = 0.0;
    }

    /// Latch this block's trajectory from the reservoir's start and end
    /// pressure as fractions of its own nominal.
    pub fn update_block(&mut self, reservoir_start: f32, reservoir_end: f32, frames: usize) {
        let drop = if self.config.trunk_conductance > 0.0 {
            self.demand / self.config.trunk_conductance
        } else {
            0.0
        };
        let nominal = self.config.nominal_pressure_pa;
        self.start_pa = nominal * reservoir_start - drop;
        self.end_pa = nominal * reservoir_end - drop;
        self.frames = frames.max(1);
    }

    /// Chest pressure at a frame of the block, without the tremulant: a straight
    /// line between the block's ends.
    pub fn sag_pressure_at(&self, frame: usize) -> f32 {
        if self.frames <= 1 {
            return self.start_pa;
        }
        let t = (frame as f32 / (self.frames - 1) as f32).min(1.0);
        self.start_pa + (self.end_pa - self.start_pa) * t
    }

    /// Voiced pressure.
    pub fn nominal_pressure_pa(&self) -> f32 {
        self.config.nominal_pressure_pa
    }

    /// The reservoir feeding it.
    pub fn bellows(&self) -> u16 {
        self.config.bellows
    }

    /// Its tremulant, if any.
    pub fn tremulant(&self) -> Option<u16> {
        self.config.tremulant
    }

    /// Its identity.
    pub fn id(&self) -> WindchestId {
        self.config.id
    }
}
