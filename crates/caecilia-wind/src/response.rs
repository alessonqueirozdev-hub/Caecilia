//! How each family answers its wind.

use caecilia_model::TonalFamily;

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
