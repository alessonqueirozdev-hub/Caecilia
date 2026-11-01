// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The wind supply, as an audio-rate control signal rather than an LFO.
//!
//! A reservoir integrates its pressure once per block under the flow the
//! sounding pipes draw; chests follow it proportionally behind their own
//! regulators and lose a little more to their own trunks; a tremulant shakes a
//! chest's pressure itself, so downstream it is pitch, level and timbre at
//! once. Every voice then reads its chest's pressure sample-accurately from a
//! state that does not change while the block renders.
//!
//! # Real time
//!
//! [`WindModel::configure`] allocates and runs off the audio thread. Everything
//! else -- [`WindModel::step`], demand booking, the tremulant controls and every
//! read -- is allocation-free and lock-free.

mod bellows;
mod chest;
mod config;
mod model;
mod response;
mod snapshot;
mod tremulant;

pub use bellows::Bellows;
pub use chest::Windchest;
pub use config::{
    BellowsConfig, DEFAULT_NOMINAL_PRESSURE_PA, FLOW_PER_VOICE, MAX_TREMULANT_DEPTH_FRACTION,
    TremulantConfig, TremulantWaveform, WindModelConfig, WindchestConfig, config_from_organ,
    flow_per_voice, rank_wind_flow, single_chest_config,
};
pub use model::WindModel;
pub use response::{WindResponseCurve, default_response_for, response_for};
pub use snapshot::{MAX_SNAPSHOT_CHESTS, WindSnapshot};
pub use tremulant::Tremulant;
