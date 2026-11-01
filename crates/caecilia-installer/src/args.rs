//! The command line.
//!
//! For people and deployment tools:
//!
//! ```text
//! Caecilia-Setup.exe [/S] [/ALLUSERS | /CURRENTUSER] [/DIR="D:\Apps\Caecilia"]
//!     [/COMPONENTS=app,vst3,clap,organ] [/NOSHORTCUTS] [/NODESKTOP] [/NOSTARTMENU]
//!     [/LOG="path"] [/LANG=en|pt|es] [/FORCE]
//! uninstall.exe --uninstall [/S] [/PURGE]
//! ```
//!
//! `/S` installs without a window, accepting the licence as the person
//! deploying it has. The installer also starts itself, elevated or not,
//! to do the work while its window shows the progress: `--engine install`
//! or `--engine uninstall` with the choices made in the window.

use std::path::PathBuf;

use crate::plan::{Choices, Components, Scope};
use crate::text::Lang;

/// What to do.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    /// Install (or update, change, repair).
    Install,
    /// Remove.
    Uninstall,
}

/// The command line, read.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Args {
    /// Install or remove.
    pub mode: Mode,
    /// Without a window.
    pub silent: bool,
    /// The work of a window in another process: no window, and the
    /// choices below are final.
    pub engine: bool,
    /// Who for, if said.
    pub scope: Option<Scope>,
    /// The program folder, if said.
    pub dir: Option<PathBuf>,
    /// The organs folder, if said (the window passes the person's own to
    /// an elevated engine).
    pub organs_dir: Option<PathBuf>,
    /// The components, if said.
    pub components: Option<Components>,
    /// Shortcuts on the desktop.
    pub desktop: bool,
    /// Shortcuts in the Start menu.
    pub start_menu: bool,
    /// Where to write the log, if said.
    pub log: Option<PathBuf>,
    /// The language, if said.
    pub lang: Option<Lang>,
    /// Install over a newer version.
    pub force: bool,
    /// When removing, remove the person's organs and settings too.
    pub purge: bool,
    /// The person's data folder to purge, as the window found it.
    pub purge_dir: Option<PathBuf>,
    /// The uninstaller already runs from a copy, so it can remove its
    /// folder.
    pub relocated: bool,
}

impl Default for Args {
    fn default() -> Args {
        Args {
            mode: Mode::Install,
            silent: false,
            engine: false,
            scope: None,
            dir: None,
            organs_dir: None,
            components: None,
            desktop: true,
            start_menu: true,
            log: None,
            lang: None,
            force: false,
            purge: false,
            purge_dir: None,
            relocated: false,
        }
    }
}

impl Args {
    /// Reads `args`, without the program's name.
    pub fn parse<I: IntoIterator<Item = String>>(args: I) -> Result<Args, String> {
        let mut a = Args::default();
        let mut it = args.into_iter();
        while let Some(arg) = it.next() {
            // `/NAME=value` or `--name value`; names in any case.
            let (name, inline) = match arg.split_once('=') {
                Some((n, v)) => (n.to_owned(), Some(v.trim_matches('"').to_owned())),
                None => (arg.clone(), None),
            };
            let key = name.trim_start_matches(['/', '-']).to_ascii_lowercase();
            let mut value = |what: &str| -> Result<String, String> {
                inline
                    .clone()
                    .or_else(|| it.next())
                    .ok_or_else(|| format!("{arg} needs {what}"))
            };
            match key.as_str() {
                "s" | "silent" | "quiet" => a.silent = true,
                "allusers" => a.scope = Some(Scope::Machine),
                "currentuser" => a.scope = Some(Scope::User),
                "scope" => {
                    let v = value("user or machine")?;
                    a.scope = Some(Scope::from_name(&v).ok_or(format!("unknown scope '{v}'"))?);
                }
                "dir" | "d" => a.dir = Some(PathBuf::from(value("a folder")?)),
                "organs-dir" => a.organs_dir = Some(PathBuf::from(value("a folder")?)),
                "components" => {
                    let v = value("a list")?;
                    let c = Components::parse(&v).ok_or(format!("unknown component in '{v}'"))?;
                    a.components = Some(c);
                }
                "noshortcuts" => (a.desktop, a.start_menu) = (false, false),
                "nodesktop" => a.desktop = false,
                "nostartmenu" => a.start_menu = false,
                "desktop" => a.desktop = value("0 or 1")? != "0",
                "start-menu" => a.start_menu = value("0 or 1")? != "0",
                "log" => a.log = Some(PathBuf::from(value("a file")?)),
                "lang" => {
                    let v = value("en, pt or es")?;
                    a.lang = Some(Lang::from_code(&v).ok_or(format!("unknown language '{v}'"))?);
                }
                "force" => a.force = true,
                "purge" => a.purge = true,
                "purge-dir" => {
                    a.purge = true;
                    a.purge_dir = Some(PathBuf::from(value("a folder")?));
                }
                "uninstall" => a.mode = Mode::Uninstall,
                "relocated" => a.relocated = true,
                "engine" => {
                    a.engine = true;
                    a.mode = match value("install or uninstall")?.as_str() {
                        "install" => Mode::Install,
                        "uninstall" => Mode::Uninstall,
                        other => return Err(format!("unknown engine work '{other}'")),
                    };
                }
                _ => return Err(format!("unknown argument '{arg}'")),
            }
        }
        Ok(a)
    }

    /// The choices the command line makes, the rest as by default.
    pub fn choices(&self, scope: Scope) -> Choices {
        Choices {
            scope,
            components: self.components.unwrap_or(Components::ALL),
            program_dir: self.dir.clone(),
            organs_dir: self.organs_dir.clone(),
            desktop: self.desktop,
            start_menu: self.start_menu,
        }
    }

    /// The command line that has an engine carry out `choices`, logging to
    /// `log`.
    pub fn for_engine_install(choices: &Choices, log: &std::path::Path, lang: Lang) -> Vec<String> {
        let mut v = vec![
            "--engine".into(),
            "install".into(),
            "--scope".into(),
            choices.scope.name().into(),
            "--components".into(),
            choices.components.list(),
            "--desktop".into(),
            u8::from(choices.desktop).to_string(),
            "--start-menu".into(),
            u8::from(choices.start_menu).to_string(),
            "--lang".into(),
            lang.code().into(),
            "--log".into(),
            log.to_string_lossy().into_owned(),
        ];
        if let Some(dir) = &choices.program_dir {
            v.extend(["--dir".into(), dir.to_string_lossy().into_owned()]);
        }
        if let Some(dir) = &choices.organs_dir {
            v.extend(["--organs-dir".into(), dir.to_string_lossy().into_owned()]);
        }
        v
    }

    /// The command line that has an engine remove the installation for
    /// `scope`, purging `purge` if given.
    pub fn for_engine_uninstall(scope: Scope, purge: Option<&std::path::Path>, log: &std::path::Path) -> Vec<String> {
        let mut v = vec![
            "--engine".into(),
            "uninstall".into(),
            "--scope".into(),
            scope.name().into(),
            "--log".into(),
            log.to_string_lossy().into_owned(),
        ];
        if let Some(dir) = purge {
            v.extend(["--purge-dir".into(), dir.to_string_lossy().into_owned()]);
        }
        v
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(s: &[&str]) -> Result<Args, String> {
        Args::parse(s.iter().map(|s| s.to_string()))
    }

    #[test]
    fn the_silent_switches_read_as_documented() {
        let a = parse(&[
            "/S",
            "/ALLUSERS",
            "/DIR=\"D:\\Apps\\Caecilia\"",
            "/COMPONENTS=vst3,clap",
            "/NODESKTOP",
            "/LOG=C:\\x.log",
            "/lang=pt",
        ])
        .unwrap();
        assert!(a.silent && !a.engine);
        assert_eq!(a.scope, Some(Scope::Machine));
        assert_eq!(a.dir, Some(PathBuf::from("D:\\Apps\\Caecilia")));
        assert_eq!(a.components, Components::parse("vst3,clap"));
        assert!(!a.desktop && a.start_menu);
        assert_eq!(a.log, Some(PathBuf::from("C:\\x.log")));
        assert_eq!(a.lang, Some(Lang::Pt));
        assert_eq!(parse(&["/currentuser", "/noshortcuts"]).unwrap().scope, Some(Scope::User));
        let u = parse(&["--uninstall", "/S", "/PURGE"]).unwrap();
        assert_eq!((u.mode, u.silent, u.purge), (Mode::Uninstall, true, true));
    }

    #[test]
    fn mistakes_are_refused() {
        assert!(parse(&["/COMPONENTS=vst2"]).is_err());
        assert!(parse(&["/WHAT"]).is_err());
        assert!(parse(&["--dir"]).is_err());
        assert!(parse(&["/LANG=fr"]).is_err());
    }

    #[test]
    fn the_engine_reads_what_the_window_writes() {
        let mut choices = Choices::defaults(Scope::Machine);
        choices.components = Components::parse("app,organ").unwrap();
        choices.program_dir = Some(PathBuf::from("D:\\Caecilia"));
        choices.organs_dir = Some(PathBuf::from("C:\\Users\\a\\AppData\\Roaming\\Caecilia\\Organs"));
        choices.desktop = false;
        let line = Args::for_engine_install(&choices, std::path::Path::new("C:\\t.log"), Lang::Es);
        let a = Args::parse(line).unwrap();
        assert!(a.engine);
        assert_eq!(a.mode, Mode::Install);
        assert_eq!(a.choices(a.scope.unwrap()), choices);
        assert_eq!(a.lang, Some(Lang::Es));

        let line = Args::for_engine_uninstall(Scope::User, Some(std::path::Path::new("C:\\data")), std::path::Path::new("C:\\u.log"));
        let a = Args::parse(line).unwrap();
        assert_eq!((a.mode, a.scope, a.purge), (Mode::Uninstall, Some(Scope::User), true));
        assert_eq!(a.purge_dir, Some(PathBuf::from("C:\\data")));
    }
}
