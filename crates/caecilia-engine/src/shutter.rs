//! The swell box's low-pass.

/// A one-pole low-pass, `y = b0 x - a1 y[-1]`, coefficients from the exact
/// impulse-invariant pole. Kept here rather than taken from the framework
/// because the 1.x references were rendered through exactly this one.
#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct OnePole {
    b0: f32,
    a1: f32,
    y1: f32,
}

impl OnePole {
    pub fn set_lowpass(&mut self, sample_rate: f64, cutoff_hz: f32) {
        let fc = f64::from(cutoff_hz).clamp(1.0, sample_rate * 0.49);
        let a = (-std::f64::consts::TAU * fc / sample_rate).exp();
        self.b0 = (1.0 - a) as f32;
        self.a1 = (-a) as f32;
    }

    pub fn reset(&mut self) {
        self.y1 = 0.0;
    }

    pub fn process_in_place(&mut self, x: &mut [f32]) {
        let mut y1 = self.y1;
        for s in x {
            y1 = self.b0 * *s - self.a1 * y1;
            *s = y1;
        }
        // Subnormals cost nothing here once the host's FTZ is on, but a box held
        // shut over silence must not leave one behind for the next block.
        self.y1 = if y1.abs() < 1.0e-30 { 0.0 } else { y1 };
    }
}
