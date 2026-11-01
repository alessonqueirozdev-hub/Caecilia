//! The log: every run writes one, line by line, as it goes.
//!
//! ```text
//! 14:02:07 note Caecilia 2.0.0 setup, Windows 10.0.26200, scope machine
//! 14:02:07 units 11
//! 14:02:07 step app
//! 14:02:08 file C:\Program Files\Caecilia\caecilia.exe 4402176 9f2c…
//! 14:02:09 end 0
//! ```
//!
//! The lines are for people, and also how the window follows an
//! installation it started in another process (an elevated one, say): it
//! reads the log as it grows ([`Progress::read`]).

use std::fmt::Display;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// A log being written.
#[derive(Debug, Default)]
pub struct Log {
    file: Option<File>,
    path: Option<PathBuf>,
    /// Every line, for the window when the work runs in its process.
    lines: Vec<String>,
}

impl Log {
    /// A log appended to `path`.
    pub fn to_file(path: &Path) -> std::io::Result<Log> {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let file = OpenOptions::new().create(true).append(true).open(path)?;
        Ok(Log {
            file: Some(file),
            path: Some(path.to_owned()),
            lines: Vec::new(),
        })
    }

    /// A log kept in memory only.
    pub fn in_memory() -> Log {
        Log::default()
    }

    /// The file it is written to.
    pub fn path(&self) -> Option<&Path> {
        self.path.as_deref()
    }

    /// The lines so far.
    pub fn lines(&self) -> &[String] {
        &self.lines
    }

    /// Writes a line: `kind` and what follows it.
    pub fn line(&mut self, kind: &str, text: impl Display) {
        let (h, m, s) = clock(SystemTime::now());
        let line = format!("{h:02}:{m:02}:{s:02} {kind} {text}");
        if let Some(file) = &mut self.file {
            // A log that cannot be written must not stop an installation.
            let _ = writeln!(file, "{line}");
            let _ = file.flush();
        }
        self.lines.push(line);
    }

    /// Something worth knowing.
    pub fn note(&mut self, text: impl Display) {
        self.line("note", text);
    }

    /// Something that went wrong.
    pub fn error(&mut self, text: impl Display) {
        self.line("error", text);
    }
}

/// The time of day, UTC, of `t`.
fn clock(t: SystemTime) -> (u64, u64, u64) {
    let secs = t.duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs());
    let day = secs % 86_400;
    (day / 3600, day / 60 % 60, day % 60)
}

/// The date and time, UTC, of `t` as `yyyymmdd-hhmmss`, for file names.
pub fn stamp(t: SystemTime) -> String {
    let secs = t.duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs());
    let (y, mo, d) = civil(secs / 86_400);
    let (h, m, s) = clock(t);
    format!("{y:04}{mo:02}{d:02}-{h:02}{m:02}{s:02}")
}

/// The date of a day counted from 1970-01-01 (Howard Hinnant's
/// `civil_from_days`).
fn civil(days: u64) -> (i64, u32, u32) {
    let z = days as i64 + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let y = yoe + era * 400 + i64::from(m <= 2);
    (y, m, d)
}

/// How far an installation is, as its log tells.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Progress {
    /// The units of work there are.
    pub units: u32,
    /// The units done.
    pub done: u32,
    /// The step under way: `app`, `vst3`, `shortcuts`, ….
    pub step: Option<String>,
    /// The last path written or removed.
    pub path: Option<String>,
    /// The last error.
    pub error: Option<String>,
    /// The exit code, once it has ended.
    pub end: Option<i32>,
    /// The lines worth showing: steps, files, notes and errors, without
    /// their times.
    pub shown: Vec<String>,
}

impl Progress {
    /// What `log` tells so far.
    pub fn read(log: &str) -> Progress {
        let mut p = Progress::default();
        for line in log.lines() {
            // "hh:mm:ss kind rest"
            let Some((_, rest)) = line.split_once(' ') else {
                continue;
            };
            let (kind, text) = rest.split_once(' ').unwrap_or((rest, ""));
            match kind {
                "units" => p.units = text.trim().parse().unwrap_or(0),
                "step" => p.step = Some(text.trim().to_owned()),
                "file" | "link" | "removed" | "registered" | "unregistered" | "kept" => {
                    p.done += 1;
                    if kind != "registered" && kind != "unregistered" {
                        // The path, before its size and hash.
                        let path = match kind {
                            "file" => text.rsplitn(3, ' ').nth(2).unwrap_or(text),
                            _ => text,
                        };
                        p.path = Some(path.to_owned());
                    }
                }
                "error" => p.error = Some(text.to_owned()),
                "end" => p.end = text.trim().parse().ok(),
                _ => {}
            }
            if kind != "units" && kind != "end" {
                p.shown.push(format!("{kind} {text}"));
            }
        }
        p
    }

    /// The share done, from 0 to 1.
    pub fn fraction(&self) -> f32 {
        if self.end == Some(0) {
            1.0
        } else if self.units == 0 {
            0.0
        } else {
            (self.done as f32 / self.units as f32).min(1.0)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dates_are_counted_right() {
        assert_eq!(civil(0), (1970, 1, 1));
        assert_eq!(civil(19_723), (2024, 1, 1));
        assert_eq!(civil(19_782), (2024, 2, 29));
        let t = UNIX_EPOCH + std::time::Duration::from_secs(1_793_570_400);
        assert_eq!(stamp(t), "20261101-220000");
    }

    #[test]
    fn progress_is_read_back_from_the_log() {
        let mut log = Log::in_memory();
        log.line("units", 4);
        log.line("step", "app");
        log.line("file", "C:\\Program Files\\Caecilia\\caecilia.exe 12 abcd");
        log.line("link", "C:\\Users\\Public\\Desktop\\Caecilia.lnk");
        let p = Progress::read(&log.lines().join("\n"));
        assert_eq!((p.units, p.done), (4, 2));
        assert_eq!(p.step.as_deref(), Some("app"));
        assert_eq!(
            p.path.as_deref(),
            Some("C:\\Users\\Public\\Desktop\\Caecilia.lnk")
        );
        assert_eq!(p.fraction(), 0.5);
        assert_eq!(p.end, None);
        log.line("registered", "machine");
        log.line("end", 0);
        let p = Progress::read(&log.lines().join("\n"));
        assert_eq!((p.done, p.end, p.fraction()), (3, Some(0), 1.0));
        let p = Progress::read(&log.lines()[..3].join("\n"));
        assert_eq!(
            p.path.as_deref(),
            Some("C:\\Program Files\\Caecilia\\caecilia.exe")
        );
    }
}
