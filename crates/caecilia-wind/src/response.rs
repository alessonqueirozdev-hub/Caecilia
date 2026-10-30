//! How each family answers its wind.

use caecilia_model::{Calibration, TonalFamily};

/// What a unit of normalised pressure deviation does to a pipe: pitch (FM),
/// level (AM), brightness and speech. Reeds swing hardest, flutes least.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct WindResponseCurve {
    /// Cents per unit deviation.
    pub cents_per_deviation: f32,
    /// dB per unit deviation.
    pub db_per_deviation: f32,
    /// Brightness change per unit deviation.
    pub brightness_per_deviation: f32,
    /// Speech sensitivity per unit deviation.
    pub attack_per_deviation: f32,
}

impl WindResponseCurve {
    /// Pitch shift for a deviation.
    pub fn pitch_cents(&self, deviation: f32) -> f32 {
        self.cents_per_deviation * deviation
    }
    /// Level change for a deviation.
    pub fn level_db(&self, deviation: f32) -> f32 {
        self.db_per_deviation * deviation
    }
    /// Brightness change for a deviation.
    pub fn brightness(&self, deviation: f32) -> f32 {
        self.brightness_per_deviation * deviation
    }
    /// Speech change for a deviation.
    pub fn attack(&self, deviation: f32) -> f32 {
        self.attack_per_deviation * deviation
    }
}

/// The curves under a calibration.
///
/// 2.0 keeps 1.x's pitch, brightness and speech axes and derives the LEVEL
/// from the physics instead of hand-setting it: a flue pipe's radiated
/// amplitude is close to proportional to its blowing pressure, so a deviation
/// `d` changes the level by `20 log10(1 + d)`, about 8.7 dB per unit at small
/// deviations; a beating reed answers harder, as pressure to the 1.3. 1.x's 2 to
/// 6 dB per unit is why its tremulant barely reached the envelope.
pub fn response_for(family: TonalFamily, cal: Calibration) -> WindResponseCurve {
    let mut c = default_response_for(family);
    if cal == Calibration::V2 {
        let exponent = match family {
            TonalFamily::Reed => 1.3,
            TonalFamily::Percussion => 0.2,
            _ => 1.0,
        };
        c.db_per_deviation = 20.0 / std::f32::consts::LN_10 * exponent;
    }
    c
}

/// 1.x's hand-set curves. To be replaced by per-rank values stated in the
/// organ file and calibrated against measured pipes.
pub fn default_response_for(family: TonalFamily) -> WindResponseCurve {
    let (c, db, b, a) = match family {
        TonalFamily::Reed => (90.0, 6.0, 1.20, 0.80),
        TonalFamily::String => (45.0, 3.5, 0.90, 0.50),
        TonalFamily::Principal | TonalFamily::Undefined => (20.0, 2.5, 0.45, 0.35),
        TonalFamily::Flute => (15.0, 2.0, 0.30, 0.30),
        TonalFamily::Mixture => (25.0, 3.0, 0.60, 0.40),
        TonalFamily::Hybrid => (35.0, 3.0, 0.70, 0.45),
        TonalFamily::Percussion => (5.0, 1.0, 0.10, 0.10),
    };
    WindResponseCurve {
        cents_per_deviation: c,
        db_per_deviation: db,
        brightness_per_deviation: b,
        attack_per_deviation: a,
    }
}
