// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The sound after the pipes: the room the organ stands in, and the master
//! EQ that voices the whole instrument, as 1.x had them.

use manguebyte_core::AudioBuffer;
use manguebyte_dsp::{Filter, FilterShape, ProcessSpec, Processor, Reverb};

/// The rooms, in the order the parameter lists them.
pub const ROOM_NAMES: [&str; 5] = ["Room", "Chamber", "Hall", "Cathedral", "Plate"];

/// The room an instance opens in: the hall.
pub const DEFAULT_ROOM: usize = 2;

/// How a room rings: its tail in seconds, its size, how dark its walls
/// (0 bright to 1 dark), and how wide it spreads.
struct Room {
    decay: f64,
    size: f64,
    damping: f64,
    width: f64,
}

/// The tails are 1.x's presets; size, damping and width are what the
/// framework's reverb calls them.
const ROOMS: [Room; 5] = [
    Room {
        decay: 0.8,
        size: 0.35,
        damping: 0.55,
        width: 0.6,
    },
    Room {
        decay: 1.6,
        size: 0.5,
        damping: 0.45,
        width: 0.8,
    },
    Room {
        decay: 2.6,
        size: 0.75,
        damping: 0.38,
        width: 1.0,
    },
    Room {
        decay: 5.2,
        size: 1.0,
        damping: 0.35,
        width: 1.0,
    },
    Room {
        decay: 2.0,
        size: 0.45,
        damping: 0.2,
        width: 0.9,
    },
];

/// Put the reverb in room `n` of [`ROOM_NAMES`]. Real-time safe.
pub(crate) fn set_room(reverb: &mut Reverb, n: usize) {
    let room = &ROOMS[n.min(ROOMS.len() - 1)];
    reverb.set_decay_seconds(room.decay);
    reverb.set_size(room.size);
    reverb.set_damping(room.damping);
    reverb.set_width(room.width);
}

/// A band of the master EQ: its parameter id and name, where it sits, how
/// wide, its shape, and the organ's voicing.
pub struct EqBand {
    /// Its parameter id.
    pub id: &'static str,
    /// What it is called.
    pub name: &'static str,
    frequency: f64,
    q: f64,
    shelf: Option<bool>,
    /// Its gain as the organ is voiced, dB.
    pub voicing_db: f32,
}

/// The five bands, low to high: a low shelf, three peaks, a high shelf.
/// Where they sit and the voicing are 1.x's: gentle, flattering every
/// registration and barely touching the headroom.
pub const EQ_BANDS: [EqBand; 5] = [
    EqBand {
        id: "eq-warmth",
        name: "Warmth",
        frequency: 110.0,
        q: 0.7,
        shelf: Some(false),
        voicing_db: 2.0,
    },
    EqBand {
        id: "eq-boxiness",
        name: "Boxiness",
        frequency: 315.0,
        q: 1.1,
        shelf: None,
        voicing_db: -2.5,
    },
    EqBand {
        id: "eq-body",
        name: "Body",
        frequency: 800.0,
        q: 0.8,
        shelf: None,
        voicing_db: 0.0,
    },
    EqBand {
        id: "eq-presence",
        name: "Presence",
        frequency: 3200.0,
        q: 0.9,
        shelf: None,
        voicing_db: 1.5,
    },
    EqBand {
        id: "eq-air",
        name: "Air",
        frequency: 9000.0,
        q: 0.7,
        shelf: Some(true),
        voicing_db: 2.0,
    },
];

/// The most a band moves in one block, so a fader dragged never clicks.
const GLIDE_DB: f32 = 0.5;

fn shape(band: &EqBand, gain_db: f32) -> FilterShape {
    let gain_db = f64::from(gain_db);
    match band.shelf {
        Some(false) => FilterShape::LowShelf { gain_db },
        Some(true) => FilterShape::HighShelf { gain_db },
        None => FilterShape::Peak { gain_db },
    }
}

/// The master EQ: five sections, each gliding to the gain asked of it.
pub(crate) struct MasterEq {
    filters: [Filter<f32>; 5],
    now_db: [f32; 5],
}

impl MasterEq {
    pub(crate) fn new() -> Self {
        Self {
            filters: std::array::from_fn(|i| {
                let band = &EQ_BANDS[i];
                Filter::new(shape(band, 0.0), band.frequency, band.q)
            }),
            now_db: [0.0; 5],
        }
    }

    pub(crate) fn prepare(&mut self, spec: &ProcessSpec) {
        for f in &mut self.filters {
            f.prepare(spec);
        }
    }

    pub(crate) fn reset(&mut self) {
        for f in &mut self.filters {
            f.reset();
        }
    }

    /// Voice `buffer` with the gains asked, or none when the EQ is off; a
    /// band whose gain is nothing is passed by.
    pub(crate) fn process(&mut self, buffer: &mut AudioBuffer<f32>, on: bool, gains_db: [f32; 5]) {
        for (i, f) in self.filters.iter_mut().enumerate() {
            let target = if on { gains_db[i] } else { 0.0 };
            let now = self.now_db[i];
            if now != target {
                let next = now + (target - now).clamp(-GLIDE_DB, GLIDE_DB);
                self.now_db[i] = next;
                f.set_shape(shape(&EQ_BANDS[i], next));
            }
            if self.now_db[i] != 0.0 {
                f.process(buffer);
            } else {
                f.reset();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tone(frequency: f32) -> AudioBuffer<f32> {
        let mut b = AudioBuffer::<f32>::new(2, 4096);
        for ch in 0..2 {
            for (i, v) in b.channel_mut(ch).iter_mut().enumerate() {
                *v = (std::f32::consts::TAU * frequency * i as f32 / 48_000.0).sin() * 0.5;
            }
        }
        b
    }

    fn peak(b: &AudioBuffer<f32>) -> f32 {
        b.channel(0)[2048..].iter().fold(0.0, |m, v| m.max(v.abs()))
    }

    #[test]
    fn a_band_lifts_its_own_frequencies() {
        let spec = ProcessSpec {
            sample_rate: 48_000.0,
            max_block_size: 4096,
            num_channels: 2,
        };
        let mut eq = MasterEq::new();
        eq.prepare(&spec);
        let mut gains = [0.0; 5];
        gains[4] = 12.0;
        // Long enough to glide all the way.
        for _ in 0..30 {
            eq.process(&mut tone(10.0), true, gains);
        }
        let mut air = tone(12_000.0);
        eq.process(&mut air, true, gains);
        let mut low = tone(100.0);
        eq.process(&mut low, true, gains);
        // Past its corner a shelf is most of the way up: above +6 dB.
        assert!(peak(&air) > 1.0, "12 kHz lifted: {}", peak(&air));
        assert!(
            (peak(&low) - 0.5).abs() < 0.05,
            "100 Hz left: {}",
            peak(&low)
        );

        // Off, it glides back to nothing and passes the sound untouched.
        for _ in 0..30 {
            eq.process(&mut tone(10.0), false, gains);
        }
        let mut air = tone(12_000.0);
        eq.process(&mut air, false, gains);
        assert!((peak(&air) - 0.5).abs() < 1e-6);
    }
}
