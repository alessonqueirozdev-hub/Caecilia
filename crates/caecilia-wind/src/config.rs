//! What a wind model is built from.

use caecilia_model::{Calibration, Footage, Organ, RankId, WindchestId};

/// A typical nominal pressure (about 75 mm of water).
pub const DEFAULT_NOMINAL_PRESSURE_PA: f32 = 735.0;

/// Pressure swing, as a fraction of chest nominal, at a tremulant depth of 1.
/// A quarter of chest pressure is a strong tremblant fort.
pub const MAX_TREMULANT_DEPTH_FRACTION: f32 = 0.25;

/// The flow one pipe draws at the reference -- middle C on an 8' rank -- in
/// the model's units. Sag in pascals is `demand * (1/feed + 1/trunk)`.
pub const FLOW_PER_VOICE: f32 = 0.3;

/// The flow one middle-C 8' pipe draws, under a calibration.
///
/// 1.x's figure leaves the wind all but dead: a full chord of the whole organ
/// sags the Grand-Orgue 0.23 %, which nobody hears. 2.0 draws ten times as much,
/// so the same chord sags it a few percent -- a well-winded instrument that is
/// nonetheless heard to breathe, and the reeds dip against the flues.
pub fn flow_per_voice(cal: Calibration) -> f32 {
    match cal {
        Calibration::OneX => FLOW_PER_VOICE,
        Calibration::V2 => 10.0 * FLOW_PER_VOICE,
    }
}

/// What one pipe of a rank draws at middle C relative to an 8': its length in
/// eight-foot units (2 for a 16', 0.25 for a 2'). An unknown length draws as
/// an 8' rather than nothing.
pub fn rank_wind_flow(footage: Footage) -> f32 {
    let feet = footage.feet();
    if feet > 0.0 { (feet / 8.0) as f32 } else { 1.0 }
}

/// The shape of a tremulant's pressure modulation.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum TremulantWaveform {
    /// A smooth undulation.
    #[default]
    Sine,
    /// Linear up and down.
    Triangle,
    /// Fast attack, slow recovery, like a mechanical valve.
    AsymmetricSaw,
}

/// One reservoir.
///
/// First order by default: it relaxes toward an equilibrium that sags in
/// proportion to the flow drawn from it. Give the top plate a resonance and it
/// becomes second order: a chord landing on it drops past the new pressure and
/// back before it settles.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BellowsConfig {
    /// Unloaded pressure.
    pub nominal_pressure_pa: f32,
    /// Volume compliance; the first-order rate is `feed / compliance`.
    pub compliance: f32,
    /// Regulator stiffness, flow per pascal of deficit.
    pub feed_conductance: f32,
    /// Passive leak conductance (unused so far).
    pub leak: f32,
    /// Natural frequency of the plate on its air spring; zero for first order.
    pub plate_resonance_hz: f32,
    /// Damping ratio of the plate, clamped just under 1.
    pub plate_damping: f32,
}

impl Default for BellowsConfig {
    fn default() -> Self {
        Self {
            nominal_pressure_pa: DEFAULT_NOMINAL_PRESSURE_PA,
            compliance: 1.0,
            feed_conductance: 4.0,
            leak: 0.0,
            plate_resonance_hz: 0.0,
            plate_damping: 0.45,
        }
    }
}

/// One windchest: a distribution node behind its own regulator.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WindchestConfig {
    /// Its identity.
    pub id: WindchestId,
    /// The reservoir feeding it.
    pub bellows: u16,
    /// Its tremulant, if it has one.
    pub tremulant: Option<u16>,
    /// The pressure it is voiced at.
    pub nominal_pressure_pa: f32,
    /// Trunk stiffness, flow per pascal of local drop.
    pub trunk_conductance: f32,
}

impl Default for WindchestConfig {
    fn default() -> Self {
        Self {
            id: WindchestId(0),
            bellows: 0,
            tremulant: None,
            nominal_pressure_pa: DEFAULT_NOMINAL_PRESSURE_PA,
            trunk_conductance: 50.0,
        }
    }
}

/// One tremulant.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TremulantConfig {
    /// Rate, typically 4 to 7 Hz.
    pub rate_hz: f32,
    /// Peak swing as a fraction of chest nominal.
    pub depth_fraction: f32,
    /// Shape.
    pub waveform: TremulantWaveform,
    /// Whether it is drawn when the organ opens.
    pub enabled_at_start: bool,
}

impl Default for TremulantConfig {
    fn default() -> Self {
        Self {
            rate_hz: 5.0,
            depth_fraction: 0.06,
            waveform: TremulantWaveform::Sine,
            enabled_at_start: false,
        }
    }
}

/// Everything a [`crate::WindModel`] compiles into runtime state.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WindModelConfig {
    /// The reservoirs.
    pub bellows: Vec<BellowsConfig>,
    /// The chests, addressed by their ids.
    pub chests: Vec<WindchestConfig>,
    /// The tremulants.
    pub tremulants: Vec<TremulantConfig>,
    /// Which chest each rank stands on; ranks not listed stand on the first.
    pub rank_chests: Vec<(RankId, WindchestId)>,
}

/// One reservoir feeding one chest with one (undrawn) tremulant.
pub fn single_chest_config(nominal_pa: f32, trem_rate_hz: f32, trem_depth: f32) -> WindModelConfig {
    WindModelConfig {
        bellows: vec![BellowsConfig {
            nominal_pressure_pa: nominal_pa,
            ..BellowsConfig::default()
        }],
        chests: vec![WindchestConfig {
            tremulant: Some(0),
            nominal_pressure_pa: nominal_pa,
            ..WindchestConfig::default()
        }],
        tremulants: vec![TremulantConfig {
            rate_hz: trem_rate_hz,
            depth_fraction: trem_depth,
            ..TremulantConfig::default()
        }],
        rank_chests: Vec::new(),
    }
}

/// The wind an organ describes: one reservoir at the highest pressure any
/// chest asks for (a regulator can lower pressure, not raise it), each chest
/// behind it at its own pressure, a tremulant for each chest that has one, and
/// every rank routed to the chest it stands on.
pub fn config_from_organ(organ: &Organ) -> WindModelConfig {
    let chests = organ.windchests();
    if chests.is_empty() {
        return single_chest_config(DEFAULT_NOMINAL_PRESSURE_PA, 5.0, 0.06);
    }

    let nominal = chests
        .iter()
        .map(|c| c.nominal_pressure_pa)
        .fold(0.0f32, f32::max);
    // A plate with mass, ringing about two cycles: a full chord is heard to
    // land on the wind rather than merely lower it.
    let mut cfg = WindModelConfig {
        bellows: vec![BellowsConfig {
            nominal_pressure_pa: nominal,
            plate_resonance_hz: 4.5,
            plate_damping: 0.45,
            ..BellowsConfig::default()
        }],
        ..WindModelConfig::default()
    };
    for c in chests {
        let tremulant = c.has_tremulant.then(|| {
            cfg.tremulants.push(TremulantConfig::default());
            (cfg.tremulants.len() - 1) as u16
        });
        cfg.chests.push(WindchestConfig {
            id: c.id,
            bellows: 0,
            tremulant,
            nominal_pressure_pa: c.nominal_pressure_pa,
            ..WindchestConfig::default()
        });
    }
    for c in chests {
        cfg.rank_chests.extend(c.ranks.iter().map(|&r| (r, c.id)));
    }
    cfg
}
