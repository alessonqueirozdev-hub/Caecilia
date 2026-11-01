//! The work the window starts: the installer run again as an engine, as an
//! administrator for everyone on the PC, followed through its log.
//!
//! The window itself never runs elevated, so that what it opens at the end
//! (the application) runs as the person and not as an administrator.

use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use crate::log::Progress;

/// Why work did not start.
#[derive(Debug)]
pub enum StartError {
    /// The person declined the administrator's prompt.
    Declined,
    /// Something else, said in words.
    Failed(String),
}

/// Work under way in another process.
pub trait Running {
    /// Its exit code, once it has ended.
    fn try_wait(&mut self) -> Option<i32>;
}

impl Running for std::process::Child {
    fn try_wait(&mut self) -> Option<i32> {
        std::process::Child::try_wait(self)
            .ok()
            .flatten()
            .map(|s| s.code().unwrap_or(1))
    }
}

#[cfg(windows)]
impl Running for crate::win::Elevated {
    fn try_wait(&mut self) -> Option<i32> {
        crate::win::Elevated::try_wait(self)
    }
}

/// Starts work; the window's tests use a pretend one.
pub trait Launcher {
    /// Runs the installer with `args`, elevated if `elevate`.
    fn start(&mut self, args: &[String], elevate: bool) -> Result<Box<dyn Running>, StartError>;
}

/// Runs this very executable.
#[derive(Debug, Default)]
pub struct SelfLauncher;

impl Launcher for SelfLauncher {
    fn start(&mut self, args: &[String], elevate: bool) -> Result<Box<dyn Running>, StartError> {
        let exe = std::env::current_exe().map_err(|e| StartError::Failed(e.to_string()))?;
        #[cfg(windows)]
        if elevate && !crate::win::is_elevated() {
            return match crate::win::Elevated::start(&exe, args) {
                Ok(p) => Ok(Box::new(p)),
                Err(crate::win::ElevateError::Declined) => Err(StartError::Declined),
                Err(crate::win::ElevateError::Other(e)) => Err(StartError::Failed(e.to_string())),
            };
        }
        let _ = elevate;
        std::process::Command::new(exe)
            .args(args)
            .spawn()
            .map(|c| Box::new(c) as Box<dyn Running>)
            .map_err(|e| StartError::Failed(e.to_string()))
    }
}

/// Work under way, and its log as far as read.
pub struct Job {
    running: Box<dyn Running>,
    log: PathBuf,
    read: u64,
    text: String,
    /// What the log says so far.
    pub progress: Progress,
    /// The exit code, once ended.
    pub exit: Option<i32>,
}

impl Job {
    /// Follows `running`, which writes `log`.
    pub fn new(running: Box<dyn Running>, log: &Path) -> Job {
        Job {
            running,
            log: log.to_owned(),
            read: 0,
            text: String::new(),
            progress: Progress::default(),
            exit: None,
        }
    }

    /// The log's file.
    pub fn log(&self) -> &Path {
        &self.log
    }

    /// Reads what the log gained, and whether the work ended. Returns
    /// whether anything changed.
    pub fn poll(&mut self) -> bool {
        let mut changed = false;
        if let Ok(mut file) = std::fs::File::open(&self.log) {
            let mut more = String::new();
            if file.seek(SeekFrom::Start(self.read)).is_ok()
                && file.read_to_string(&mut more).is_ok()
            {
                // Only whole lines: the rest is read next time.
                if let Some(end) = more.rfind('\n') {
                    self.text.push_str(&more[..=end]);
                    self.read += end as u64 + 1;
                    self.progress = Progress::read(&self.text);
                    changed = true;
                }
            }
        }
        if self.exit.is_none() {
            if let Some(code) = self.running.try_wait() {
                self.exit = Some(code);
                changed = true;
            }
        }
        changed
    }

    /// Asks the work to stop and undo what it did.
    pub fn cancel(&self) {
        let _ = std::fs::write(cancel_file(&self.log), b"");
    }
}

/// The file whose presence asks the engine writing `log` to stop.
pub fn cancel_file(log: &Path) -> PathBuf {
    let mut name = log.as_os_str().to_owned();
    name.push(".cancel");
    PathBuf::from(name)
}
