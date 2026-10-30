//! What a host sees and automates.

use caecilia_model::Organ;
use caecilia_registration::MASK_CAPACITY;
use manguebyte_params::{BoolParam, FloatParam, FloatRange, Param, ParamSet};

/// Couplers the host can draw.
pub const MAX_COUPLERS: usize = 16;

/// The parameters. Stops and couplers are pools of a fixed size, because a
/// host fixes the parameter list when it loads a plugin; each is named after
/// the stop it draws on the loaded organ, and the rest are marked unused.
pub struct CaeciliaParams {
    /// Output level, dB.
    pub level: FloatParam,
    /// Reverb blend.
    pub reverb: FloatParam,
    /// The swell shoe of every enclosed division, shut 0 to open 1.
    pub swell: FloatParam,
    /// The tremulant.
    pub tremulant: BoolParam,
    /// Its rate.
    pub tremulant_rate: FloatParam,
    /// Its depth, 0 to 1 of the strongest.
    pub tremulant_depth: FloatParam,
    /// Drawstops, by stop id.
    pub stops: Vec<BoolParam>,
    /// Couplers, by coupler id.
    pub couplers: Vec<BoolParam>,
}

/// The parameter id of stop `n`. Stable across versions: a host keys its
/// automation on it.
pub fn stop_param_id(n: usize) -> String {
    format!("stop-{n:02}")
}

/// The parameter id of coupler `n`.
pub fn coupler_param_id(n: usize) -> String {
    format!("coupler-{n:02}")
}

impl CaeciliaParams {
    /// The parameters for `organ`, drawn as it opens.
    pub fn new(organ: &Organ) -> Self {
        let opening = organ.default_opening_registration(organ.primary_manual());
        let stops = (0..MASK_CAPACITY)
            .map(|n| {
                let (name, on) = match organ.stops().get(n) {
                    Some(s) => {
                        let division = organ.division(s.division).map_or("", |d| d.name.as_str());
                        (format!("{division} {}", s.name), opening.contains(&s.id))
                    }
                    None => (format!("Stop {n:02} (unused)"), false),
                };
                BoolParam::new(&stop_param_id(n), &name, on)
            })
            .collect();
        let couplers = (0..MAX_COUPLERS)
            .map(|n| {
                let name = organ
                    .couplers()
                    .get(n)
                    .map_or_else(|| format!("Coupler {n:02} (unused)"), |c| c.name.clone());
                BoolParam::new(&coupler_param_id(n), &name, false)
            })
            .collect();
        Self {
            level: FloatParam::new("level", "Level", FloatRange::linear(-40.0, 6.0), 0.0)
                .with_unit("dB")
                .with_precision(1),
            reverb: FloatParam::new("reverb", "Reverb", FloatRange::linear(0.0, 1.0), 0.25),
            swell: FloatParam::new("swell", "Swell", FloatRange::linear(0.0, 1.0), 1.0),
            tremulant: BoolParam::new("tremulant", "Tremulant", false),
            tremulant_rate: FloatParam::new(
                "tremulant-rate",
                "Tremulant rate",
                FloatRange::linear(3.0, 9.0),
                5.5,
            )
            .with_unit("Hz")
            .with_precision(2),
            tremulant_depth: FloatParam::new(
                "tremulant-depth",
                "Tremulant depth",
                FloatRange::linear(0.0, 1.0),
                0.5,
            ),
            stops,
            couplers,
        }
    }

    /// The drawn stops, bit `n` being stop `n`. Real-time safe.
    pub fn stop_mask(&self) -> u64 {
        self.stops
            .iter()
            .enumerate()
            .filter(|(_, p)| p.value())
            .fold(0, |m, (n, _)| m | 1 << n)
    }

    /// The drawn couplers, bit `n` being coupler `n`. Real-time safe.
    pub fn coupler_mask(&self) -> u32 {
        self.couplers
            .iter()
            .enumerate()
            .filter(|(_, p)| p.value())
            .fold(0, |m, (n, _)| m | 1 << n)
    }
}

impl ParamSet for CaeciliaParams {
    fn visit<'a>(&'a self, visitor: &mut dyn FnMut(&'a dyn Param)) {
        visitor(&self.level);
        visitor(&self.reverb);
        visitor(&self.swell);
        visitor(&self.tremulant);
        visitor(&self.tremulant_rate);
        visitor(&self.tremulant_depth);
        for p in &self.stops {
            visitor(p);
        }
        for p in &self.couplers {
            visitor(p);
        }
    }
}
