//! The engine against a pretend system under a temporary folder: never the
//! real Program Files, registry, desktop or Start menu.

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

use caecilia_installer::engine::{Engine, Error};
use caecilia_installer::log::Log;
use caecilia_installer::payload::{Payload, PayloadBuilder, Root, Unpacked};
use caecilia_installer::plan::{Choices, Components, Places, Plan, Scope};
use caecilia_installer::record::Record;
use caecilia_installer::system::{MemorySystem, System};

/// A folder of its own for each test, removed at the end.
struct Scratch(PathBuf);

impl Scratch {
    fn new(name: &str) -> Scratch {
        let dir = std::env::temp_dir().join(format!(
            "caecilia-installer-test-{name}-{}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

/// Every file under `dir` and its bytes, and every folder.
fn tree(dir: &Path) -> BTreeMap<PathBuf, Option<Vec<u8>>> {
    let mut out = BTreeMap::new();
    let mut stack = vec![dir.to_owned()];
    while let Some(d) = stack.pop() {
        for entry in fs::read_dir(&d).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                out.insert(path.clone(), None);
                stack.push(path);
            } else {
                out.insert(path.clone(), Some(fs::read(&path).unwrap()));
            }
        }
    }
    out
}

fn payload(version: &str, with_readme: bool) -> Unpacked {
    let plugin: Vec<u8> = (0..30_000u32).map(|i| (i % 253) as u8).collect();
    let mut b = PayloadBuilder::new("Caecilia", version, "Alesson Queiroz");
    b.add("core", Root::Program, "caecilia.ico", b"icon");
    b.add("core", Root::Program, "LICENSE.txt", b"Apache-2.0");
    b.add(
        "app",
        Root::Program,
        "caecilia.exe",
        format!("app {version}").as_bytes(),
    );
    b.add(
        "vst3",
        Root::Vst3,
        "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3",
        &plugin,
    );
    b.add("clap", Root::Clap, "Caecilia.clap", &plugin);
    b.add(
        "organ",
        Root::Organs,
        "caecilia-demo.organ.json",
        b"{\"name\":\"demo\"}",
    );
    if with_readme {
        b.add("core", Root::Program, "README.txt", b"read me");
    }
    let bytes = b.build().unwrap();
    Payload::parse(&bytes).unwrap().unpack().unwrap()
}

/// The places, with the folders a real system already has.
fn places(base: &Path) -> Places {
    let p = Places::under(base);
    for d in [
        &p.program_files,
        &p.common_files,
        &p.user_programs,
        &p.app_data,
        &p.user_desktop,
        &p.common_desktop,
        &p.user_start_menu,
        &p.common_start_menu,
        &p.temp,
    ] {
        fs::create_dir_all(d).unwrap();
    }
    p
}

fn install(
    sys: &mut MemorySystem,
    plan: &Plan,
    files: &Unpacked,
    previous: Option<&Record>,
) -> Result<caecilia_installer::engine::Outcome, Error> {
    let mut log = Log::in_memory();
    let mut engine = Engine {
        sys,
        log: &mut log,
        cancelled: &|| false,
    };
    engine.install(plan, files, b"MZ uninstaller", previous)
}

fn uninstall(
    sys: &mut MemorySystem,
    record: &Record,
    purge: Option<&Path>,
) -> caecilia_installer::engine::Outcome {
    let mut log = Log::in_memory();
    let mut engine = Engine {
        sys,
        log: &mut log,
        cancelled: &|| false,
    };
    engine.uninstall(record, purge).unwrap()
}

#[test]
fn an_installation_places_everything_and_its_removal_leaves_nothing() {
    for scope in [Scope::Machine, Scope::User] {
        let scratch = Scratch::new(&format!("roundtrip-{}", scope.name()));
        let places = places(&scratch.0);
        let before = tree(&scratch.0);
        let files = payload("2.0.0", false);
        let plan = Plan::new(&files.manifest, &Choices::defaults(scope), &places, 14);
        let mut sys = MemorySystem::default();
        let outcome = install(&mut sys, &plan, &files, None).unwrap();

        for f in &plan.files {
            assert_eq!(
                fs::read(&f.dest).unwrap(),
                files.bytes(&f.entry),
                "{}",
                f.dest.display()
            );
        }
        assert_eq!(fs::read(&plan.uninstaller).unwrap(), b"MZ uninstaller");
        for s in &plan.shortcuts {
            assert!(s.link.is_file(), "{}", s.link.display());
        }
        let installed = sys.installed(scope, "Caecilia").unwrap();
        assert_eq!(
            (installed.version.as_str(), installed.location.as_path()),
            ("2.0.0", plan.program_dir.as_path())
        );
        let record = Record::load(&plan.program_dir).unwrap();
        assert_eq!(record, outcome.record);
        assert_eq!(record.files.len(), plan.files.len() + 1);
        // No leftovers of the transaction.
        assert!(
            !tree(&scratch.0)
                .keys()
                .any(|p| p.to_string_lossy().contains(".caecilia-"))
        );

        let gone = uninstall(&mut sys, &record, None);
        assert!(gone.kept.is_empty(), "{:?}", gone.kept);
        assert_eq!(
            tree(&scratch.0),
            before,
            "the removal leaves the folders as they were"
        );
        assert!(sys.installed(scope, "Caecilia").is_none());
    }
}

#[test]
fn a_file_changed_after_installing_is_kept() {
    let scratch = Scratch::new("changed");
    let places = places(&scratch.0);
    let files = payload("2.0.0", false);
    let plan = Plan::new(
        &files.manifest,
        &Choices::defaults(Scope::User),
        &places,
        14,
    );
    let mut sys = MemorySystem::default();
    let record = install(&mut sys, &plan, &files, None).unwrap().record;
    let organ = plan
        .files
        .iter()
        .find(|f| f.entry.root == Root::Organs)
        .unwrap()
        .dest
        .clone();
    fs::write(&organ, b"my own registration").unwrap();
    // And an organ of the person's own beside it, and their session.
    let mine = organ.with_file_name("mine.organ.json");
    fs::write(&mine, b"{}").unwrap();
    let session = places.user_data_dir("Caecilia").join("session.txt");
    fs::write(&session, b"session").unwrap();

    let gone = uninstall(&mut sys, &record, None);
    assert_eq!(gone.kept, std::slice::from_ref(&organ));
    assert_eq!(fs::read(&organ).unwrap(), b"my own registration");
    assert!(mine.is_file() && session.is_file());
    assert!(!plan.program_dir.exists());

    // Asked to, the person's folder goes too.
    let gone = uninstall(&mut sys, &record, Some(&places.user_data_dir("Caecilia")));
    assert!(
        !places.user_data_dir("Caecilia").exists(),
        "{:?}",
        gone.kept
    );
}

#[test]
fn a_failure_half_way_puts_everything_back() {
    let scratch = Scratch::new("rollback");
    let places = places(&scratch.0);
    let files = payload("2.0.0", false);
    let plan = Plan::new(
        &files.manifest,
        &Choices::defaults(Scope::Machine),
        &places,
        14,
    );
    // A folder where the CLAP plugin goes: it cannot be replaced.
    let clap = plan
        .files
        .iter()
        .find(|f| f.entry.root == Root::Clap)
        .unwrap()
        .dest
        .clone();
    fs::create_dir_all(&clap).unwrap();
    let before = tree(&scratch.0);
    let mut sys = MemorySystem::default();
    let err = install(&mut sys, &plan, &files, None).unwrap_err();
    assert!(matches!(err, Error::Io { .. }), "{err}");
    assert_eq!(tree(&scratch.0), before);
    assert!(sys.entries.is_empty());
}

#[test]
fn a_refused_registration_and_a_cancel_put_everything_back() {
    let scratch = Scratch::new("refused");
    let places = places(&scratch.0);
    let files = payload("2.0.0", false);
    let plan = Plan::new(
        &files.manifest,
        &Choices::defaults(Scope::Machine),
        &places,
        14,
    );
    let before = tree(&scratch.0);
    let mut sys = MemorySystem {
        fail_register: true,
        ..MemorySystem::default()
    };
    assert!(install(&mut sys, &plan, &files, None).is_err());
    assert_eq!(tree(&scratch.0), before);

    // Cancelled after the third question.
    let asked = std::cell::Cell::new(0);
    let cancelled = || {
        asked.set(asked.get() + 1);
        asked.get() > 3
    };
    let mut sys = MemorySystem::default();
    let mut log = Log::in_memory();
    let mut engine = Engine {
        sys: &mut sys,
        log: &mut log,
        cancelled: &cancelled,
    };
    let err = engine.install(&plan, &files, b"MZ", None).unwrap_err();
    assert!(matches!(err, Error::Cancelled));
    assert_eq!(tree(&scratch.0), before);
}

#[test]
fn an_upgrade_replaces_files_and_drops_what_the_new_version_has_not() {
    let scratch = Scratch::new("upgrade");
    let places = places(&scratch.0);
    let old_files = payload("2.0.0", true);
    let old_plan = Plan::new(
        &old_files.manifest,
        &Choices::defaults(Scope::Machine),
        &places,
        14,
    );
    let mut sys = MemorySystem::default();
    let old = install(&mut sys, &old_plan, &old_files, None)
        .unwrap()
        .record;
    assert!(old_plan.program_dir.join("README.txt").is_file());

    let new_files = payload("2.0.1", false);
    let mut choices = Choices::defaults(Scope::Machine);
    choices.components = Components::parse("app,vst3,organ").unwrap();
    let new_plan = Plan::new(&new_files.manifest, &choices, &places, 14);
    let new = install(&mut sys, &new_plan, &new_files, Some(&old))
        .unwrap()
        .record;

    assert_eq!(
        fs::read(new_plan.program_dir.join("caecilia.exe")).unwrap(),
        b"app 2.0.1"
    );
    assert!(
        !new_plan.program_dir.join("README.txt").exists(),
        "2.0.1 has no README"
    );
    let clap = old_plan
        .files
        .iter()
        .find(|f| f.entry.root == Root::Clap)
        .unwrap()
        .dest
        .clone();
    assert!(!clap.exists(), "the CLAP plugin was left out");
    assert_eq!(
        sys.installed(Scope::Machine, "Caecilia").unwrap().version,
        "2.0.1"
    );
    assert_eq!(Record::load(&new_plan.program_dir).unwrap(), new);
    assert!(
        !tree(&scratch.0)
            .keys()
            .any(|p| p.to_string_lossy().contains(".caecilia-"))
    );

    // And everything goes on removal, folders the old version made too.
    uninstall(&mut sys, &new, None);
    assert!(!new_plan.program_dir.exists());
    assert!(!places.plugin_dir(Scope::Machine, "VST3").exists());
    assert!(!places.plugin_dir(Scope::Machine, "CLAP").exists());
}

#[test]
fn a_dropped_plugin_a_host_still_holds_is_not_forgotten() {
    use std::os::windows::fs::OpenOptionsExt;
    // Per person, it waits for the next run; for everyone, for a restart.
    for admin in [false, true] {
        let scratch = Scratch::new(&format!("dropped-{admin}"));
        let places = places(&scratch.0);
        let old_files = payload("2.0.0", false);
        let old_plan = Plan::new(
            &old_files.manifest,
            &Choices::defaults(Scope::User),
            &places,
            14,
        );
        let mut sys = MemorySystem {
            can_delete_at_restart: admin,
            ..MemorySystem::default()
        };
        let old = install(&mut sys, &old_plan, &old_files, None)
            .unwrap()
            .record;
        let clap = old_plan
            .files
            .iter()
            .find(|f| f.entry.root == Root::Clap)
            .unwrap()
            .dest
            .clone();
        // A host has it open: it can be read, not deleted.
        let held = fs::OpenOptions::new()
            .read(true)
            .share_mode(1)
            .open(&clap)
            .unwrap();

        let new_files = payload("2.0.1", false);
        let mut choices = Choices::defaults(Scope::User);
        choices.components = Components::parse("app,vst3,organ").unwrap();
        let new_plan = Plan::new(&new_files.manifest, &choices, &places, 14);
        let outcome = install(&mut sys, &new_plan, &new_files, Some(&old)).unwrap();
        drop(held);
        assert!(outcome.kept.is_empty(), "{:?}", outcome.kept);
        assert_eq!(outcome.restart, admin);
        if admin {
            assert_eq!(sys.at_restart, std::slice::from_ref(&clap));
            assert!(outcome.record.leftovers.is_empty());
        } else {
            assert_eq!(outcome.record.leftovers, std::slice::from_ref(&clap));
            assert_eq!(Record::load(&new_plan.program_dir).unwrap(), outcome.record);
            let again = install(&mut sys, &new_plan, &new_files, Some(&outcome.record)).unwrap();
            assert!(again.record.leftovers.is_empty());
            assert!(!clap.exists(), "the next run cleared it");
        }
    }
}

#[test]
fn a_running_app_is_replaced_and_its_old_copy_cleared_later() {
    let scratch = Scratch::new("inuse");
    let places = places(&scratch.0);
    let files = payload("2.0.0", false);
    let plan = Plan::new(
        &files.manifest,
        &Choices::defaults(Scope::User),
        &places,
        14,
    );
    let mut sys = MemorySystem::default();
    let old = install(&mut sys, &plan, &files, None).unwrap().record;
    let exe = plan.program_dir.join("caecilia.exe");

    // A real program running from where the app is: this test binary,
    // asleep in the test below. Windows lets a running program be renamed
    // but not deleted.
    fs::copy(std::env::current_exe().unwrap(), &exe).unwrap();
    let mut running = std::process::Command::new(&exe)
        .args(["--ignored", "--exact", "sleeper", "--test-threads=1"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .unwrap();
    std::thread::sleep(std::time::Duration::from_millis(500));
    assert!(caecilia_installer::engine::in_use(&exe));

    let files2 = payload("2.0.1", false);
    let plan2 = Plan::new(
        &files2.manifest,
        &Choices::defaults(Scope::User),
        &places,
        14,
    );
    let outcome = install(&mut sys, &plan2, &files2, Some(&old));
    running.kill().unwrap();
    running.wait().unwrap();
    let outcome = outcome.unwrap();
    assert_eq!(fs::read(&exe).unwrap(), b"app 2.0.1");
    assert_eq!(
        outcome.record.leftovers.len(),
        1,
        "the old copy waits for the next run"
    );
    assert!(outcome.record.leftovers[0].exists());

    // Windows lets go of a program's image a moment after it has ended.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
    while caecilia_installer::engine::in_use(&outcome.record.leftovers[0])
        && std::time::Instant::now() < deadline
    {
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
    let again = install(&mut sys, &plan2, &files2, Some(&outcome.record)).unwrap();
    assert!(again.record.leftovers.is_empty());
    assert!(!outcome.record.leftovers[0].exists());
}

#[test]
fn the_running_uninstaller_leaves_no_folder_behind() {
    let scratch = Scratch::new("selfremove");
    let places = places(&scratch.0);
    let files = payload("2.0.0", false);
    let plan = Plan::new(
        &files.manifest,
        &Choices::defaults(Scope::User),
        &places,
        14,
    );
    // The uninstaller is a real program: this test binary.
    let me = fs::read(std::env::current_exe().unwrap()).unwrap();
    let mut sys = MemorySystem::default();
    let mut log = Log::in_memory();
    let record = Engine {
        sys: &mut sys,
        log: &mut log,
        cancelled: &|| false,
    }
    .install(&plan, &files, &me, None)
    .unwrap()
    .record;
    let mut running = std::process::Command::new(&plan.uninstaller)
        .args(["--ignored", "--exact", "sleeper", "--test-threads=1"])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .unwrap();
    std::thread::sleep(std::time::Duration::from_millis(500));
    let gone = uninstall(&mut sys, &record, None);
    running.kill().unwrap();
    running.wait().unwrap();
    assert!(gone.kept.is_empty(), "{:?}", gone.kept);
    assert!(!plan.program_dir.exists(), "the folder went with it");
    // What the temporary folder's cleaning would take.
    let moved =
        std::env::temp_dir().join(format!("uninstall.exe.{}.caecilia-old", std::process::id()));
    for _ in 0..100 {
        if fs::remove_file(&moved).is_ok() || !moved.exists() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
}

/// What the tests above run: a program that keeps running.
#[test]
#[ignore = "run by the tests of programs in use"]
fn sleeper() {
    std::thread::sleep(std::time::Duration::from_secs(30));
}
