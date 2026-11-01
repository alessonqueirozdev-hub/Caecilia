// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! A tremulant.

use crate::config::{TremulantConfig, TremulantWaveform};

fn wrap01(phase: f64) -> f64 {
    let p = phase - phase.floor();
    if p >= 1.0 { 0.0 } else { p }
}

/// Modulation of a chest's pressure itself, not of the output gain, so that
/// downstream it moves pitch, level and timbre together.
///
/// Phase advances once per block and per-frame values are evaluated
/// analytically, so it is sample-accurate. Engaging, disengaging and changing
/// depth all glide, so none of them clicks.
#[derive(Clone, Copy, Debug)]
pub struct Tremulant {
    config: TremulantConfig,
    depth_pa: f32,
    depth_target_pa: f32,
    phase: f64,
    phase_start: f64,
    phase_inc: f64,
    enabled: bool,
    gain: f32,
}

impl Tremulant {
    /// A tremulant on a chest of `chest_nominal_pa`.
    pub fn new(config: TremulantConfig, chest_nominal_pa: f32) -> Self {
        let mut t = Self {
            config,
            depth_pa: 0.0,
            depth_target_pa: config.depth_fraction * chest_nominal_pa,
            phase: 0.0,
            phase_start: 0.0,
            phase_inc: 0.0,
            enabled: false,
            gain: 0.0,
        };
        t.reset();
        t
    }

    /// Back to the start of a cycle, at its drawn-at-start state.
    pub fn reset(&mut self) {
        self.depth_pa = self.depth_target_pa;
        self.phase = 0.0;
        self.phase_start = 0.0;
        self.phase_inc = 0.0;
        self.enabled = self.config.enabled_at_start;
        self.gain = if self.enabled { 1.0 } else { 0.0 };
    }

    /// Draw or retire it; the depth glides.
    pub fn set_enabled(&mut self, on: bool) {
        self.enabled = on;
    }

    /// Whether it is drawn.
    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    /// Change rate and depth; the depth glides there.
    pub fn set_shape(&mut self, rate_hz: f32, depth_fraction: f32, chest_nominal_pa: f32) {
        self.config.rate_hz = rate_hz;
        self.config.depth_fraction = depth_fraction;
        self.depth_target_pa = depth_fraction * chest_nominal_pa;
    }

    /// Rate in Hz.
    pub fn rate_hz(&self) -> f32 {
        self.config.rate_hz
    }

    /// Phase at the start of the current block, in [0, 1).
    pub fn block_start_phase(&self) -> f64 {
        self.phase_start
    }

    /// Advance across a block.
    pub fn advance_block(&mut self, frames: usize, sample_rate: f64) {
        self.phase_start = self.phase;
        self.phase_inc = if sample_rate > 0.0 {
            f64::from(self.config.rate_hz) / sample_rate
        } else {
            0.0
        };
        self.phase = wrap01(self.phase + self.phase_inc * frames as f64);
        let target = if self.enabled { 1.0 } else { 0.0 };
        self.gain += (target - self.gain) * 0.25;
        self.depth_pa += (self.depth_target_pa - self.depth_pa) * 0.25;
    }

    /// The pressure it adds at a frame of the current block.
    pub fn pressure_delta_pa(&self, frame: usize) -> f32 {
        if self.gain <= 0.0 || self.depth_pa == 0.0 {
            return 0.0;
        }
        let phase = wrap01(self.phase_start + self.phase_inc * frame as f64);
        self.depth_pa * self.waveform(phase) * self.gain
    }

    fn waveform(&self, p: f64) -> f32 {
        (match self.config.waveform {
            TremulantWaveform::Sine => (std::f64::consts::TAU * p).sin(),
            TremulantWaveform::Triangle => {
                if p < 0.5 {
                    4.0 * p - 1.0
                } else {
                    3.0 - 4.0 * p
                }
            }
            TremulantWaveform::AsymmetricSaw => {
                if p < 0.25 {
                    8.0 * p - 1.0
                } else {
                    1.0 - (p - 0.25) / 0.75 * 2.0
                }
            }
        }) as f32
    }
}
