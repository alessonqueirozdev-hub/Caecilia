//! Organ voices.
//!
//! [`PartialBank`] is the loop-free modeled sustain: partials as quadrature
//! phasors whose pitch, level and brightness follow the wind, each drifting on
//! its own slow walk, with mixture break-back, fixed-Hz reed formants and a
//! per-note treble tilt. [`AdditiveVoice`] makes one rank's pipe of it.
//!
//! The sample tier and the physical tier come later; this is the voice 1.x
//! actually plays, ported first so the references can hold it to its sound.

mod bank;
mod math;
mod voice;

pub use bank::{DEFAULT_MAX_PARTIALS, Liveliness, PartialBank, ReleaseStyle};
pub use voice::{AdditiveVoice, CONTROL_FRAMES, liveliness_for};
