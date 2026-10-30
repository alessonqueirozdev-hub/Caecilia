//! Which voicing the instrument speaks with.

/// 1.x exactly, or the 2.0 calibration.
///
/// `OneX` exists so the port can keep proving, against `reference/cpp-1.x`,
/// that it reproduces 1.x to the last hundredth of a decibel. `V2` is what the
/// instrument plays: every place it departs from 1.x is a named, measured
/// change with a test that says how far it moved.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Calibration {
    /// The C++ 1.x instrument, reproduced.
    OneX,
    /// Caecilia 2.0.
    #[default]
    V2,
}
