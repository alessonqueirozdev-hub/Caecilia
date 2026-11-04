// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The console: Caecilia's editor, drawn with `manguebyte-ui`.
//!
//! A French console as the approved mockup has it
//! (`crates/caecilia-tools/examples/console_mockup.rs`): the jambs of
//! drawstops either side, the registration and the wind between them, the
//! manuals with the general pistons in the key slip, the swell and
//! crescendo shoes, the pedalboard and the toe studs. It is laid out from
//! the organ the instance plays, and shows what plays: the stops drawn,
//! the general recalled, every key held, each chest's wind. It is zoomed to
//! fit any window of about its shape; in one much smaller, as at half a screen
//! beside a score, [`compact`] takes its place at its real size.
//!
//! Everything it shows it reads once a frame from the parameters, the
//! registration controller and the audio thread's [`Telemetry`]; a part is
//! drawn again only when what it shows has changed.

mod actions;
mod bench;
mod compact;
mod controls;
mod demos;
mod library;
mod live;
mod look;
mod paint;
mod parts;
mod registration;
mod snapshot;
mod sound;

use std::sync::{Arc, Mutex};

use manguebyte_gui::Editor;
use manguebyte_plugin::editor::{HostSize, Resize};
use manguebyte_ui::responsive;

use manguebyte_core::spsc::Producer;

use crate::UiNote;
use crate::controller::RegistrationController;
use crate::params::CaeciliaParams;
use crate::telemetry::Telemetry;

/// The MIDI channel each part of a demonstration plays on, on `organ`.
pub(crate) fn demo_channels(organ: &caecilia_model::Organ) -> [u8; 3] {
    demos::parts(organ).map(|p| p.1)
}

/// The console's size at scale 1, in logical pixels.
pub const EDITOR_SIZE: HostSize = HostSize::new(1600, 980);

/// The window can get this small, narrower and shorter than half of a small
/// screen, and this large.
const SMALLEST_WINDOW: HostSize = HostSize::new(520, 520);
const LARGEST_WINDOW: HostSize = HostSize::new(7680, 4320);

/// How small the full console could be zoomed. The compact one takes its place
/// well before that, at [`compact::BELOW`].
const SMALLEST_ZOOM: f32 = 0.5;

/// What the console shares with the instance: handles, never the instance.
#[derive(Clone)]
pub(crate) struct Shared {
    pub(crate) params: Arc<CaeciliaParams>,
    pub(crate) controller: Arc<Mutex<RegistrationController>>,
    pub(crate) telemetry: Arc<Telemetry>,
    /// Where the keys played on the console go.
    pub(crate) notes: Arc<Mutex<Producer<UiNote>>>,
}

/// The console for an instance, built each time a host opens it.
pub(crate) fn editor(shared: Shared) -> Editor {
    Editor::new(EDITOR_SIZE, move || {
        let source = snapshot::Source::new(shared.clone());
        responsive((
            parts::screen(&source),
            compact::screen(&source, false),
            compact::screen(&source, true),
        ))
        .below(compact::BELOW)
        .below(compact::SHORT_BELOW)
    })
    .with_theme(look::theme(&look::NIGHT))
    .with_resize(Resize::Free {
        min: SMALLEST_WINDOW,
        max: LARGEST_WINDOW,
    })
    .with_zoom_to_fit(SMALLEST_ZOOM)
    .with_real_size_below(compact::BELOW)
}
