//! What the audio thread tells the console: the wind in each chest, the
//! keys held, the voices sounding, how much of the block the render took.
//!
//! Atomics, written once a block and read whenever the console paints: no
//! lock, no allocation, and a reading may be a block old.

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

/// Chests whose pressure is reported.
pub const TELEMETRY_CHESTS: usize = 16;

/// The audio thread's report.
pub struct Telemetry {
    /// Each chest's pressure at the top of the next block, pascals, as `f32`
    /// bits; by chest index.
    pressure: [AtomicU32; TELEMETRY_CHESTS],
    /// Keys held on each MIDI channel, low and high halves.
    held: [[AtomicU64; 2]; 16],
    voices: AtomicU32,
    /// The render's share of the block's time, smoothed, as `f32` bits.
    load: AtomicU32,
}

impl Default for Telemetry {
    fn default() -> Self {
        Self {
            pressure: std::array::from_fn(|_| AtomicU32::new(0)),
            held: std::array::from_fn(|_| [AtomicU64::new(0), AtomicU64::new(0)]),
            voices: AtomicU32::new(0),
            load: AtomicU32::new(0),
        }
    }
}

impl Telemetry {
    /// The pressure in chest `index`, pascals; 0 before the first block.
    pub fn pressure_pa(&self, index: usize) -> f32 {
        self.pressure
            .get(index)
            .map_or(0.0, |p| f32::from_bits(p.load(Ordering::Relaxed)))
    }

    /// The keys held on `channel`, bit `k` being key `k`.
    pub fn held(&self, channel: usize) -> u128 {
        self.held.get(channel).map_or(0, |[lo, hi]| {
            u128::from(lo.load(Ordering::Relaxed)) | u128::from(hi.load(Ordering::Relaxed)) << 64
        })
    }

    /// Voices sounding.
    pub fn voices(&self) -> u32 {
        self.voices.load(Ordering::Relaxed)
    }

    /// The render's share of the block's time, 0 to 1 and past it when the
    /// machine cannot keep up.
    pub fn load(&self) -> f32 {
        f32::from_bits(self.load.load(Ordering::Relaxed))
    }

    pub(crate) fn set_pressure(&self, index: usize, pa: f32) {
        if let Some(p) = self.pressure.get(index) {
            p.store(pa.to_bits(), Ordering::Relaxed);
        }
    }

    pub(crate) fn set_held(&self, held: &[u128; 16]) {
        for (slot, keys) in self.held.iter().zip(held) {
            slot[0].store(*keys as u64, Ordering::Relaxed);
            slot[1].store((*keys >> 64) as u64, Ordering::Relaxed);
        }
    }

    pub(crate) fn set_voices(&self, voices: usize) {
        self.voices
            .store(u32::try_from(voices).unwrap_or(u32::MAX), Ordering::Relaxed);
    }

    /// Fold in one block's share, smoothed over about a third of a second at
    /// usual block sizes.
    pub(crate) fn add_load(&self, share: f32) {
        let was = self.load();
        let now = if share.is_finite() {
            was + 0.05 * (share - was)
        } else {
            was
        };
        self.load.store(now.to_bits(), Ordering::Relaxed);
    }
}
