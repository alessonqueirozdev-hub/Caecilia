//! Linux's and macOS's side of [`System`]: which installations there are,
//! kept in a small file for each (Windows keeps them in the registry), the
//! launchers and the commands, who owns what an administrator's run puts in
//! a person's home. Nothing is ever left for a restart: a file in use can be
//! deleted there.
//!
//! All of it builds everywhere, but what only Linux and macOS have, so its
//! tests run on every system.

use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use serde_json::{Value, json};

use crate::engine::write_whole;
use crate::plan::{Flavor, Places, Scope, Shortcut, UninstallEntry};
use crate::system::{Installed, System};
use crate::text::Lang;

/// This person's user number, asked of `id`, which needs no `unsafe` here
/// and exists on both systems.
pub fn uid() -> Option<u32> {
    static UID: OnceLock<Option<u32>> = OnceLock::new();
    *UID.get_or_init(|| {
        let out = Command::new("id").arg("-u").output().ok()?;
        String::from_utf8(out.stdout).ok()?.trim().parse().ok()
    })
}

/// Whether this process runs as an administrator.
pub fn is_root() -> bool {
    uid() == Some(0)
}

/// The system's folders for the person whose home `HOME` names.
pub fn places() -> Result<Places, String> {
    let home = std::env::var_os("HOME")
        .filter(|h| !h.is_empty())
        .ok_or("HOME is not set, so the person's folders are not known")?;
    Ok(Places::for_unix(
        Flavor::host(),
        Path::new(&home),
        &std::env::temp_dir(),
    ))
}

/// The language the environment asks for.
pub fn language() -> Lang {
    ["LC_ALL", "LC_MESSAGES", "LANG"]
        .iter()
        .find_map(|name| std::env::var(name).ok().filter(|v| !v.is_empty()))
        .map_or(Lang::En, |locale| Lang::from_locale(&locale))
}

/// `path` as a desktop entry's `Exec` takes it: quoted, and with what is
/// special inside the quotes escaped.
fn exec_quoted(path: &Path) -> String {
    let mut out = String::from("\"");
    for c in path.to_string_lossy().chars() {
        if matches!(c, '"' | '`' | '$' | '\\') {
            out.push('\\');
        }
        out.push(c);
    }
    out.push('"');
    out
}

/// The desktop entry that starts `s.target`. In a terminal: that is where
/// Caecilia plays while Manguebyte has no windows on Linux.
fn desktop_entry(s: &Shortcut) -> String {
    let name = s
        .link
        .file_stem()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let mut title = name.chars();
    let title = title
        .next()
        .map(|c| c.to_uppercase().collect::<String>() + title.as_str())
        .unwrap_or_default();
    format!(
        "[Desktop Entry]\nType=Application\nName={title}\nComment={}\nExec={}\nIcon={}\nTerminal=true\nCategories=AudioVideo;Audio;Midi;\n",
        s.description,
        exec_quoted(&s.target),
        s.icon.display()
    )
}

/// Whether the process numbered `pid` is alive.
#[cfg(unix)]
fn alive(pid: &str) -> bool {
    // `ps` answers for any user's process, where a signal is refused.
    Command::new("ps")
        .args(["-p", pid])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map_or(true, |s| s.success())
}

#[cfg(not(unix))]
fn alive(_pid: &str) -> bool {
    true
}

/// Linux or macOS, as [`System`] sees it.
#[derive(Debug)]
pub struct PosixSystem {
    places: Places,
    lock_dir: PathBuf,
    lock: Option<PathBuf>,
}

impl PosixSystem {
    /// A system with `places`. Locks are kept in `/tmp`, the same folder
    /// for an administrator's run and a person's.
    pub fn new(places: Places) -> PosixSystem {
        PosixSystem::with_locks_in(places, Path::new("/tmp"))
    }

    /// As [`new`](Self::new), the locks in `lock_dir`.
    pub fn with_locks_in(places: Places, lock_dir: &Path) -> PosixSystem {
        PosixSystem {
            places,
            lock_dir: lock_dir.to_owned(),
            lock: None,
        }
    }

    fn entry_file(&self, scope: Scope, product: &str) -> PathBuf {
        self.places
            .entries_dir(scope)
            .join(format!("{product}.json"))
    }
}

impl Drop for PosixSystem {
    fn drop(&mut self) {
        if let Some(lock) = self.lock.take() {
            let _ = std::fs::remove_file(lock);
        }
    }
}

impl System for PosixSystem {
    fn installed(&self, scope: Scope, product: &str) -> Option<Installed> {
        let text = std::fs::read_to_string(self.entry_file(scope, product)).ok()?;
        let v: Value = serde_json::from_str(&text).ok()?;
        Some(Installed {
            version: v.get("version")?.as_str()?.to_owned(),
            location: PathBuf::from(v.get("location")?.as_str()?),
        })
    }

    fn register(&mut self, scope: Scope, e: &UninstallEntry) -> io::Result<()> {
        let file = self.entry_file(scope, &e.name);
        if let Some(dir) = file.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let text = json!({
            "name": e.name,
            "version": e.version,
            "publisher": e.publisher,
            "location": e.location.to_string_lossy(),
            "uninstaller": e.uninstaller.to_string_lossy(),
            "size_kib": e.size_kib,
        });
        let text = serde_json::to_vec_pretty(&text).map_err(io::Error::other)?;
        write_whole(&file, &text)
    }

    fn unregister(&mut self, scope: Scope, product: &str) -> io::Result<()> {
        let file = self.entry_file(scope, product);
        match std::fs::remove_file(&file) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => return Err(e),
        }
        // Only when no other product's entry is in it.
        if let Some(dir) = file.parent() {
            let _ = std::fs::remove_dir(dir);
        }
        Ok(())
    }

    fn make_shortcut(&mut self, s: &Shortcut) -> io::Result<()> {
        let desktop = s
            .link
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("desktop"));
        if desktop {
            write_whole(&s.link, desktop_entry(s).as_bytes())?;
            // File managers ask before starting an entry not marked so.
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                std::fs::set_permissions(&s.link, std::fs::Permissions::from_mode(0o755))?;
            }
            return Ok(());
        }
        // A command, or on macOS a `.command`, which Terminal opens.
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink(&s.target, &s.link)
        }
        #[cfg(not(unix))]
        {
            Err(io::Error::new(
                io::ErrorKind::Unsupported,
                "links are for Linux and macOS",
            ))
        }
    }

    fn delete_at_restart(&mut self, _path: &Path) -> bool {
        false
    }

    #[cfg(unix)]
    fn give_to_person(&mut self, path: &Path) {
        use std::os::unix::fs::{MetadataExt, chown};
        if !is_root() {
            return;
        }
        // The first folder above it someone other than the administrator
        // owns is the person's home, or in it. There is none in root's.
        let owner = path
            .ancestors()
            .skip(1)
            .filter_map(|a| std::fs::metadata(a).ok())
            .find(|m| m.uid() != 0);
        if let Some(m) = owner {
            let _ = chown(path, Some(m.uid()), Some(m.gid()));
        }
    }

    fn lock(&mut self, product: &str) -> bool {
        if self.lock.is_some() {
            return true;
        }
        // One for each user: installations for one person are that person's,
        // and those for everyone all run as the administrator.
        let name = format!("{product}-setup-{}.lock", uid().unwrap_or(0));
        let file = self.lock_dir.join(name);
        // Twice: a lock left by a run that has ended gives way.
        for _ in 0..2 {
            let made = std::fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&file);
            match made {
                Ok(mut f) => {
                    use std::io::Write;
                    let _ = write!(f, "{}", std::process::id());
                    self.lock = Some(file);
                    return true;
                }
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {
                    let pid = std::fs::read_to_string(&file).unwrap_or_default();
                    let pid = pid.trim();
                    if !pid.is_empty() && alive(pid) {
                        return false;
                    }
                    if std::fs::remove_file(&file).is_err() {
                        return false;
                    }
                }
                Err(_) => return false,
            }
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "caecilia-installer-posix-{name}-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn entry(location: &Path) -> UninstallEntry {
        UninstallEntry {
            name: "Caecilia".into(),
            version: "2.0.0".into(),
            publisher: "Alesson Queiroz".into(),
            location: location.to_owned(),
            uninstaller: location.join("uninstall"),
            icon: location.join("caecilia.ico"),
            size_kib: 12,
        }
    }

    #[test]
    fn an_entry_is_written_read_and_removed() {
        let home = scratch("entry");
        let places = Places::for_unix(Flavor::Linux, &home, &home);
        let dir = places.entries_dir(Scope::User);
        let mut sys = PosixSystem::with_locks_in(places, &home);
        assert_eq!(sys.installed(Scope::User, "Caecilia"), None);
        let location = home.join("Caecilia");
        sys.register(Scope::User, &entry(&location)).unwrap();
        assert_eq!(
            sys.installed(Scope::User, "Caecilia"),
            Some(Installed {
                version: "2.0.0".into(),
                location,
            })
        );
        sys.unregister(Scope::User, "Caecilia").unwrap();
        assert_eq!(sys.installed(Scope::User, "Caecilia"), None);
        assert!(!dir.exists(), "its folder goes when it is the last");
        // Gone already is fine.
        sys.unregister(Scope::User, "Caecilia").unwrap();
        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn a_damaged_entry_is_no_entry() {
        let home = scratch("damaged");
        let places = Places::for_unix(Flavor::Macos, &home, &home);
        let dir = places.entries_dir(Scope::User);
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("Caecilia.json"), b"{ not json").unwrap();
        let sys = PosixSystem::with_locks_in(places, &home);
        assert_eq!(sys.installed(Scope::User, "Caecilia"), None);
        let _ = std::fs::remove_dir_all(&home);
    }

    #[test]
    fn a_desktop_entry_starts_the_program_in_a_terminal() {
        let text = desktop_entry(&Shortcut {
            link: PathBuf::from("caecilia.desktop"),
            target: PathBuf::from("My $Apps/caecilia"),
            icon: PathBuf::from("caecilia.ico"),
            description: "Caecilia, the pipe organ".into(),
        });
        assert!(text.starts_with("[Desktop Entry]\n"));
        assert!(text.contains("\nName=Caecilia\n"));
        assert!(text.contains("\nExec=\"My \\$Apps/caecilia\"\n"));
        assert!(text.contains("\nTerminal=true\n"));
    }

    #[test]
    fn only_one_run_holds_the_lock_and_it_ends_with_the_run() {
        let dir = scratch("lock");
        let places = Places::for_unix(Flavor::Linux, &dir, &dir);
        let mut first = PosixSystem::with_locks_in(places.clone(), &dir);
        assert!(first.lock("Caecilia"));
        assert!(first.lock("Caecilia"), "its own holder may ask again");
        let mut second = PosixSystem::with_locks_in(places.clone(), &dir);
        assert!(!second.lock("Caecilia"));
        drop(first);
        assert!(second.lock("Caecilia"));
        drop(second);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[cfg(unix)]
    #[test]
    fn a_lock_left_by_a_run_that_ended_gives_way() {
        let dir = scratch("stale");
        let places = Places::for_unix(Flavor::Linux, &dir, &dir);
        let name = format!("Caecilia-setup-{}.lock", uid().unwrap_or(0));
        // No process has this number: the largest Linux allows, and more.
        std::fs::write(dir.join(name), b"99999999").unwrap();
        let mut sys = PosixSystem::with_locks_in(places, &dir);
        assert!(sys.lock("Caecilia"));
        drop(sys);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[cfg(unix)]
    #[test]
    fn a_command_is_a_link_to_the_program() {
        let dir = scratch("link");
        let places = Places::for_unix(Flavor::Linux, &dir, &dir);
        let mut sys = PosixSystem::with_locks_in(places, &dir);
        let target = dir.join("caecilia");
        std::fs::write(&target, b"program").unwrap();
        let link = dir.join("command");
        sys.make_shortcut(&Shortcut {
            link: link.clone(),
            target: target.clone(),
            icon: dir.join("caecilia.ico"),
            description: "Caecilia".into(),
        })
        .unwrap();
        assert_eq!(std::fs::read_link(&link).unwrap(), target);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
