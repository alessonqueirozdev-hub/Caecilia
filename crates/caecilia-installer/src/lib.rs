//! Caecilia's Windows installer, in Rust: the standalone application, the
//! VST3 and CLAP plugins and the demonstration organ, with an uninstaller
//! and an entry in *Apps & features*. See `docs/INSTALLER.md`.
//!
//! - [`payload`]: the files, carried inside the installer and checked by
//!   hash;
//! - [`plan`]: the person's choices and the system's folders turned into
//!   what to make, where;
//! - [`engine`]: carrying out a plan as one transaction, and removing an
//!   installation from its [`record`];
//! - [`system`]: the registry and shortcuts, behind a trait so tests never
//!   touch the real ones;
//! - [`args`], [`log`], [`text`]: the command line, the log, the words;
//! - [`ui`]: the wizard, in the console's materials.
//!
//! The engine knows nothing of Caecilia beyond what the payload says; once
//! it has shipped Caecilia it can move into Manguebyte for every plugin.

pub mod args;
pub mod engine;
pub mod job;
pub mod log;
pub mod payload;
pub mod plan;
pub mod record;
pub mod run;
pub mod system;
pub mod text;
pub mod ui;
pub mod version;
#[cfg(windows)]
pub mod win;

/// The exit codes, as deployment tools expect them.
pub mod exit {
    /// Done.
    pub const OK: i32 = 0;
    /// Failed; the log says why.
    pub const FAILED: i32 = 1;
    /// The person cancelled, or declined the administrator's prompt.
    pub const CANCELLED: i32 = 2;
    /// The installer is damaged.
    pub const DAMAGED: i32 = 3;
    /// Bad arguments.
    pub const USAGE: i32 = 87;
    /// An installation for everyone needs an administrator
    /// (`ERROR_ELEVATION_REQUIRED`).
    pub const NEEDS_ADMIN: i32 = 740;
    /// Another run of the installer is at work
    /// (`ERROR_INSTALL_ALREADY_RUNNING`).
    pub const BUSY: i32 = 1618;
    /// A newer version is installed (`ERROR_PRODUCT_VERSION`).
    pub const NEWER_INSTALLED: i32 = 1638;
    /// Done; a file in use goes when Windows restarts
    /// (`ERROR_SUCCESS_REBOOT_REQUIRED`).
    pub const RESTART: i32 = 3010;
}
