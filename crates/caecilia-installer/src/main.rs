//! `caecilia-setup`: Caecilia's installer, and once installed its
//! uninstaller (the same program, copied into the program folder).
//!
//! With no arguments it opens the wizard (on Linux and macOS, where there
//! are no windows yet, it asks the same questions in the terminal); `/S` or
//! `--silent` installs without asking; `--uninstall` removes. See
//! `crate::args` for every switch and `docs/INSTALLER.md` for the design.

#![cfg_attr(windows, windows_subsystem = "windows")]

/// The files it installs, embedded by `build.rs` (empty in a development
/// build, which then says it is damaged).
static PAYLOAD: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/payload.bin"));

/// The licence the person accepts.
const LICENCE: &str = include_str!("../../../LICENSE");

fn main() {
    std::process::exit(run());
}

#[cfg(not(windows))]
fn run() -> i32 {
    unix_main::run()
}

#[cfg(not(windows))]
mod unix_main {
    use std::io::IsTerminal;
    use std::path::Path;

    use caecilia_installer::args::{Args, Mode};
    use caecilia_installer::job::SelfLauncher;
    use caecilia_installer::payload::Payload;
    use caecilia_installer::plan::{Flavor, Places};
    use caecilia_installer::posix::{self, PosixSystem};
    use caecilia_installer::record::Record;
    use caecilia_installer::run::{self, Found};
    use caecilia_installer::text::Lang;
    use caecilia_installer::ui::Facts;
    use caecilia_installer::{exit, terminal};

    use super::{LICENCE, PAYLOAD};

    pub fn run() -> i32 {
        let mut args = match Args::parse(std::env::args().skip(1)) {
            Ok(args) => args,
            Err(e) => {
                eprintln!("{e}");
                return exit::USAGE;
            }
        };
        run::removes_its_own_folder(&mut args);
        let lang = args.lang.unwrap_or_else(posix::language);
        let places = posix::places();
        if args.engine || args.silent {
            // With no home the work stops at once, saying so; the system
            // needs folders all the same.
            let mut sys = PosixSystem::new(places.clone().unwrap_or_else(|_| {
                Places::for_unix(Flavor::host(), Path::new("/"), &std::env::temp_dir())
            }));
            return run::headless(&args, PAYLOAD, places, posix::is_root(), lang, &mut sys);
        }
        let t = lang.text();
        if !std::io::stdin().is_terminal() {
            eprintln!("{}", t.term_no_terminal);
            return exit::USAGE;
        }
        let places = match places {
            Ok(places) => places,
            Err(e) => {
                eprintln!("{e}");
                return exit::FAILED;
            }
        };
        ask(&args, places, lang)
    }

    fn ask(args: &Args, places: Places, lang: Lang) -> i32 {
        let manifest = Payload::parse(PAYLOAD)
            .map(|p| p.manifest)
            .map_err(|e| e.to_string());
        let product = manifest
            .as_ref()
            .map_or("Caecilia".to_owned(), |m| m.product.clone());
        let sys = PosixSystem::new(places.clone());
        let uninstall = args.mode == Mode::Uninstall;
        let found = match (&args.dir, uninstall) {
            (Some(dir), true) => Record::load(dir).map(|record| Found {
                scope: record.scope,
                record,
            }),
            _ => run::find_installed(&sys, &product),
        };
        let unreadable = found
            .is_none()
            .then(|| run::find_unreadable(&sys, &product))
            .flatten()
            .map(|(scope, installed)| (scope, installed.location));
        let facts = Facts {
            manifest,
            places,
            found,
            unreadable,
            uninstall,
            version_1: false,
            elevated: posix::is_root(),
            licence: LICENCE.replace("\r\n", "\n"),
            self_size: std::env::current_exe()
                .and_then(std::fs::metadata)
                .map_or(0, |m| m.len()),
            log_dir: std::env::temp_dir(),
            lang,
        };
        terminal::run(
            &facts,
            &mut SelfLauncher,
            std::io::stdin().lock(),
            std::io::stdout().lock(),
        )
    }
}

#[cfg(windows)]
fn run() -> i32 {
    windows_main::run()
}

#[cfg(windows)]
mod windows_main {
    use std::cell::{Cell, RefCell};
    use std::path::Path;
    use std::process::Command;
    use std::rc::Rc;

    use caecilia_installer::args::{Args, Mode};
    use caecilia_installer::job::SelfLauncher;
    use caecilia_installer::payload::Payload;
    use caecilia_installer::record::Record;
    use caecilia_installer::run::{self, Found};
    use caecilia_installer::text::{Lang, fill};
    use caecilia_installer::ui::{self, Env, Facts, Wizard};
    use caecilia_installer::win::{self, WinSystem};
    use caecilia_installer::{engine, exit};
    use manguebyte_window::App;

    use super::{LICENCE, PAYLOAD};

    pub fn run() -> i32 {
        let args = match Args::parse(std::env::args().skip(1)) {
            Ok(args) => args,
            Err(e) => {
                // Asked for silence, it shows nothing: the exit code says it.
                let silent = std::env::args().skip(1).any(|a| {
                    let a = a.trim_start_matches(['/', '-']).to_ascii_lowercase();
                    a == "s" || a == "silent" || a == "quiet" || a == "engine"
                });
                if !silent {
                    win::message("Caecilia", &e);
                }
                return exit::USAGE;
            }
        };
        let mut args = args;
        run::removes_its_own_folder(&mut args);
        if args.engine || args.silent {
            run::headless(
                &args,
                PAYLOAD,
                win::places().map_err(|e| e.to_string()),
                win::is_elevated(),
                lang(&args),
                &mut WinSystem,
            )
        } else {
            gui(&args)
        }
    }

    fn lang(args: &Args) -> Lang {
        args.lang
            .unwrap_or_else(|| Lang::from_langid(win::ui_language()))
    }

    fn gui(args: &Args) -> i32 {
        let lang = lang(args);
        let t = lang.text();
        let places = match win::places() {
            Ok(p) => p,
            Err(e) => {
                win::message("Caecilia", &e.to_string());
                return exit::FAILED;
            }
        };
        let manifest = Payload::parse(PAYLOAD)
            .map(|p| p.manifest)
            .map_err(|e| e.to_string());
        let product = manifest
            .as_ref()
            .map_or("Caecilia".to_owned(), |m| m.product.clone());
        let uninstall = args.mode == Mode::Uninstall;
        let found = match (&args.dir, uninstall) {
            (Some(dir), true) => Record::load(dir).map(|record| Found {
                scope: record.scope,
                record,
            }),
            _ => run::find_installed(&WinSystem, &product),
        };
        let unreadable = found
            .is_none()
            .then(|| run::find_unreadable(&WinSystem, &product))
            .flatten()
            .map(|(scope, installed)| (scope, installed.location));
        let version = manifest
            .as_ref()
            .map_or(String::new(), |m| m.version.clone());
        let facts = Facts {
            manifest,
            places,
            found,
            unreadable,
            uninstall,
            version_1: win::has_version_1(),
            elevated: win::is_elevated(),
            licence: LICENCE.replace("\r\n", "\n"),
            self_size: std::env::current_exe()
                .and_then(std::fs::metadata)
                .map_or(0, |m| m.len()),
            log_dir: std::env::temp_dir(),
            lang,
        };
        let app = match App::new() {
            Ok(app) => app,
            Err(e) => {
                win::message("Caecilia", &e.to_string());
                return exit::FAILED;
            }
        };
        let dialogs = Rc::new(RefCell::new(None));
        let busy = Rc::new(Cell::new(false));
        let quit = app.clone();
        let env = Env {
            launcher: Box::new(SelfLauncher),
            dialogs: Rc::clone(&dialogs),
            quit: Box::new(move || quit.quit()),
            open: Box::new(open),
            in_use: Box::new(engine::in_use),
            busy: Rc::clone(&busy),
        };
        let title = if uninstall {
            t.title_remove.to_owned()
        } else {
            fill(t.title, &[&version])
        };
        let window = match ui::open(&app, &title, Wizard::new(facts, env), dialogs, busy) {
            Ok(window) => window,
            Err(e) => {
                win::message("Caecilia", &e.to_string());
                return exit::FAILED;
            }
        };
        let _ = app.run();
        drop(window);
        exit::OK
    }

    /// Opens a program, or a file in the program Windows opens it with.
    fn open(path: &Path) {
        let is_exe = path
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("exe"));
        let _ = if is_exe {
            Command::new(path)
                .current_dir(path.parent().unwrap_or(Path::new(".")))
                .spawn()
        } else {
            Command::new("explorer.exe").arg(path).spawn()
        };
    }
}
