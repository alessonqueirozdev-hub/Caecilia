// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The organ: what it is, before anything sounds.
//!
//! Identities, exact-rational footages, the compiled [`Organ`] (stops, ranks,
//! chests, divisions, couplers), the procedural spectra per tonal family, and
//! each rank's calibrated [`RankVoicing`]. Nothing here runs on the audio
//! thread; everything it builds is read-only afterwards.

mod builder;
mod calibration;
mod demo;
mod file;
mod footage;
mod ids;
mod organ;
pub mod recipes;
mod spectrum;
mod voicing;

pub use builder::{OrganBuilder, StopSpec};
pub use calibration::Calibration;
pub use demo::{caecilia_organ, demo_organ};
pub use file::{
    Diagnostic, FORMAT_VERSION, MAX_COUPLERS, MAX_STOPS, OrganDocument, OrganFileError, read_organ,
    read_organ_file, read_spectrum, write_organ,
};
pub use footage::Footage;
pub use ids::{
    ChorusRole, CouplerId, DivisionId, MidiNote, PipeId, PitchClass, RankId, StopId, TonalFamily,
    WindchestId,
};
pub use organ::{
    Coupler, Division, DivisionKind, Manual, Organ, Rank, RankCharacter, RecipeOverride, Stop,
    Windchest,
};
pub use spectrum::{
    FormantPeak, MAX_FORMANTS, MIXTURE_SERIES, PartialTrack, SpectralModel, SpeechProfile,
    mixture_series_index,
};
pub use voicing::{
    MAX_PARTIALS, RankVoicing, build_rank_voicing, spectral_model_for_stop, speech_profile_for,
};
