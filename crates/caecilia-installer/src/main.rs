//! `caecilia-setup`: Caecilia's installer, and once installed its
//! uninstaller (the same program, copied into the program folder).
//!
//! With no arguments it opens the wizard; `/S` installs without a window;
//! `--uninstall` removes. See `crate::args` for every switch and
//! `docs/INSTALLER.md` for the design.

#![windows_subsystem = "windows"]

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
    eprintln!("The Caecilia installer runs on Windows.");
    caecilia_installer::exit::FAILED
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
    use std::time::SystemTime;

    use caecilia_installer::args::{Args, Mode};
    use caecilia_installer::job::{self, SelfLauncher};
    use caecilia_installer::log::{Log, stamp};
    use caecilia_installer::payload::Payload;
    use caecilia_installer::record::Record;
    use caecilia_installer::run::{self, Found, Host};
    use caecilia_installer::system::System;
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
        if args.mode == Mode::Uninstall && !args.relocated && !args.engine {
            if let Some(code) = relocate(&args) {
                return code;
            }
        }
        let code = if args.engine || args.silent {
            headless(&args)
        } else {
            gui(&args)
        };
        if args.relocated {
            // The copy in the temporary folder goes when Windows restarts,
            // if this is an administrator; else the folder's cleaning does.
            if let Ok(me) = std::env::current_exe() {
                WinSystem.delete_at_restart(&me);
            }
        }
        code
    }

    fn lang(args: &Args) -> Lang {
        args.lang
            .unwrap_or_else(|| Lang::from_langid(win::ui_language()))
    }

    fn product() -> String {
        Payload::parse(PAYLOAD).map_or_else(|_| "Caecilia".into(), |p| p.manifest.product)
    }

    /// The uninstaller cannot remove the folder it runs from: it runs again
    /// from a copy in the temporary folder. `None` when that fails, and it
    /// goes on from where it is.
    fn relocate(args: &Args) -> Option<i32> {
        let me = std::env::current_exe().ok()?;
        let copy = std::env::temp_dir().join(format!(
            "{}-Uninstall-{}.exe",
            product(),
            std::process::id()
        ));
        std::fs::copy(&me, &copy).ok()?;
        let mut line: Vec<String> = std::env::args().skip(1).collect();
        line.push("--relocated".into());
        if args.dir.is_none() {
            line.push("--dir".into());
            line.push(me.parent()?.to_string_lossy().into_owned());
        }
        let mut child = Command::new(&copy).args(&line).spawn().ok()?;
        if args.silent {
            // Whoever runs the quiet uninstall waits for its result.
            Some(child.wait().ok()?.code().unwrap_or(exit::FAILED))
        } else {
            Some(exit::OK)
        }
    }

    fn headless(args: &Args) -> i32 {
        let what = if args.mode == Mode::Uninstall {
            "Uninstall"
        } else {
            "Setup"
        };
        let product = product();
        let log_path = args.log.clone().unwrap_or_else(|| {
            std::env::temp_dir().join(format!("{product}-{what}-{}.log", stamp(SystemTime::now())))
        });
        let mut log = Log::to_file(&log_path).unwrap_or_else(|_| Log::in_memory());
        log.note(format_args!(
            "{product} {what}, {} run, elevated {}, language {}",
            if args.engine { "engine" } else { "silent" },
            win::is_elevated(),
            lang(args).code()
        ));
        let code = match win::places() {
            Err(e) => {
                log.error(format_args!("the system's folders are not known: {e}"));
                exit::FAILED
            }
            Ok(places) => {
                let cancel = job::cancel_file(&log_path);
                let cancelled = || cancel.exists();
                let uninstaller = match args.mode {
                    Mode::Install => std::env::current_exe()
                        .and_then(std::fs::read)
                        .unwrap_or_default(),
                    Mode::Uninstall => Vec::new(),
                };
                let mut sys = WinSystem;
                let mut host = Host {
                    sys: &mut sys,
                    places,
                    elevated: win::is_elevated(),
                    uninstaller,
                    cancelled: &cancelled,
                };
                let code = match args.mode {
                    Mode::Install => run::install(args, PAYLOAD, &mut host, &mut log),
                    Mode::Uninstall => run::uninstall(args, &product, &mut host, &mut log),
                };
                let _ = std::fs::remove_file(&cancel);
                code
            }
        };
        log.line("end", code);
        code
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
        let version = manifest
            .as_ref()
            .map_or(String::new(), |m| m.version.clone());
        let facts = Facts {
            manifest,
            places,
            found,
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
