//! The console: Caecilia's editor, drawn with `manguebyte-ui`.
//!
//! A French console as the approved mockup has it
//! (`crates/caecilia-tools/examples/console_mockup.rs`): the jambs of
//! drawstops either side, the registration and the wind between them, the
//! manuals with the general pistons in the key slip, the swell and
//! crescendo shoes, the pedalboard and the toe studs. It is laid out from
//! the organ the instance plays, and shows what plays: the stops drawn,
//! the general recalled, every key held, each chest's wind.
//!
//! Everything it shows it reads once a frame from the parameters, the
//! registration controller and the audio thread's [`Telemetry`]; a part is
//! drawn again only when what it shows has changed.

mod live;
mod look;
mod paint;
mod parts;
mod snapshot;

use std::sync::{Arc, Mutex};

use manguebyte_gui::Editor;
use manguebyte_plugin::editor::HostSize;

use crate::controller::RegistrationController;
use crate::params::CaeciliaParams;
use crate::telemetry::Telemetry;

/// The console's size at scale 1, in logical pixels.
pub const EDITOR_SIZE: HostSize = HostSize::new(1600, 980);

/// What the console shares with the instance: handles, never the instance.
#[derive(Clone)]
pub(crate) struct Shared {
    pub(crate) params: Arc<CaeciliaParams>,
    pub(crate) controller: Arc<Mutex<RegistrationController>>,
    pub(crate) telemetry: Arc<Telemetry>,
}

/// The console for an instance, built each time a host opens it.
pub(crate) fn editor(shared: Shared) -> Editor {
    let lit = look::NIGHT;
    Editor::new(EDITOR_SIZE, move || {
        let source = snapshot::Source::new(shared.clone(), lit);
        parts::screen(&source)
    })
    .with_theme(look::theme(&lit))
}
