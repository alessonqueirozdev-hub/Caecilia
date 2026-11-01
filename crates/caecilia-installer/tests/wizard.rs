//! The wizard with no window: every page, by day and by night, in every
//! language, driven the way a person and assistive technology drive it.
//! The work it starts is pretend, and writes its log as the engine does.
//!
//! `CAECILIA_INSTALLER_SHOTS=<folder>` saves each page as a PNG there.

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};
use std::rc::Rc;

use caecilia_installer::job::{Launcher, Running, StartError};
use caecilia_installer::payload::{Payload, PayloadBuilder, Root};
use caecilia_installer::plan::{Components, Places, Scope};
use caecilia_installer::record::Record;
use caecilia_installer::run::Found;
use caecilia_installer::text::Lang;
use caecilia_installer::ui::{Env, Facts, SIZE, Wizard};
use manguebyte_ui::Key;
use manguebyte_ui::access::{Action, Role};
use manguebyte_ui::testing::Harness;

/// What the pretend work was asked, and how it answers.
#[derive(Default)]
struct Asked {
    starts: Vec<(Vec<String>, bool)>,
    decline: bool,
    exit: i32,
    /// The work goes on until told.
    hold: bool,
}

struct FakeLauncher(Rc<RefCell<Asked>>);

struct Finished(Option<i32>);

impl Running for Finished {
    fn try_wait(&mut self) -> Option<i32> {
        self.0
    }
}

impl Launcher for FakeLauncher {
    fn start(&mut self, args: &[String], elevate: bool) -> Result<Box<dyn Running>, StartError> {
        let mut asked = self.0.borrow_mut();
        asked.starts.push((args.to_vec(), elevate));
        if asked.decline {
            return Err(StartError::Declined);
        }
        // The log the engine would write.
        let log = args
            .iter()
            .position(|a| a == "--log")
            .map(|i| PathBuf::from(&args[i + 1]))
            .unwrap();
        let text = "10:00:00 units 2\n10:00:00 step app\n10:00:01 file C:\\Program Files\\Caecilia\\caecilia.exe 3 ab\n10:00:01 kept C:\\x\\mine.organ.json\n10:00:02 registered machine\n";
        std::fs::write(&log, text).unwrap();
        Ok(Box::new(Finished((!asked.hold).then_some(asked.exit))))
    }
}

fn manifest() -> caecilia_installer::payload::Manifest {
    let mut b = PayloadBuilder::new("Caecilia", "2.0.1", "Alesson Queiroz");
    b.add("core", Root::Program, "caecilia.ico", b"icon");
    b.add("app", Root::Program, "caecilia.exe", &[1; 4_000_000]);
    b.add(
        "vst3",
        Root::Vst3,
        "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3",
        &[2; 4_000_000],
    );
    b.add("clap", Root::Clap, "Caecilia.clap", &[2; 4_000_000]);
    b.add("organ", Root::Organs, "caecilia-demo.organ.json", b"{}");
    Payload::parse(&b.build().unwrap()).unwrap().manifest
}

struct Rig {
    ui: Harness,
    asked: Rc<RefCell<Asked>>,
    quit: Rc<Cell<bool>>,
    opened: Rc<RefCell<Vec<PathBuf>>>,
    in_use: Rc<Cell<bool>>,
    /// Where the logs go; removed with the rig.
    dir: PathBuf,
}

impl Drop for Rig {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

fn rig(name: &str, found: Option<Found>, uninstall: bool, lang: Lang) -> Rig {
    rig_with(name, found, None, uninstall, lang)
}

fn rig_with(
    name: &str,
    found: Option<Found>,
    unreadable: Option<(Scope, PathBuf)>,
    uninstall: bool,
    lang: Lang,
) -> Rig {
    let dir = std::env::temp_dir().join(format!("caecilia-wizard-{name}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let asked = Rc::new(RefCell::new(Asked::default()));
    let quit = Rc::new(Cell::new(false));
    let opened = Rc::new(RefCell::new(Vec::new()));
    let in_use = Rc::new(Cell::new(false));
    let facts = Facts {
        manifest: Ok(manifest()),
        places: Places::under(Path::new("C:\\")),
        found,
        unreadable,
        uninstall,
        version_1: false,
        elevated: false,
        licence: "Apache License\nVersion 2.0, January 2004\n\n".repeat(40),
        self_size: 5_000_000,
        log_dir: dir.clone(),
        lang,
    };
    let (q, o, u) = (Rc::clone(&quit), Rc::clone(&opened), Rc::clone(&in_use));
    let env = Env {
        launcher: Box::new(FakeLauncher(Rc::clone(&asked))),
        dialogs: Rc::new(RefCell::new(None)),
        quit: Box::new(move || q.set(true)),
        open: Box::new(move |p| o.borrow_mut().push(p.to_owned())),
        in_use: Box::new(move |_| u.get()),
        busy: Rc::new(Cell::new(false)),
    };
    let mut ui = Harness::new(Wizard::new(facts, env), SIZE, 1.0);
    ui.frame();
    Rig {
        ui,
        asked,
        quit,
        opened,
        in_use,
        dir,
    }
}

impl Rig {
    fn press(&mut self, role: Role, name: &str) {
        let found = self.ui.get(role, name);
        assert!(
            self.ui.act(&found, Action::Press),
            "{name} did not take the press"
        );
        self.ui.frame();
    }

    fn page_title(&self) -> String {
        self.ui
            .get(Role::Dialog, "Caecilia 2.0.1 Setup")
            .value_text()
            .unwrap_or_default()
            .to_owned()
    }

    fn shot(&mut self, name: &str) {
        self.ui.frame();
        if let Some(dir) = std::env::var_os("CAECILIA_INSTALLER_SHOTS") {
            let png = self.ui.render_fresh().encode_png().unwrap();
            std::fs::write(PathBuf::from(dir).join(format!("{name}.png")), png).unwrap();
        }
    }
}

fn found_machine(version: &str) -> Found {
    let record = Record {
        product: "Caecilia".into(),
        version: version.into(),
        scope: Scope::Machine,
        components: Components::parse("app,vst3,organ").unwrap(),
        program_dir: "C:\\Program Files\\Caecilia".into(),
        organs_dir: Some("C:\\AppData\\Caecilia\\Organs".into()),
        // Of whoever installed it, not of the person removing it.
        data_dir: Some("C:\\Users\\owner\\AppData\\Caecilia".into()),
        files: vec![],
        dirs: vec![],
        shortcuts: vec![],
        leftovers: vec![],
        desktop: true,
        start_menu: false,
    };
    Found {
        scope: Scope::Machine,
        record,
    }
}

#[test]
fn a_first_installation_goes_from_welcome_to_done() {
    let mut r = rig("first", None, false, Lang::En);
    assert_eq!(r.page_title(), "Caecilia, the pipe organ");
    r.shot("1-welcome");
    r.press(Role::Button, "Next");
    assert_eq!(r.page_title(), "Licence");
    // Install waits for the licence to be accepted.
    let install = r.ui.get(Role::Button, "Install");
    assert!(install.node.state.disabled);
    r.press(Role::Button, "Install");
    assert!(r.asked.borrow().starts.is_empty());
    r.shot("2-licence-not-accepted");
    r.press(
        Role::Switch,
        "I have read and accept the terms of the licence",
    );
    r.shot("2-licence");
    r.press(Role::Button, "Options…");
    assert_eq!(r.page_title(), "Options");
    r.press(Role::Switch, "CLAP plugin");
    assert_eq!(
        r.ui.get(Role::Switch, "CLAP plugin").node.state.checked,
        Some(false)
    );
    r.shot("3-options");
    r.press(Role::Button, "Install");
    {
        let asked = r.asked.borrow();
        let (args, elevate) = &asked.starts[0];
        assert!(*elevate, "for everyone, as an administrator");
        let at = |k: &str| args[args.iter().position(|a| a == k).unwrap() + 1].clone();
        assert_eq!(at("--scope"), "machine");
        assert_eq!(at("--components"), "app,vst3,organ");
        assert_eq!(at("--dir"), "C:\\Program Files\\Caecilia");
        assert_eq!(at("--organs-dir"), "C:\\AppData\\Caecilia\\Organs");
    }
    // The pretend work has ended at once.
    r.ui.frame();
    assert_eq!(r.page_title(), "Caecilia is installed");
    r.shot("5-done");
    r.press(Role::Button, "Finish");
    assert!(r.quit.get());
    assert_eq!(
        r.opened.borrow()[0],
        PathBuf::from("C:\\Program Files\\Caecilia\\caecilia.exe")
    );
}

#[test]
fn just_for_me_asks_no_administrator_and_a_refusal_offers_it() {
    let mut r = rig("decline", None, false, Lang::En);
    r.asked.borrow_mut().decline = true;
    r.press(Role::Button, "Next");
    r.press(
        Role::Switch,
        "I have read and accept the terms of the licence",
    );
    r.press(Role::Button, "Install");
    assert_eq!(r.page_title(), "Caecilia was not installed");
    r.shot("6-declined");
    r.press(Role::Button, "Install just for me");
    assert_eq!(r.page_title(), "Options");
    assert_eq!(
        r.ui.get(Role::Switch, "Just me").node.state.checked,
        Some(true)
    );
    r.asked.borrow_mut().decline = false;
    r.press(Role::Button, "Install");
    let asked = r.asked.borrow();
    let (args, elevate) = asked.starts.last().unwrap();
    assert!(!elevate);
    assert!(
        args.windows(2)
            .any(|w| w[0] == "--dir" && w[1] == "C:\\LocalAppData\\Programs\\Caecilia")
    );
}

#[test]
fn the_progress_shows_the_step_and_the_files() {
    let mut r = rig("progress", None, false, Lang::Pt);
    r.asked.borrow_mut().hold = true;
    let t = Lang::Pt.text();
    r.press(Role::Button, t.next);
    r.press(Role::Switch, t.licence_accept);
    r.press(Role::Button, t.install);
    r.ui.frame();
    assert_eq!(
        r.ui.get(Role::Dialog, "Instalação do Caecilia 2.0.1")
            .value_text(),
        Some(t.installing_title)
    );
    r.shot("4-progress");
}

#[test]
fn a_running_app_is_asked_to_close_first() {
    let mut r = rig("busy", None, false, Lang::En);
    r.in_use.set(true);
    r.press(Role::Button, "Next");
    r.press(
        Role::Switch,
        "I have read and accept the terms of the licence",
    );
    r.press(Role::Button, "Install");
    assert!(r.asked.borrow().starts.is_empty());
    r.shot("7-busy");
    r.in_use.set(false);
    r.press(Role::Button, "Retry");
    assert_eq!(r.asked.borrow().starts.len(), 1);
}

#[test]
fn a_failure_says_so_and_shows_the_log() {
    let mut r = rig("failed", None, false, Lang::En);
    r.asked.borrow_mut().exit = 1;
    r.press(Role::Button, "Next");
    r.press(
        Role::Switch,
        "I have read and accept the terms of the licence",
    );
    r.press(Role::Button, "Install");
    r.ui.frame();
    assert_eq!(r.page_title(), "Caecilia was not installed");
    r.press(Role::Button, "Show the log");
    assert!(r.opened.borrow()[0].to_string_lossy().ends_with(".log"));
}

#[test]
fn an_installation_here_offers_update_change_and_remove() {
    let mut r = rig("maintain", Some(found_machine("2.0.0")), false, Lang::En);
    assert_eq!(r.page_title(), "Caecilia is already installed");
    r.shot("8-update");
    r.press(Role::Button, "Change");
    assert_eq!(r.page_title(), "Options");
    assert!(
        r.ui.get(Role::Switch, "Just me").node.state.disabled,
        "the scope stays"
    );
    assert_eq!(
        r.ui.get(Role::Switch, "CLAP plugin").node.state.checked,
        Some(false)
    );
    r.shot("8-change");
    r.press(Role::Button, "Back");
    r.press(Role::Button, "Remove");
    assert_eq!(r.page_title(), "Remove Caecilia?");
    r.press(Role::Switch, "Also remove my organs and settings");
    r.shot("9-remove");
    r.press(Role::Button, "Remove");
    {
        let asked = r.asked.borrow();
        let (args, elevate) = &asked.starts[0];
        assert!(*elevate);
        assert_eq!(&args[..2], ["--engine", "uninstall"]);
        assert!(
            args.windows(2)
                .any(|w| w[0] == "--purge-dir" && w[1] == "C:\\Users\\owner\\AppData\\Caecilia")
        );
    }
    r.ui.frame();
    assert_eq!(r.page_title(), "Caecilia was removed");
    assert!(r.ui.describe().contains("Caecilia was removed"));
    r.shot("9-removed");
}

#[test]
fn an_installation_without_its_record_is_repaired_not_removed() {
    let dir = PathBuf::from("D:\\Program Files\\Caecilia");
    // As the uninstaller: it cannot tell its files, and says so.
    let mut r = rig_with(
        "unreadable-remove",
        None,
        Some((Scope::Machine, dir.clone())),
        true,
        Lang::En,
    );
    assert_eq!(r.page_title(), "Caecilia can't be removed safely");
    r.shot("10-unreadable");
    r.press(Role::Button, "Close");
    assert!(r.quit.get());
    assert!(r.asked.borrow().starts.is_empty());

    // As the installer: over it, where it is and for whom.
    let mut r = rig_with(
        "unreadable-repair",
        None,
        Some((Scope::User, dir.clone())),
        false,
        Lang::En,
    );
    r.press(Role::Button, "Next");
    r.press(
        Role::Switch,
        "I have read and accept the terms of the licence",
    );
    r.press(Role::Button, "Install");
    let asked = r.asked.borrow();
    let (args, elevate) = &asked.starts[0];
    assert!(!elevate);
    let at = |k: &str| args[args.iter().position(|a| a == k).unwrap() + 1].clone();
    assert_eq!(at("--scope"), "user");
    assert_eq!(PathBuf::from(at("--dir")), dir);
    assert!(args.iter().any(|a| a == "--force"));
}

#[test]
fn every_page_in_every_language_and_light_draws_and_is_accessible() {
    for lang in Lang::ALL {
        for night in [false, true] {
            let mut r = rig(&format!("all-{}-{night}", lang.code()), None, false, lang);
            if night {
                let switch =
                    r.ui.everything()
                        .into_iter()
                        .find(|f| f.name() == lang.text().night_day.0)
                        .unwrap();
                r.ui.act(&switch, Action::Press);
                r.ui.frame();
            }
            let tag = format!("{}-{}", lang.code(), if night { "night" } else { "day" });
            r.shot(&format!("lang-{tag}-welcome"));
            r.ui.assert_accessible();
            // Through the keyboard: Enter takes the default, Next.
            r.ui.key(Key::Enter);
            r.ui.frame();
            r.shot(&format!("lang-{tag}-licence"));
            r.ui.key(Key::Escape);
            r.ui.frame();
            let options = lang.text().options;
            let found =
                r.ui.everything()
                    .into_iter()
                    .find(|f| f.name() == options)
                    .unwrap();
            r.ui.act(&found, Action::Press);
            r.ui.frame();
            r.shot(&format!("lang-{tag}-options"));
            r.ui.assert_accessible();
        }
    }
    let mut r = rig("removing", Some(found_machine("2.0.1")), true, Lang::Pt);
    r.shot("pt-uninstall");
}
