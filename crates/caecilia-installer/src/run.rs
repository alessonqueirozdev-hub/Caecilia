//! The work without a window: `/S` from a person or a deployment tool, and
//! the engine the window starts, elevated or not. Each returns an exit
//! code ([`crate::exit`]) and says what it did in the log.

use std::cmp::Ordering;
use std::path::{Path, PathBuf};

use crate::args::Args;
use crate::engine::{Engine, Error};
use crate::exit;
use crate::log::Log;
use crate::payload::Payload;
use crate::plan::{Places, Plan, Scope};
use crate::record::{LOG_COPY, RECORD, Record};
use crate::system::{Installed, System};
use crate::version;

/// What the work runs on.
pub struct Host<'a> {
    /// The system.
    pub sys: &'a mut dyn System,
    /// Its folders.
    pub places: Places,
    /// Whether the process has an administrator's rights.
    pub elevated: bool,
    /// The uninstaller to place: the installer's own executable.
    pub uninstaller: Vec<u8>,
    /// Asked between steps: true stops and undoes everything.
    pub cancelled: &'a dyn Fn() -> bool,
}

/// An installation found, and its record.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Found {
    /// Who it is for.
    pub scope: Scope,
    /// What it made.
    pub record: Record,
}

/// The installation of `product` already here, everyone's first.
pub fn find_installed(sys: &dyn System, product: &str) -> Option<Found> {
    [Scope::Machine, Scope::User].into_iter().find_map(|scope| {
        let installed = sys.installed(scope, product)?;
        let record = Record::load(&installed.location)?;
        Some(Found { scope, record })
    })
}

/// An installation of `product` in *Apps & features* whose record is
/// missing or damaged, so what it placed is not known.
pub fn find_unreadable(sys: &dyn System, product: &str) -> Option<(Scope, Installed)> {
    [Scope::Machine, Scope::User].into_iter().find_map(|scope| {
        let installed = sys.installed(scope, product)?;
        Record::load(&installed.location)
            .is_none()
            .then_some((scope, installed))
    })
}

fn for_whom(scope: Scope) -> &'static str {
    match scope {
        Scope::Machine => "everyone",
        Scope::User => "one person",
    }
}

/// Installs as `args` say from `payload`.
pub fn install(args: &Args, payload: &[u8], host: &mut Host<'_>, log: &mut Log) -> i32 {
    let files = match Payload::parse(payload).and_then(|p| p.unpack()) {
        Ok(files) => files,
        Err(e) => {
            log.error(&e);
            return exit::DAMAGED;
        }
    };
    let product = files.manifest.product.clone();
    if !lock(host, &product, log) {
        return exit::BUSY;
    }
    let found = find_installed(host.sys, &product);
    let unreadable = find_unreadable(host.sys, &product);
    let scope = args
        .scope
        .or(found.as_ref().map(|f| f.scope))
        .or(unreadable.as_ref().map(|u| u.0))
        .unwrap_or(Scope::Machine);
    if let Some((at, installed)) = &unreadable {
        // Installing beside it would leave its files and its entry behind
        // with nothing to remove them; over it, only when asked.
        if !args.force || *at != scope {
            log.error(format_args!(
                "{product} is installed for {} in {}, but its record ({RECORD}) is missing or damaged; \
                 /FORCE installs over it, for {} again",
                for_whom(*at),
                installed.location.display(),
                for_whom(*at)
            ));
            return exit::FAILED;
        }
        log.note(format_args!(
            "installing over the installation in {}, whose record is unreadable",
            installed.location.display()
        ));
    }
    if let Some(f) = &found {
        if f.scope != scope {
            log.error(format_args!(
                "{product} is installed for {} in {}: remove it first",
                for_whom(f.scope),
                f.record.program_dir.display()
            ));
            return exit::FAILED;
        }
        if version::compare(&f.record.version, &files.manifest.version) == Ordering::Greater
            && !args.force
        {
            log.error(format_args!(
                "{product} {} is installed, newer than {}; /FORCE installs over it",
                f.record.version, files.manifest.version
            ));
            return exit::NEWER_INSTALLED;
        }
    }
    if scope == Scope::Machine && !host.elevated {
        log.error("an installation for everyone needs an administrator");
        return exit::NEEDS_ADMIN;
    }
    let mut choices = args.choices(scope);
    // One form for each folder (`\`, no `.` or `..`, no `\` at the end), as
    // the record keeps it and as an update compares it.
    for dir in [&mut choices.program_dir, &mut choices.organs_dir]
        .into_iter()
        .flatten()
    {
        *dir = crate::engine::whole(dir);
    }
    if let Some((_, installed)) = &unreadable {
        if choices.program_dir.is_none() {
            choices.program_dir = Some(installed.location.clone());
        }
    }
    let previous = found.map(|f| f.record);
    if let Some(old) = &previous {
        // An update stays where it was, keeps where the organs went, and
        // keeps what was chosen, unless the command line says otherwise.
        if choices.program_dir.is_none() {
            choices.program_dir = Some(old.program_dir.clone());
        }
        if choices.organs_dir.is_none() {
            choices.organs_dir = old.organs_dir.clone();
        }
        choices.components = args.components.unwrap_or(old.components);
        choices.desktop = args.desktop.unwrap_or(old.desktop);
        choices.start_menu = args.start_menu.unwrap_or(old.start_menu);
    }
    if choices.components.is_empty() {
        log.error("nothing to install");
        return exit::USAGE;
    }
    let plan = Plan::new(
        &files.manifest,
        &choices,
        &host.places,
        host.uninstaller.len() as u64,
    );
    if scope == Scope::Machine && !host.places.protects(&plan.program_dir) {
        log.error(format_args!(
            "{} is not inside {}: an installation for everyone goes where only an administrator can change it",
            plan.program_dir.display(),
            host.places.program_files.display()
        ));
        return exit::USAGE;
    }
    let mut engine = Engine {
        sys: host.sys,
        log,
        cancelled: host.cancelled,
    };
    match engine.install(&plan, &files, &host.uninstaller, previous.as_ref()) {
        Ok(outcome) => {
            copy_log(log, &plan.program_dir);
            if outcome.restart {
                exit::RESTART
            } else {
                exit::OK
            }
        }
        Err(Error::Cancelled) => exit::CANCELLED,
        Err(Error::Payload(_)) => exit::DAMAGED,
        Err(_) => exit::FAILED,
    }
}

/// Removes the installation of `product` in `args.dir`, or the one found.
pub fn uninstall(args: &Args, product: &str, host: &mut Host<'_>, log: &mut Log) -> i32 {
    if !lock(host, product, log) {
        return exit::BUSY;
    }
    let record = match &args.dir {
        Some(dir) => Record::load(dir),
        None => find_installed(host.sys, product).map(|f| f.record),
    };
    let Some(record) = record else {
        log.error(format_args!("no installation of {product} was found"));
        return exit::FAILED;
    };
    if record.scope == Scope::Machine && !host.elevated {
        log.error("removing an installation for everyone needs an administrator");
        return exit::NEEDS_ADMIN;
    }
    // The data of the person who installed it, whoever removes it: a
    // deployment's removal runs as another account.
    let purge: Option<PathBuf> = args.purge_dir.clone().or_else(|| {
        args.purge.then(|| {
            record
                .data_dir
                .clone()
                .unwrap_or_else(|| host.places.user_data_dir(&record.product))
        })
    });
    let mut engine = Engine {
        sys: host.sys,
        log,
        cancelled: host.cancelled,
    };
    match engine.uninstall(&record, purge.as_deref()) {
        Ok(outcome) if outcome.restart => exit::RESTART,
        Ok(_) => exit::OK,
        Err(_) => exit::FAILED,
    }
}

/// Takes the lock of `product`'s installers, or says another run has it.
fn lock(host: &mut Host<'_>, product: &str, log: &mut Log) -> bool {
    let got = host.sys.lock(product);
    if !got {
        log.error(format_args!(
            "another run of the {product} installer is at work: try again when it ends"
        ));
    }
    got
}

/// Copies the log into the program folder, for later.
fn copy_log(log: &Log, dir: &Path) {
    if let Some(path) = log.path() {
        let _ = std::fs::copy(path, dir.join(LOG_COPY));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::payload::{PayloadBuilder, Root};
    use crate::plan::Components;
    use crate::system::MemorySystem;

    fn payload(version: &str) -> Vec<u8> {
        let mut b = PayloadBuilder::new("Caecilia", version, "Alesson Queiroz");
        b.add("core", Root::Program, "caecilia.ico", b"icon");
        b.add("app", Root::Program, "caecilia.exe", version.as_bytes());
        b.add("clap", Root::Clap, "Caecilia.clap", b"clap");
        b.build().unwrap()
    }

    fn parse(s: &[&str]) -> Args {
        Args::parse(s.iter().map(|s| s.to_string())).unwrap()
    }

    #[test]
    fn a_second_run_waits_its_turn() {
        let mut sys = MemorySystem {
            locked_elsewhere: true,
            ..MemorySystem::default()
        };
        let no = || false;
        let base = std::env::temp_dir().join("caecilia-installer-run-busy");
        let mut host = Host {
            sys: &mut sys,
            places: Places::under(&base),
            elevated: true,
            uninstaller: b"MZ".to_vec(),
            cancelled: &no,
        };
        let mut log = Log::in_memory();
        let v1 = payload("2.0.0");
        assert_eq!(
            install(&parse(&["/S", "/CURRENTUSER"]), &v1, &mut host, &mut log),
            exit::BUSY
        );
        assert_eq!(
            uninstall(
                &parse(&["--uninstall", "/S"]),
                "Caecilia",
                &mut host,
                &mut log
            ),
            exit::BUSY
        );
        assert!(!base.exists(), "nothing was touched");
    }

    #[test]
    fn an_installation_without_its_record_is_not_installed_beside() {
        let base = std::env::temp_dir().join(format!(
            "caecilia-installer-run-unreadable-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&base);
        let mut sys = MemorySystem::default();
        let no = || false;
        let mut host = Host {
            sys: &mut sys,
            places: Places::under(&base),
            elevated: true,
            uninstaller: b"MZ".to_vec(),
            cancelled: &no,
        };
        let mut log = Log::in_memory();
        let v1 = payload("2.0.0");
        let user = ["/S", "/CURRENTUSER"];
        assert_eq!(install(&parse(&user), &v1, &mut host, &mut log), exit::OK);
        let dir = host.places.program_dir(Scope::User, "Caecilia");
        std::fs::remove_file(dir.join(RECORD)).unwrap();

        // Not beside it, for everyone or again for one person.
        for args in [
            &["/S", "/ALLUSERS"][..],
            &["/S"],
            &["/S", "/FORCE", "/ALLUSERS"],
        ] {
            assert_eq!(
                install(&parse(args), &v1, &mut host, &mut log),
                exit::FAILED,
                "{args:?}"
            );
        }
        // Over it, when asked.
        assert_eq!(
            install(&parse(&["/S", "/FORCE"]), &v1, &mut host, &mut log),
            exit::OK
        );
        assert_eq!(Record::load(&dir).unwrap().program_dir, dir);
        let _ = std::fs::remove_dir_all(&base);
    }

    #[test]
    fn a_purge_takes_the_data_of_whoever_installed() {
        let base = std::env::temp_dir().join(format!(
            "caecilia-installer-run-purge-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&base);
        let mut sys = MemorySystem::default();
        let no = || false;
        let mut host = Host {
            sys: &mut sys,
            places: Places::under(&base),
            elevated: true,
            uninstaller: b"MZ".to_vec(),
            cancelled: &no,
        };
        let mut log = Log::in_memory();
        // Installed by a person, whose organs folder the window passes.
        let theirs = base.join("Person").join("Caecilia");
        let organs = format!("--organs-dir={}", theirs.join("Organs").display());
        assert_eq!(
            install(
                &parse(&["/S", "/CURRENTUSER", &organs]),
                &payload("2.0.0"),
                &mut host,
                &mut log
            ),
            exit::OK
        );
        std::fs::create_dir_all(&theirs).unwrap();
        std::fs::write(theirs.join("session.txt"), b"theirs").unwrap();
        // Removed by another account, which has data of its own.
        let mine = host.places.user_data_dir("Caecilia");
        std::fs::create_dir_all(&mine).unwrap();
        std::fs::write(mine.join("session.txt"), b"mine").unwrap();
        assert_eq!(
            uninstall(
                &parse(&["--uninstall", "/S", "/PURGE"]),
                "Caecilia",
                &mut host,
                &mut log
            ),
            exit::OK
        );
        assert!(!theirs.exists());
        assert!(mine.join("session.txt").is_file());
        let _ = std::fs::remove_dir_all(&base);
    }

    #[test]
    fn silent_runs_say_what_happened_by_their_exit_code() {
        let base =
            std::env::temp_dir().join(format!("caecilia-installer-run-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&base);
        let mut sys = MemorySystem::default();
        let no = || false;
        let mut host = Host {
            sys: &mut sys,
            places: Places::under(&base),
            elevated: false,
            uninstaller: b"MZ".to_vec(),
            cancelled: &no,
        };
        let mut log = Log::in_memory();
        let v1 = payload("2.0.0");

        assert_eq!(
            install(&parse(&["/S"]), &v1, &mut host, &mut log),
            exit::NEEDS_ADMIN
        );
        assert_eq!(
            install(&parse(&["/S"]), &v1[..v1.len() - 1], &mut host, &mut log),
            exit::DAMAGED
        );
        // For everyone, never in a folder a person could change.
        host.elevated = true;
        let outside = base.join("Apps").join("Caecilia");
        assert_eq!(
            install(
                &parse(&["/S", "/ALLUSERS", &format!("/DIR={}", outside.display())]),
                &v1,
                &mut host,
                &mut log
            ),
            exit::USAGE
        );
        assert!(!outside.exists());
        host.elevated = false;
        assert_eq!(
            install(
                &parse(&["/S", "/CURRENTUSER", "/COMPONENTS=app", "/NODESKTOP"]),
                &v1,
                &mut host,
                &mut log
            ),
            exit::OK
        );
        let dir = host.places.program_dir(Scope::User, "Caecilia");
        assert_eq!(std::fs::read(dir.join("caecilia.exe")).unwrap(), b"2.0.0");
        // Not for everyone while it is installed for one person.
        host.elevated = true;
        assert_eq!(
            install(&parse(&["/S", "/ALLUSERS"]), &v1, &mut host, &mut log),
            exit::FAILED
        );
        // An update needs no scope: it follows the installation.
        assert_eq!(
            install(&parse(&["/S"]), &payload("2.0.1"), &mut host, &mut log),
            exit::OK
        );
        assert_eq!(std::fs::read(dir.join("caecilia.exe")).unwrap(), b"2.0.1");
        // And keeps what was chosen.
        let r = Record::load(&dir).unwrap();
        assert_eq!(r.components, Components::parse("app").unwrap());
        assert_eq!((r.desktop, r.start_menu), (false, true));
        assert!(!host.places.plugin_dir(Scope::User, "CLAP").exists());
        // Unless the command line says otherwise.
        assert_eq!(
            install(
                &parse(&["/S", "/COMPONENTS=app,clap", "/NOSTARTMENU"]),
                &payload("2.0.1"),
                &mut host,
                &mut log
            ),
            exit::OK
        );
        let r = Record::load(&dir).unwrap();
        assert!(r.components.clap);
        assert_eq!((r.desktop, r.start_menu), (false, false));
        // A repair naming the same folder another way loses nothing.
        let other_form = format!("/DIR={}/./", dir.display()).replace('\\', "/");
        assert_eq!(
            install(
                &parse(&["/S", &other_form]),
                &payload("2.0.1"),
                &mut host,
                &mut log
            ),
            exit::OK
        );
        let r = Record::load(&dir).unwrap();
        assert_eq!(r.program_dir, dir);
        for f in &r.files {
            assert!(f.path.is_file(), "{}", f.path.display());
        }
        // Going back needs /FORCE.
        assert_eq!(
            install(&parse(&["/S"]), &v1, &mut host, &mut log),
            exit::NEWER_INSTALLED
        );
        assert_eq!(
            install(&parse(&["/S", "/FORCE"]), &v1, &mut host, &mut log),
            exit::OK
        );

        assert_eq!(
            uninstall(
                &parse(&["--uninstall", "/S"]),
                "Caecilia",
                &mut host,
                &mut log
            ),
            exit::OK
        );
        assert!(!dir.exists());
        assert_eq!(
            uninstall(
                &parse(&["--uninstall", "/S"]),
                "Caecilia",
                &mut host,
                &mut log
            ),
            exit::FAILED
        );
        let _ = std::fs::remove_dir_all(&base);
    }
}
