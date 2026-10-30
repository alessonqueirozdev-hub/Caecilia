//! A reservoir.

use crate::config::BellowsConfig;

/// The pressure vessel that regulates the wind, integrated once per block.
///
/// Both orders are solved in closed form over the block, so the step is stable
/// at any buffer size a host hands over: an explicit integrator at 4.5 Hz goes
/// unstable around 2048 frames, which is a buffer size hosts really use.
#[derive(Clone, Debug)]
pub struct Bellows {
    config: BellowsConfig,
    pressure_pa: f32,
    /// dP/dt in Pa/s; the plate's second state variable, zero without one.
    dpdt: f32,
    start_pa: f32,
    end_pa: f32,
    demand: f32,
}

impl Bellows {
    /// A reservoir at its nominal pressure.
    pub fn new(config: BellowsConfig) -> Self {
        let mut b = Self {
            config,
            pressure_pa: 0.0,
            dpdt: 0.0,
            start_pa: 0.0,
            end_pa: 0.0,
            demand: 0.0,
        };
        b.reset();
        b
    }

    /// Back to nominal, no demand.
    pub fn reset(&mut self) {
        let n = self.config.nominal_pressure_pa;
        self.pressure_pa = n;
        self.dpdt = 0.0;
        self.start_pa = n;
        self.end_pa = n;
        self.demand = 0.0;
    }

    /// Clear the demand booked for the block about to be integrated.
    pub fn begin_step(&mut self) {
        self.demand = 0.0;
    }

    /// Book flow drawn this block. Negative demand is ignored, so a stray value
    /// can never inflate the reservoir above nominal.
    pub fn add_demand(&mut self, flow: f32) {
        if flow > 0.0 {
            self.demand += flow;
        }
    }

    /// Advance by one block of `dt` seconds.
    pub fn integrate(&mut self, dt: f64) {
        self.start_pa = self.pressure_pa;
        let sag = if self.config.feed_conductance > 0.0 {
            self.demand / self.config.feed_conductance
        } else {
            0.0
        };
        let equilibrium = self.config.nominal_pressure_pa - sag;

        if self.config.plate_resonance_hz > 0.0 {
            self.integrate_plate(equilibrium, dt);
        } else {
            let rate = if self.config.compliance > 0.0 {
                f64::from(self.config.feed_conductance) / f64::from(self.config.compliance)
            } else {
                0.0
            };
            let alpha = (1.0 - (-rate * dt).exp()) as f32;
            self.pressure_pa += (equilibrium - self.pressure_pa) * alpha;
        }
        self.end_pa = self.pressure_pa;
    }

    /// `p'' + 2 zeta w0 p' + w0^2 (p - p_eq) = 0`, solved exactly: the demand is
    /// booked once per block, so `p_eq` is constant over it.
    fn integrate_plate(&mut self, equilibrium: f32, dt: f64) {
        let w0 = std::f64::consts::TAU * f64::from(self.config.plate_resonance_hz);
        let zeta = f64::from(self.config.plate_damping.clamp(0.0, 0.999));
        let a = zeta * w0;
        let wd = w0 * (1.0 - zeta * zeta).sqrt();
        let e0 = f64::from(self.pressure_pa) - f64::from(equilibrium);
        let v0 = f64::from(self.dpdt);
        let decay = (-a * dt).exp();
        let c = (wd * dt).cos();
        // sin(wd dt)/wd tends to dt near critical damping; take the limit.
        let s_over_wd = if wd > 1.0e-9 {
            (wd * dt).sin() / wd
        } else {
            dt
        };
        let e1 = decay * (e0 * c + (v0 + a * e0) * s_over_wd);
        let v1 = decay * (v0 * c - (w0 * w0 * e0 + a * v0) * s_over_wd);
        self.pressure_pa = (f64::from(equilibrium) + e1) as f32;
        self.dpdt = v1 as f32;
    }

    /// Pressure at the start of the last integrated block.
    pub fn pressure_start_pa(&self) -> f32 {
        self.start_pa
    }

    /// Pressure at the end of the last integrated block.
    pub fn pressure_end_pa(&self) -> f32 {
        self.end_pa
    }

    /// Unloaded pressure.
    pub fn nominal_pressure_pa(&self) -> f32 {
        self.config.nominal_pressure_pa
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn settle(b: &mut Bellows, demand: f32, blocks: usize, dt: f64) -> Vec<f32> {
        (0..blocks)
            .map(|_| {
                b.begin_step();
                b.add_demand(demand);
                b.integrate(dt);
                b.pressure_end_pa()
            })
            .collect()
    }

    #[test]
    fn first_order_sags_to_demand_over_feed_and_never_overshoots() {
        let mut b = Bellows::new(BellowsConfig::default());
        let p = settle(&mut b, 40.0, 400, 256.0 / 48000.0);
        assert!((p[p.len() - 1] - (735.0 - 10.0)).abs() < 0.01);
        assert!(p.windows(2).all(|w| w[1] <= w[0] + 1e-4), "monotone");
    }

    #[test]
    fn a_plate_with_mass_drops_past_the_new_pressure_and_comes_back() {
        let cfg = BellowsConfig {
            plate_resonance_hz: 4.5,
            plate_damping: 0.45,
            ..BellowsConfig::default()
        };
        let mut b = Bellows::new(cfg);
        let p = settle(&mut b, 40.0, 400, 256.0 / 48000.0);
        let lowest = p.iter().copied().fold(f32::INFINITY, f32::min);
        assert!(lowest < 725.0 - 1.0, "overshoot: lowest {lowest}");
        assert!(
            (p[p.len() - 1] - 725.0).abs() < 0.05,
            "settles: {}",
            p[p.len() - 1]
        );
    }

    #[test]
    fn the_plate_is_stable_at_any_block_size() {
        let cfg = BellowsConfig {
            plate_resonance_hz: 8.0,
            plate_damping: 0.2,
            ..BellowsConfig::default()
        };
        for frames in [1.0, 64.0, 2048.0, 8192.0] {
            let mut b = Bellows::new(cfg);
            let p = settle(&mut b, 40.0, 2000, frames / 48000.0);
            assert!(
                p.iter().all(|x| x.is_finite() && *x > 600.0 && *x < 760.0),
                "{frames} frames"
            );
        }
    }
}
