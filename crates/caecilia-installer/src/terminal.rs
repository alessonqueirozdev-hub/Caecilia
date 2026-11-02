//! The wizard's questions in a terminal, for the systems where Manguebyte
//! has no windows yet (Linux and macOS). The work is the same: the installer
//! run again as an engine, through `sudo` for everyone, followed through
//! its log. Once those systems have windows, the wizard takes its place.

use std::cmp::Ordering;
use std::io::{BufRead, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use crate::args::Args;
use crate::engine::whole;
use crate::exit;
use crate::job::{Job, Launcher, StartError};
use crate::log::stamp;
use crate::payload::Manifest;
use crate::plan::{Choices, Components, Plan, Scope};
use crate::run::Found;
use crate::text::{Text, fill, size};
use crate::ui::Facts;
use crate::ui::wizard::choices_of;
use crate::version;

/// How often the log is read while the work runs.
const POLL: Duration = Duration::from_millis(100);

/// Asks on `input`, answers on `out`, works through `launcher`; returns the
/// exit code. At the end of `input` it stops asking, and does nothing more.
pub fn run<R: BufRead, W: Write>(
    facts: &Facts,
    launcher: &mut dyn Launcher,
    input: R,
    out: W,
) -> i32 {
    let mut session = Session {
        t: facts.lang.text(),
        facts,
        launcher,
        input,
        out,
    };
    session.start()
}

struct Session<'a, R, W> {
    facts: &'a Facts,
    t: &'static Text,
    launcher: &'a mut dyn Launcher,
    input: R,
    out: W,
}

impl<R: BufRead, W: Write> Session<'_, R, W> {
    fn say(&mut self, line: &str) {
        let _ = writeln!(self.out, "{line}");
    }

    /// The answer to `prompt`, trimmed; `None` when there will be no more.
    fn ask(&mut self, prompt: &str) -> Option<String> {
        let _ = write!(self.out, "{prompt}");
        let _ = self.out.flush();
        let mut line = String::new();
        match self.input.read_line(&mut line) {
            Ok(n) if n > 0 => Some(line.trim().to_owned()),
            _ => None,
        }
    }

    /// Yes or no, in the person's language or in English. With no more
    /// answers to come, no, whatever the default.
    fn yes(&mut self, question: &str, default: bool) -> bool {
        let (yes, no) = self.t.term_yes_no;
        let hint = if default {
            self.t.term_yes_no_hint.0
        } else {
            self.t.term_yes_no_hint.1
        };
        loop {
            let Some(answer) = self.ask(&format!("{question} {hint} ")) else {
                return false;
            };
            let answer = answer.to_lowercase();
            if answer.is_empty() {
                return default;
            }
            let first = answer.chars().next().unwrap_or_default().to_string();
            if first == yes || first == "y" {
                return true;
            }
            if first == no {
                return false;
            }
        }
    }

    /// One of `options`, by its number, from 0; `None` with no more answers.
    fn choose(&mut self, options: &[&str], default: usize) -> Option<usize> {
        for (i, option) in options.iter().enumerate() {
            self.say(&format!("  {}) {option}", i + 1));
        }
        let prompt = fill(self.t.term_choose, &[&(default + 1).to_string()]);
        loop {
            let answer = self.ask(&prompt)?;
            if answer.is_empty() {
                return Some(default);
            }
            if let Some(n) = answer
                .parse::<usize>()
                .ok()
                .filter(|n| (1..=options.len()).contains(n))
            {
                return Some(n - 1);
            }
        }
    }

    fn start(&mut self) -> i32 {
        let facts = self.facts;
        let manifest = match &facts.manifest {
            Ok(manifest) => manifest,
            Err(why) => {
                self.say(self.t.damaged_title);
                self.say(self.t.damaged_lead);
                self.say(why);
                return exit::DAMAGED;
            }
        };
        if facts.uninstall {
            return match (&facts.found, &facts.unreadable) {
                (Some(found), _) => self.remove(found),
                (None, Some((_, dir))) => {
                    self.say(self.t.unreadable_title);
                    let lead = fill(self.t.unreadable_lead, &[&dir.display().to_string()]);
                    self.say(&lead);
                    exit::FAILED
                }
                (None, None) => {
                    self.say(self.t.removed_title);
                    exit::OK
                }
            };
        }
        match &facts.found {
            Some(found) => self.maintain(manifest, found),
            None => self.first(manifest),
        }
    }

    /// Nothing here yet: the whole of the wizard.
    fn first(&mut self, manifest: &Manifest) -> i32 {
        let (t, facts) = (self.t, self.facts);
        self.say(&fill(t.title, &[&manifest.version]));
        self.say(t.welcome_lead);
        self.say(t.built_with);
        self.say("");
        self.say(t.licence_title);
        self.say(t.licence_lead);
        self.say("");
        self.say(&facts.licence);
        self.say("");
        if !self.yes(t.licence_accept, false) {
            self.say(t.licence_needed);
            return exit::CANCELLED;
        }
        self.say("");
        // Over an installation whose record is lost, where it is: a repair.
        let mut choices = match &facts.unreadable {
            Some((scope, dir)) => {
                let lead = fill(t.unreadable_lead, &[&dir.display().to_string()]);
                self.say(&lead);
                let mut choices = Choices::defaults(*scope);
                choices.program_dir = Some(dir.clone());
                choices
            }
            None => {
                self.say(t.who);
                let default = usize::from(!facts.elevated);
                let Some(who) = self.choose(&[t.everyone.0, t.just_me.0], default) else {
                    return exit::CANCELLED;
                };
                let scope = if who == 0 {
                    Scope::Machine
                } else {
                    Scope::User
                };
                let mut choices = Choices::defaults(scope);
                let Some(dir) = self.folder(&manifest.product, scope) else {
                    return exit::CANCELLED;
                };
                choices.program_dir = Some(dir);
                choices
            }
        };
        self.say("");
        self.say(t.what);
        let Some(components) = self.components() else {
            return exit::CANCELLED;
        };
        choices.components = components;
        choices.desktop = false;
        choices.start_menu = components.app && self.yes(t.term_launcher, true);
        let plan = Plan::new(manifest, &choices, &facts.places, facts.self_size);
        self.say("");
        self.say(&fill(t.needs, &[&size(plan.bytes())]));
        if !self.yes(t.term_go, true) {
            return exit::CANCELLED;
        }
        self.install(&manifest.product, choices)
    }

    /// Where the program goes, asked until it is somewhere it may go.
    fn folder(&mut self, product: &str, scope: Scope) -> Option<PathBuf> {
        let (t, places) = (self.t, &self.facts.places);
        let default = places.program_dir(scope, product);
        let prompt = fill(t.term_folder, &[&default.display().to_string()]);
        loop {
            let answer = self.ask(&prompt)?;
            let dir = if answer.is_empty() {
                default.clone()
            } else {
                whole(Path::new(&answer))
            };
            if scope == Scope::User || places.protects(&dir) {
                return Some(dir);
            }
            let root = places.program_files.display().to_string();
            self.say(&fill(t.term_protected, &[&root]));
        }
    }

    /// What to install, asked until it is something.
    fn components(&mut self) -> Option<Components> {
        let t = self.t;
        let question = |c: (&str, &str)| fill(t.term_component, &[c.0, c.1]);
        loop {
            let chosen = Components {
                app: self.yes(&question(t.app), true),
                vst3: self.yes(&question(t.vst3), true),
                clap: self.yes(&question(t.clap), true),
                organ: self.yes(&question(t.organ), true),
            };
            if !chosen.is_empty() {
                return Some(chosen);
            }
            self.say(t.choose_something);
            // At the end of the input every answer is no: stop asking.
            self.ask("")?;
        }
    }

    /// An installation here: update, repair or remove it.
    fn maintain(&mut self, manifest: &Manifest, found: &Found) -> i32 {
        let t = self.t;
        let old = &found.record.version;
        let new = &manifest.version;
        let who = match found.scope {
            Scope::Machine => t.scope_words.0,
            Scope::User => t.scope_words.1,
        };
        self.say(t.installed_title);
        self.say(&fill(t.installed_lead, &[old, who, new]));
        let order = version::compare(old, new);
        let replace = match order {
            Ordering::Less => Some(fill(t.update.0, &[new])),
            Ordering::Equal => Some(t.repair.0.to_owned()),
            Ordering::Greater => None,
        };
        if replace.is_none() {
            self.say(&fill(t.newer_lead, &[old]));
        }
        let mut options: Vec<&str> = replace.iter().map(String::as_str).collect();
        options.extend([t.uninstall.0, t.term_quit]);
        let quit = options.len() - 1;
        let Some(choice) = self.choose(&options, quit) else {
            return exit::CANCELLED;
        };
        if choice == quit {
            return exit::CANCELLED;
        }
        if replace.is_some() && choice == 0 {
            return self.install(&manifest.product, choices_of(found));
        }
        self.remove(found)
    }

    fn log_file(&self, product: &str, what: &str) -> PathBuf {
        let stamp = stamp(SystemTime::now());
        self.facts
            .log_dir
            .join(format!("{product}-{what}-{stamp}.log"))
    }

    fn install(&mut self, product: &str, mut choices: Choices) -> i32 {
        let (t, facts) = (self.t, self.facts);
        let dir = choices
            .program_dir
            .clone()
            .unwrap_or_else(|| facts.places.program_dir(choices.scope, product));
        // The person's own organs folder, whoever the work runs as.
        if choices.organs_dir.is_none() {
            choices.organs_dir = Some(facts.places.organs_dir(product));
        }
        choices.program_dir = Some(dir);
        let log = self.log_file(product, "Setup");
        let mut args = Args::for_engine_install(&choices, &log, facts.lang);
        if facts.found.is_none() && facts.unreadable.is_some() {
            args.push("--force".into());
        }
        let bin = match choices.scope {
            Scope::Machine => facts.places.common_bin.clone(),
            Scope::User => facts.places.user_bin.clone(),
        };
        let code = self.work(&args, choices.scope, &log);
        if code == exit::OK || code == exit::RESTART {
            self.say("");
            self.say(t.done_title);
            self.say(t.done_lead);
            if choices.components.app {
                self.say(t.term_no_window);
                let command = bin.join(product.to_ascii_lowercase());
                self.say(&fill(t.term_run, &[&command.display().to_string()]));
            }
            if choices.components.vst3 || choices.components.clap {
                self.say(t.rescan);
            }
        } else {
            self.say(t.failed_title);
        }
        code
    }

    fn remove(&mut self, found: &Found) -> i32 {
        let (t, facts) = (self.t, self.facts);
        let record = &found.record;
        self.say(t.remove_lead);
        self.say(&format!("  {}", record.program_dir.display()));
        if !self.yes(t.remove_title, false) {
            return exit::CANCELLED;
        }
        let purge = self.yes(t.purge.0, false).then(|| {
            record
                .data_dir
                .clone()
                .unwrap_or_else(|| facts.places.user_data_dir(&record.product))
        });
        let log = self.log_file(&record.product, "Uninstall");
        let args = Args::for_engine_uninstall(&record.program_dir, purge.as_deref(), &log);
        let code = self.work(&args, found.scope, &log);
        if code == exit::OK || code == exit::RESTART {
            self.say(t.removed_title);
            self.say(t.removed_lead);
        }
        code
    }

    /// Runs the engine with `args`, saying each step as it comes.
    fn work(&mut self, args: &[String], scope: Scope, log: &Path) -> i32 {
        let t = self.t;
        let elevate = scope == Scope::Machine && !self.facts.elevated;
        if elevate {
            self.say(t.term_sudo);
        }
        let running = match self.launcher.start(args, elevate) {
            Ok(running) => running,
            Err(StartError::Declined) => {
                self.say(t.declined_lead);
                return exit::CANCELLED;
            }
            Err(StartError::Failed(why)) => {
                self.say(&why);
                return exit::FAILED;
            }
        };
        let mut job = Job::new(running, log);
        let mut said: Option<String> = None;
        loop {
            job.poll();
            if job.progress.step != said {
                said.clone_from(&job.progress.step);
                let step = said.as_deref().unwrap_or("");
                if let Some((_, words)) = t.step_names.iter().find(|(key, _)| *key == step) {
                    let share = (job.progress.fraction() * 100.0) as u32;
                    self.say(&format!("[{share:3}%] {words}"));
                }
            }
            if let Some(code) = job.exit {
                // What it wrote just before it ended.
                job.poll();
                let kept: Vec<String> = job
                    .progress
                    .shown
                    .iter()
                    .filter_map(|l| l.strip_prefix("kept "))
                    .map(str::to_owned)
                    .collect();
                if !kept.is_empty() {
                    self.say(t.kept);
                    for path in kept {
                        self.say(&format!("  {path}"));
                    }
                }
                if code != exit::OK && code != exit::RESTART {
                    if let Some(error) = job.progress.error.clone() {
                        self.say(&error);
                    }
                    self.say(&fill(t.term_log, &[&log.display().to_string()]));
                }
                return code;
            }
            std::thread::sleep(POLL);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::Running;
    use crate::payload::{Payload, PayloadBuilder, Root};
    use crate::plan::{Flavor, Places};
    use crate::record::Record;
    use crate::text::Lang;
    use std::cell::RefCell;
    use std::io::Cursor;
    use std::rc::Rc;

    struct Ended(i32);

    impl Running for Ended {
        fn try_wait(&mut self) -> Option<i32> {
            Some(self.0)
        }
    }

    /// Writes the log the engine would, and ends with `code`.
    struct Fake {
        started: Rc<RefCell<Started>>,
        code: i32,
    }

    impl Launcher for Fake {
        fn start(
            &mut self,
            args: &[String],
            elevate: bool,
        ) -> Result<Box<dyn Running>, StartError> {
            self.started.borrow_mut().push((args.to_vec(), elevate));
            let at = args.iter().position(|a| a == "--log").unwrap();
            let text = "10:00:00 units 2\n10:00:00 step app\n10:00:01 file /opt/x 3 ab\n10:00:01 error it broke\n10:00:02 registered user\n";
            std::fs::write(&args[at + 1], text).unwrap();
            Ok(Box::new(Ended(self.code)))
        }
    }

    fn manifest() -> Manifest {
        let mut b = PayloadBuilder::new("Caecilia", "2.0.1", "Alesson Queiroz");
        b.add("core", Root::Program, "caecilia.ico", b"icon");
        b.add_file("app", Root::Program, "caecilia", b"app", true);
        Payload::parse(&b.build().unwrap()).unwrap().manifest
    }

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "caecilia-installer-terminal-{name}-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn facts(dir: &Path, found: Option<Found>, uninstall: bool) -> Facts {
        Facts {
            manifest: Ok(manifest()),
            places: Places::for_unix(Flavor::Linux, Path::new("/home/ana"), Path::new("/tmp")),
            found,
            unreadable: None,
            uninstall,
            version_1: false,
            elevated: false,
            licence: "Apache License".into(),
            self_size: 100,
            log_dir: dir.to_owned(),
            lang: Lang::En,
        }
    }

    fn found(scope: Scope, version: &str) -> Found {
        Found {
            scope,
            record: Record {
                product: "Caecilia".into(),
                version: version.into(),
                scope,
                components: Components::ALL,
                program_dir: PathBuf::from("/opt/Caecilia"),
                organs_dir: None,
                data_dir: Some(PathBuf::from("/home/owner/.config/caecilia")),
                files: vec![],
                dirs: vec![],
                shortcuts: vec![],
                leftovers: vec![],
                desktop: false,
                start_menu: true,
            },
        }
    }

    /// What the pretend work was started with: its arguments, and whether
    /// as an administrator.
    type Started = Vec<(Vec<String>, bool)>;

    /// Runs with `answers`; the exit code, what was said, what was started.
    fn talk(facts: &Facts, answers: &str, code: i32) -> (i32, String, Started) {
        let started = Rc::new(RefCell::new(Vec::new()));
        let mut fake = Fake {
            started: Rc::clone(&started),
            code,
        };
        let mut out = Vec::new();
        let exit = run(facts, &mut fake, Cursor::new(answers.to_owned()), &mut out);
        let started = started.borrow().clone();
        (exit, String::from_utf8(out).unwrap(), started)
    }

    fn at(args: &[String], key: &str) -> String {
        args[args.iter().position(|a| a == key).unwrap() + 1].clone()
    }

    #[test]
    fn a_first_installation_asks_everything_and_starts_the_engine() {
        let dir = scratch("first");
        let facts = facts(&dir, None, false);
        // Licence; just me; the default folder; no CLAP; launcher; go.
        let (code, said, started) = talk(&facts, "y\n2\n\n\n\nn\n\n\n\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(said.contains("Apache License"));
        assert!(said.contains("Caecilia is installed"));
        assert!(said.contains(".local"), "says where the command is: {said}");
        let (args, elevate) = &started[0];
        assert!(!elevate);
        assert_eq!(at(args, "--scope"), "user");
        assert_eq!(at(args, "--components"), "app,vst3,organ");
        assert_eq!(at(args, "--start-menu"), "1");
        assert_eq!(at(args, "--desktop"), "0");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn the_licence_refused_or_no_answers_install_nothing() {
        let dir = scratch("refused");
        let facts = facts(&dir, None, false);
        let (code, _, started) = talk(&facts, "n\n", exit::OK);
        assert_eq!(code, exit::CANCELLED);
        assert!(started.is_empty());
        // Input that ends: nothing is assumed, not even the defaults.
        let (code, _, started) = talk(&facts, "", exit::OK);
        assert_eq!(code, exit::CANCELLED);
        assert!(started.is_empty());
        let (code, _, started) = talk(&facts, "y\n", exit::OK);
        assert_eq!(code, exit::CANCELLED);
        assert!(started.is_empty());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn for_everyone_it_goes_through_sudo_and_only_where_it_is_protected() {
        let dir = scratch("everyone");
        let facts = facts(&dir, None, false);
        // Everyone; a folder a person could change, then the default.
        let (code, said, started) = talk(&facts, "y\n1\n/home/ana/x\n\n\n\n\n\n\n\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(said.contains("only an administrator can change"));
        assert!(said.contains("sudo"));
        let (args, elevate) = &started[0];
        assert!(*elevate);
        assert_eq!(at(args, "--scope"), "machine");
        assert_eq!(
            PathBuf::from(at(args, "--dir")),
            Path::new("/opt").join("Caecilia")
        );
        // The person's own organs, though root does the work.
        assert!(at(args, "--organs-dir").contains("ana"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_failure_says_why_and_where_the_log_is() {
        let dir = scratch("failure");
        let facts = facts(&dir, None, false);
        let (code, said, _) = talk(&facts, "y\n2\n\n\n\n\n\n\n\n", exit::FAILED);
        assert_eq!(code, exit::FAILED);
        assert!(said.contains("it broke"), "{said}");
        assert!(said.contains("The log:"));
        assert!(said.contains("Caecilia was not installed"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn an_older_installation_is_offered_the_update() {
        let dir = scratch("update");
        let facts = facts(&dir, Some(found(Scope::Machine, "2.0.0")), false);
        let (code, said, started) = talk(&facts, "1\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(said.contains("Update to 2.0.1"));
        let (args, elevate) = &started[0];
        assert!(*elevate);
        assert_eq!(&args[..2], ["--engine", "install"]);
        // Enter alone leaves it as it is.
        let (code, _, started) = talk(&facts, "\n", exit::OK);
        assert_eq!(code, exit::CANCELLED);
        assert!(started.is_empty());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_newer_installation_can_only_be_removed() {
        let dir = scratch("newer");
        let facts = facts(&dir, Some(found(Scope::User, "9.0.0")), false);
        let (code, said, started) = talk(&facts, "1\ny\nn\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(!said.contains("Update to"));
        assert_eq!(&started[0].0[..2], ["--engine", "uninstall"]);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn the_uninstaller_asks_and_purges_the_data_of_whoever_installed() {
        let dir = scratch("remove");
        let facts = facts(&dir, Some(found(Scope::User, "2.0.1")), true);
        let (code, _, started) = talk(&facts, "n\n", exit::OK);
        assert_eq!(code, exit::CANCELLED);
        assert!(started.is_empty());
        let (code, said, started) = talk(&facts, "y\ny\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(said.contains("Caecilia was removed"));
        assert_eq!(
            at(&started[0].0, "--purge-dir"),
            "/home/owner/.config/caecilia"
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn an_installation_without_its_record_is_repaired_not_removed() {
        let dir = scratch("unreadable");
        let mut f = facts(&dir, None, true);
        f.unreadable = Some((Scope::User, PathBuf::from("/home/ana/Caecilia")));
        let (code, said, started) = talk(&f, "", exit::OK);
        assert_eq!(code, exit::FAILED);
        assert!(said.contains("can't be removed safely"));
        assert!(started.is_empty());

        f.uninstall = false;
        let (code, said, started) = talk(&f, "y\n\n\n\n\n\n\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        let args = &started[0].0;
        assert!(args.iter().any(|a| a == "--force"));
        assert_eq!(at(args, "--dir"), "/home/ana/Caecilia");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn it_speaks_the_persons_language() {
        let dir = scratch("language");
        let mut f = facts(&dir, None, false);
        f.lang = Lang::Pt;
        let (code, said, started) = talk(&f, "s\n2\n\ns\ns\nn\nn\n\n\n", exit::OK);
        assert_eq!(code, exit::OK, "{said}");
        assert!(said.contains("[S/n]"));
        assert!(said.contains("Caecilia está instalado"), "{said}");
        assert_eq!(at(&started[0].0, "--components"), "app,vst3");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_damaged_installer_says_so() {
        let dir = scratch("damaged");
        let mut f = facts(&dir, None, false);
        f.manifest = Err("the hashes do not match".into());
        let (code, said, _) = talk(&f, "", exit::OK);
        assert_eq!(code, exit::DAMAGED);
        assert!(said.contains("the hashes do not match"));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
