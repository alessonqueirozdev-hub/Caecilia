//! Carrying out a plan, as one transaction, and undoing an installation
//! from its record.
//!
//! Each file is written next to its place as `name.caecilia-new`, flushed,
//! read back and hashed, then renamed into place; a file already there is
//! first renamed aside as `name.caecilia-old`. Renaming works on an
//! application that is running and on a plugin a DAW has loaded, where
//! overwriting would not. If anything fails, or the person cancels, every
//! step is undone in reverse and the old files go back. Once everything is
//! in place the old files are deleted; one still in use is deleted when
//! Windows next starts, or by the next run.

use std::fmt;
use std::fs::{self, File};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use crate::log::Log;
use crate::payload::{Digest, PayloadError, Unpacked};
use crate::plan::{Plan, Scope};
use crate::record::{LOG_COPY, Leftover, RECORD, Record, RecordedFile};
use crate::system::System;

/// What stopped an installation.
#[derive(Debug)]
pub enum Error {
    /// The person cancelled; everything was undone.
    Cancelled,
    /// The payload is damaged.
    Payload(PayloadError),
    /// A file could not be written, read, moved or removed.
    Io {
        /// What was being done.
        doing: &'static str,
        /// To what.
        path: PathBuf,
        /// Why it failed.
        source: io::Error,
    },
    /// A file was written but does not read back as it should.
    Verify(PathBuf),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Cancelled => f.write_str("cancelled"),
            Error::Payload(e) => e.fmt(f),
            Error::Io {
                doing,
                path,
                source,
            } => {
                write!(f, "could not {doing} {}: {source}", path.display())
            }
            Error::Verify(path) => write!(f, "{} does not read back as written", path.display()),
        }
    }
}

impl std::error::Error for Error {}

fn io_err<'a>(doing: &'static str, path: &'a Path) -> impl FnOnce(io::Error) -> Error + 'a {
    move |source| Error::Io {
        doing,
        path: path.to_owned(),
        source,
    }
}

/// How an installation or a removal ended.
#[derive(Debug)]
pub struct Outcome {
    /// What is installed now (after a removal, what was).
    pub record: Record,
    /// Files left because the person changed them after they were placed.
    pub kept: Vec<PathBuf>,
    /// Whether a file in use is deleted only when Windows next starts.
    pub restart: bool,
}

/// `path` with `suffix` added to its name.
fn beside(path: &Path, suffix: &str) -> PathBuf {
    let mut name = path.file_name().unwrap_or_default().to_os_string();
    name.push(suffix);
    path.with_file_name(name)
}

/// The SHA-256 of the file at `path`, or `None` when it is not there.
fn hash_of(path: &Path) -> io::Result<Option<Digest>> {
    match File::open(path) {
        Ok(file) => Digest::of_reader(file).map(Some),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e),
    }
}

/// `path` in one form: absolute, `/` as `\`, `.` and `..` resolved, no
/// separator at the end.
pub fn whole(path: &Path) -> PathBuf {
    std::path::absolute(path)
        .unwrap_or_else(|_| path.to_owned())
        .components()
        .collect()
}

/// The parts of `path` in one form, without regard to case, as Windows
/// compares them.
fn parts(path: &Path) -> Vec<String> {
    whole(path)
        .components()
        .map(|c| c.as_os_str().to_string_lossy().to_lowercase())
        .collect()
}

/// Paths compared as Windows does.
fn same_path(a: &Path, b: &Path) -> bool {
    parts(a) == parts(b)
}

/// Whether the file at `path` is held open by a running program, as the
/// application is while it plays.
pub fn in_use(path: &Path) -> bool {
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        const ERROR_SHARING_VIOLATION: i32 = 32;
        match fs::OpenOptions::new().write(true).share_mode(0).open(path) {
            Ok(_) => false,
            Err(e) => e.raw_os_error() == Some(ERROR_SHARING_VIOLATION),
        }
    }
    #[cfg(not(windows))]
    {
        let _ = path;
        false
    }
}

/// The steps made so far, to undo or to finish.
#[derive(Default)]
struct Transaction {
    /// Folders made, parents first.
    dirs: Vec<PathBuf>,
    /// Files and shortcuts put in place, each with the old one it moved
    /// aside.
    placed: Vec<(PathBuf, Option<PathBuf>)>,
}

impl Transaction {
    /// Makes `dir` and any parents missing, noting each one made.
    fn make_dir(&mut self, dir: &Path) -> Result<(), Error> {
        let mut missing = Vec::new();
        let mut at = Some(dir);
        while let Some(d) = at {
            if d.as_os_str().is_empty() || d.is_dir() {
                break;
            }
            missing.push(d.to_owned());
            at = d.parent();
        }
        for d in missing.into_iter().rev() {
            match fs::create_dir(&d) {
                Ok(()) => self.dirs.push(d),
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists && d.is_dir() => {}
                Err(e) => return Err(io_err("make the folder", &d)(e)),
            }
        }
        Ok(())
    }

    /// Moves whatever is at `dest` aside, so something new can go there.
    fn move_aside(&mut self, dest: &Path) -> Result<Option<PathBuf>, Error> {
        match fs::symlink_metadata(dest) {
            Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(io_err("look at", dest)(e)),
            Ok(meta) if meta.is_dir() => Err(Error::Io {
                doing: "replace",
                path: dest.to_owned(),
                source: io::Error::other("a folder is in the way"),
            }),
            Ok(_) => {
                let mut old = beside(dest, ".caecilia-old");
                let mut n = 1;
                // One left by an earlier run that could not delete it.
                while old.exists() && fs::remove_file(&old).is_err() {
                    n += 1;
                    old = beside(dest, &format!(".caecilia-old{n}"));
                }
                fs::rename(dest, &old).map_err(io_err("move aside", dest))?;
                Ok(Some(old))
            }
        }
    }

    /// Writes `bytes` at `dest`, which must then hash to `sha256`.
    fn place(&mut self, dest: &Path, bytes: &[u8], sha256: Digest) -> Result<(), Error> {
        if let Some(dir) = dest.parent() {
            self.make_dir(dir)?;
        }
        let new = beside(dest, ".caecilia-new");
        let write = || -> io::Result<()> {
            let mut file = File::create(&new)?;
            file.write_all(bytes)?;
            file.sync_all()
        };
        if let Err(e) = write() {
            let _ = fs::remove_file(&new);
            return Err(io_err("write", dest)(e));
        }
        if hash_of(&new).ok().flatten() != Some(sha256) {
            let _ = fs::remove_file(&new);
            return Err(Error::Verify(dest.to_owned()));
        }
        let old = match self.move_aside(dest) {
            Ok(old) => old,
            Err(e) => {
                let _ = fs::remove_file(&new);
                return Err(e);
            }
        };
        if let Err(e) = fs::rename(&new, dest) {
            let _ = fs::remove_file(&new);
            if let Some(old) = &old {
                let _ = fs::rename(old, dest);
            }
            return Err(io_err("put in place", dest)(e));
        }
        self.placed.push((dest.to_owned(), old));
        Ok(())
    }

    /// Undoes every step, last first.
    fn undo(self, log: &mut Log) {
        for (dest, old) in self.placed.into_iter().rev() {
            if let Err(e) = fs::remove_file(&dest) {
                log.error(format_args!("could not take back {}: {e}", dest.display()));
            }
            if let Some(old) = old {
                if let Err(e) = fs::rename(&old, &dest) {
                    log.error(format_args!("could not put back {}: {e}", dest.display()));
                }
            }
            log.line("undone", dest.display());
        }
        for dir in self.dirs.into_iter().rev() {
            let _ = fs::remove_dir(&dir);
        }
    }

    /// Deletes the old files moved aside. Returns those still in use.
    fn finish(self, sys: &mut dyn System, log: &mut Log) -> (Vec<Leftover>, bool) {
        let mut leftovers = Vec::new();
        let mut restart = false;
        for old in self.placed.into_iter().filter_map(|(_, old)| old) {
            let (left, at_restart) = delete_or_defer(&old, sys, log);
            // Named by the installer: its own whatever it holds.
            leftovers.extend(left.map(|path| Leftover { path, sha256: None }));
            restart |= at_restart;
        }
        (leftovers, restart)
    }
}

/// Deletes `path`; if it is in use, has it deleted when Windows next
/// starts, or returns it for the next run.
fn delete_or_defer(path: &Path, sys: &mut dyn System, log: &mut Log) -> (Option<PathBuf>, bool) {
    match fs::remove_file(path) {
        Ok(()) => (None, false),
        Err(e) if e.kind() == io::ErrorKind::NotFound => (None, false),
        Err(_) if sys.delete_at_restart(path) => {
            log.note(format_args!(
                "{} is in use: deleted when Windows restarts",
                path.display()
            ));
            (None, true)
        }
        Err(e) => {
            let by = if path.starts_with(std::env::temp_dir()) {
                "left to the temporary folder's cleaning"
            } else {
                "deleted by the next run"
            };
            log.note(format_args!("{} is in use ({e}): {by}", path.display()));
            (Some(path.to_owned()), false)
        }
    }
}

/// Whether `path` is `dir` or inside it, as Windows compares paths.
fn inside(path: &Path, dir: &Path) -> bool {
    parts(path).starts_with(&parts(dir))
}

/// Whether the leftover `l` is still what the installer left.
fn is_ours(l: &Leftover) -> bool {
    match l.sha256 {
        None => true,
        Some(d) => matches!(hash_of(&l.path), Ok(Some(h)) if h == d),
    }
}

/// Deletes `path` when nothing is to take its place. In use (the
/// uninstaller itself, a plugin a DAW has loaded), it is moved out, so its
/// folder can go, then deleted when it can be. Returns what is left where
/// it was, and whether a restart deletes something.
fn remove_for_good(path: &Path, sys: &mut dyn System, log: &mut Log) -> (Option<PathBuf>, bool) {
    match fs::remove_file(path) {
        Ok(()) => (None, false),
        Err(e) if e.kind() == io::ErrorKind::NotFound => (None, false),
        Err(_) => match move_out(path) {
            Some(aside) => {
                let (left, at_restart) = delete_or_defer(&aside, sys, log);
                // In the temporary folder it is out of the installation;
                // its cleaning takes it.
                let temp = std::env::temp_dir();
                (left.filter(|p| !p.starts_with(&temp)), at_restart)
            }
            // Not even that: deleted at restart if it can be, else kept.
            None if sys.delete_at_restart(path) => {
                log.note(format_args!(
                    "{} is in use: deleted when Windows restarts",
                    path.display()
                ));
                (None, true)
            }
            None => (Some(path.to_owned()), false),
        },
    }
}

/// Moves a file in use out of the way: into the temporary folder, which a
/// running program's file can be renamed into on the same disk, else
/// beside itself. Returns where it went.
fn move_out(path: &Path) -> Option<PathBuf> {
    let name = path.file_name()?.to_string_lossy().into_owned();
    let temp = std::env::temp_dir().join(format!("{name}.{}.caecilia-old", std::process::id()));
    [temp, beside(path, ".caecilia-old")]
        .into_iter()
        .find(|to| fs::rename(path, to).is_ok())
}

/// What dropping an earlier version's files left.
#[derive(Default)]
struct Dropped {
    /// Changed since they were placed, so kept.
    kept: Vec<PathBuf>,
    /// In use, so left for the next run.
    leftovers: Vec<Leftover>,
    /// Some are deleted when Windows restarts.
    restart: bool,
}

/// Installs and removes, writing what it does to a log.
pub struct Engine<'a> {
    /// The system.
    pub sys: &'a mut dyn System,
    /// The log.
    pub log: &'a mut Log,
    /// Asked between steps: true stops and undoes everything.
    pub cancelled: &'a dyn Fn() -> bool,
}

impl Engine<'_> {
    fn check_cancel(&self) -> Result<(), Error> {
        if (self.cancelled)() {
            Err(Error::Cancelled)
        } else {
            Ok(())
        }
    }

    /// Carries out `plan` with the files of `payload`; `uninstaller` is the
    /// installer's own executable, copied into the program folder.
    /// `previous` is what an earlier version installed, if any: what it
    /// placed and this plan does not is removed once everything is in
    /// place.
    pub fn install(
        &mut self,
        plan: &Plan,
        payload: &Unpacked,
        uninstaller: &[u8],
        previous: Option<&Record>,
    ) -> Result<Outcome, Error> {
        self.log.note(format_args!(
            "installing {} {} for {} in {}",
            plan.product,
            plan.version,
            if plan.scope == Scope::Machine {
                "everyone"
            } else {
                "this person"
            },
            plan.program_dir.display()
        ));
        self.log
            .note(format_args!("components {}", plan.components.list()));
        if let Some(old) = previous {
            self.log
                .note(format_args!("replacing version {}", old.version));
            self.clear_leftovers(&old.leftovers);
        }
        // Files, the uninstaller, shortcuts, the record and the entry.
        let units = plan.files.len() + 1 + plan.shortcuts.len() + 1;
        self.log.line("units", units);
        let mut tx = Transaction::default();
        match self.lay(plan, payload, uninstaller, previous, &mut tx) {
            Ok(mut record) => {
                let (mut leftovers, mut restart) = tx.finish(self.sys, self.log);
                let mut kept = Vec::new();
                if let Some(old) = previous {
                    let dropped = self.drop_old(old, &record);
                    kept = dropped.kept;
                    leftovers.extend(dropped.leftovers);
                    restart |= dropped.restart;
                }
                if !leftovers.is_empty() {
                    record.leftovers.extend(leftovers);
                    // The record already in place gains them: a failure here
                    // only means the next run does not know of them.
                    let _ = fs::write(plan.program_dir.join(RECORD), record.to_json());
                }
                self.log.note(format_args!(
                    "{} {} is installed",
                    plan.product, plan.version
                ));
                Ok(Outcome {
                    record,
                    kept,
                    restart,
                })
            }
            Err(e) => {
                self.log.error(&e);
                self.log.note("undoing everything done so far");
                tx.undo(self.log);
                Err(e)
            }
        }
    }

    fn lay(
        &mut self,
        plan: &Plan,
        payload: &Unpacked,
        uninstaller: &[u8],
        previous: Option<&Record>,
        tx: &mut Transaction,
    ) -> Result<Record, Error> {
        // Old files an earlier run left that are still there, before this
        // run moves anything aside under the same names. One this plan
        // places again is no longer left over: the new file takes its name.
        let surviving: Vec<Leftover> = previous
            .map(|r| {
                r.leftovers
                    .iter()
                    .filter(|l| l.path.exists() && is_ours(l))
                    .filter(|l| !plan.files.iter().any(|f| same_path(&f.dest, &l.path)))
                    .cloned()
                    .collect()
            })
            .unwrap_or_default();
        tx.make_dir(&plan.program_dir)?;
        let mut files = Vec::new();
        let mut step = "";
        for f in &plan.files {
            self.check_cancel()?;
            if f.entry.component != step {
                step = &f.entry.component;
                self.log.line("step", step);
            }
            tx.place(&f.dest, payload.bytes(&f.entry), f.entry.sha256)?;
            self.log.line(
                "file",
                format_args!(
                    "{} {} {}",
                    f.dest.display(),
                    f.entry.size,
                    f.entry.sha256.hex()
                ),
            );
            files.push(RecordedFile {
                path: f.dest.clone(),
                size: f.entry.size,
                sha256: f.entry.sha256,
            });
        }
        self.check_cancel()?;
        self.log.line("step", "uninstaller");
        let sha256 = Digest::of(uninstaller);
        tx.place(&plan.uninstaller, uninstaller, sha256)?;
        self.log.line(
            "file",
            format_args!(
                "{} {} {}",
                plan.uninstaller.display(),
                uninstaller.len(),
                sha256.hex()
            ),
        );
        files.push(RecordedFile {
            path: plan.uninstaller.clone(),
            size: uninstaller.len() as u64,
            sha256,
        });

        if !plan.shortcuts.is_empty() {
            self.log.line("step", "shortcuts");
        }
        let mut shortcuts = Vec::new();
        for s in &plan.shortcuts {
            self.check_cancel()?;
            if let Some(dir) = s.link.parent() {
                tx.make_dir(dir)?;
            }
            let old = tx.move_aside(&s.link)?;
            if let Err(e) = self.sys.make_shortcut(s) {
                if let Some(old) = &old {
                    let _ = fs::rename(old, &s.link);
                }
                return Err(io_err("make the shortcut", &s.link)(e));
            }
            tx.placed.push((s.link.clone(), old));
            self.log.line("link", s.link.display());
            shortcuts.push(s.link.clone());
        }

        self.check_cancel()?;
        self.log.line("step", "register");
        let mut dirs: Vec<PathBuf> = previous.map(|r| r.dirs.clone()).unwrap_or_default();
        for d in &tx.dirs {
            if !dirs.iter().any(|known| same_path(known, d)) {
                dirs.push(d.clone());
            }
        }
        let organs_dir = plan
            .files
            .iter()
            .find(|f| f.entry.root == crate::payload::Root::Organs)
            .and_then(|f| f.dest.parent().map(Path::to_owned))
            .or_else(|| previous.and_then(|r| r.organs_dir.clone()));
        let record = Record {
            product: plan.product.clone(),
            version: plan.version.clone(),
            scope: plan.scope,
            components: plan.components,
            program_dir: plan.program_dir.clone(),
            organs_dir,
            data_dir: Some(plan.data_dir.clone()),
            files,
            dirs,
            shortcuts,
            leftovers: surviving,
            desktop: plan.desktop,
            start_menu: plan.start_menu,
        };
        let json = record.to_json();
        tx.place(
            &plan.program_dir.join(RECORD),
            json.as_bytes(),
            Digest::of(json.as_bytes()),
        )?;
        self.sys.register(plan.scope, &plan.entry).map_err(io_err(
            "register in Apps & features",
            &plan.entry.uninstaller,
        ))?;
        self.log.line("registered", plan.scope.name());
        Ok(record)
    }

    /// Removes what `old` placed that `new` did not, unless it was changed.
    fn drop_old(&mut self, old: &Record, new: &Record) -> Dropped {
        let mut dropped = Dropped::default();
        for f in old.files.iter().rev() {
            if new.files.iter().any(|n| same_path(&n.path, &f.path)) {
                continue;
            }
            match hash_of(&f.path) {
                Ok(Some(h)) if h == f.sha256 => {
                    let (left, at_restart) = delete_or_defer(&f.path, self.sys, self.log);
                    dropped.restart |= at_restart;
                    match left {
                        None => self.log.line("removed", f.path.display()),
                        // In use: the new record keeps it, for the next run.
                        Some(path) => dropped.leftovers.push(Leftover {
                            path,
                            sha256: Some(f.sha256),
                        }),
                    }
                }
                Ok(None) => {}
                _ => {
                    self.log.line("kept", f.path.display());
                    dropped.kept.push(f.path.clone());
                }
            }
        }
        for link in &old.shortcuts {
            if !new.shortcuts.iter().any(|n| same_path(n, link)) && fs::remove_file(link).is_ok() {
                self.log.line("removed", link.display());
            }
        }
        // Moved to another folder: the old one's record and log go too, or
        // it would never be empty.
        if !same_path(&old.program_dir, &new.program_dir) {
            for name in [RECORD, LOG_COPY] {
                let _ = fs::remove_file(old.program_dir.join(name));
            }
        }
        // Folders only the old version needed, now empty.
        for dir in old.dirs.iter().rev() {
            let _ = fs::remove_dir(dir);
        }
        dropped
    }

    /// Whether a leftover is still what the installer left: a file left
    /// where it was that someone has since replaced or changed is theirs.
    fn still_ours(&mut self, l: &Leftover) -> bool {
        let ours = is_ours(l);
        if !ours {
            self.log.line(
                "kept",
                format_args!("{} (changed since it was installed)", l.path.display()),
            );
        }
        ours
    }

    fn clear_leftovers(&mut self, leftovers: &[Leftover]) {
        for l in leftovers {
            let path = &l.path;
            if !self.still_ours(l) {
                continue;
            }
            if fs::remove_file(path).is_ok() {
                self.log.note(format_args!(
                    "deleted {}, left by the last run",
                    path.display()
                ));
            }
        }
    }

    /// Removes what `record` says was installed. A file changed since it
    /// was placed is kept. `purge` is the person's own data folder, removed
    /// with everything in it when given.
    pub fn uninstall(&mut self, record: &Record, purge: Option<&Path>) -> Result<Outcome, Error> {
        self.log.note(format_args!(
            "removing {} {} from {}",
            record.product,
            record.version,
            record.program_dir.display()
        ));
        self.log
            .line("units", record.files.len() + record.shortcuts.len() + 1);
        let mut kept = Vec::new();
        let mut restart = false;
        // What earlier runs left: the record goes with this removal, so one
        // still in use is moved out or deleted at restart, not forgotten.
        for l in record.leftovers.iter().filter(|l| l.path.exists()) {
            let path = &l.path;
            if !self.still_ours(l) {
                kept.push(path.clone());
                continue;
            }
            let (left, at_restart) = remove_for_good(path, self.sys, self.log);
            restart |= at_restart;
            match left {
                None => self.log.note(format_args!(
                    "deleted {}, left by the last run",
                    path.display()
                )),
                Some(p) => {
                    self.log
                        .line("kept", format_args!("{} (in use)", p.display()));
                    kept.push(p);
                }
            }
        }
        self.log.line("step", "files");
        for f in record.files.iter().rev() {
            match hash_of(&f.path) {
                Ok(None) => self.log.line(
                    "removed",
                    format_args!("{} (already gone)", f.path.display()),
                ),
                Ok(Some(h)) if h == f.sha256 => {
                    let (left, at_restart) = remove_for_good(&f.path, self.sys, self.log);
                    restart |= at_restart;
                    match left {
                        None => self.log.line("removed", f.path.display()),
                        Some(p) => {
                            self.log
                                .line("kept", format_args!("{} (in use)", p.display()));
                            kept.push(p);
                        }
                    }
                }
                _ => {
                    self.log.line(
                        "kept",
                        format_args!("{} (changed since it was installed)", f.path.display()),
                    );
                    kept.push(f.path.clone());
                }
            }
        }
        self.log.line("step", "shortcuts");
        for link in &record.shortcuts {
            match fs::remove_file(link) {
                Ok(()) => self.log.line("removed", link.display()),
                Err(e) if e.kind() == io::ErrorKind::NotFound => {
                    self.log.line("removed", link.display())
                }
                Err(e) => {
                    self.log
                        .error(format_args!("could not remove {}: {e}", link.display()));
                    kept.push(link.clone());
                }
            }
        }
        self.log.line("step", "register");
        if let Err(e) = self.sys.unregister(record.scope, &record.product) {
            // The record stays, and the entry: running the installer again
            // finds both and its Remove tries again.
            let e = io_err("remove the entry in Apps & features", &record.program_dir)(e);
            self.log.error(&e);
            return Err(e);
        }
        self.log.line("unregistered", record.scope.name());
        for name in [RECORD, LOG_COPY] {
            let _ = fs::remove_file(record.program_dir.join(name));
        }
        // No process can remove the folder it works in: one started inside
        // a folder going now (a prompt opened there) steps out first.
        let going = |cwd: &Path| {
            record
                .dirs
                .iter()
                .map(PathBuf::as_path)
                .chain(purge)
                .any(|d| inside(cwd, d))
        };
        if std::env::current_dir().is_ok_and(|cwd| going(&cwd)) {
            let _ = std::env::set_current_dir(std::env::temp_dir());
        }
        // Only a folder of the product's own name: a record could say
        // anything, and this removes everything in it.
        let purge = purge.filter(|dir| {
            let own = dir
                .file_name()
                .is_some_and(|n| n.eq_ignore_ascii_case(&record.product));
            if !own {
                self.log.error(format_args!(
                    "not purging {}: not a {} folder",
                    dir.display(),
                    record.product
                ));
            }
            own
        });
        if let Some(dir) = purge {
            self.log.line("step", "purge");
            match fs::remove_dir_all(dir) {
                Ok(()) => self.log.line("removed", dir.display()),
                Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                Err(e) => self
                    .log
                    .error(format_args!("could not remove {}: {e}", dir.display())),
            }
        }
        for dir in record.dirs.iter().rev() {
            if fs::remove_dir(dir).is_ok() {
                self.log
                    .note(format_args!("removed the folder {}", dir.display()));
            }
        }
        self.log.note(format_args!("{} is removed", record.product));
        Ok(Outcome {
            record: record.clone(),
            kept,
            restart,
        })
    }
}
