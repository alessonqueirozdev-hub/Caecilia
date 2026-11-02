//! What an installation makes, and where: the person's choices and the
//! system's folders turned into a list of files, shortcuts and an entry in
//! *Apps & features*, before anything is touched.
//!
//! | | Everyone on this PC | Just me |
//! |---|---|---|
//! | Program | `%ProgramFiles%\Caecilia` | `%LOCALAPPDATA%\Programs\Caecilia` |
//! | VST3 | `%CommonProgramFiles%\VST3` | `%LOCALAPPDATA%\Programs\Common\VST3` |
//! | CLAP | `%CommonProgramFiles%\CLAP` | `%LOCALAPPDATA%\Programs\Common\CLAP` |
//! | Organs | `%APPDATA%\Caecilia\Organs` of the person installing | the same |
//! | Shortcuts | the Public desktop, the all-users Start menu | the person's own |
//!
//! The plugin folders are the ones the VST3 and CLAP specifications name.
//!
//! On Linux and macOS ([`Places::for_unix`]):
//!
//! | | Linux, everyone | Linux, just me | macOS, everyone | macOS, just me |
//! |---|---|---|---|---|
//! | Program | `/opt/Caecilia` | `~/.local/share/Caecilia` | `/Library/Application Support/Caecilia` | `~/Library/Application Support/Caecilia` |
//! | VST3 | `/usr/lib/vst3` | `~/.vst3` | `/Library/Audio/Plug-Ins/VST3` | `~/Library/Audio/Plug-Ins/VST3` |
//! | CLAP | `/usr/lib/clap` | `~/.clap` | `/Library/Audio/Plug-Ins/CLAP` | `~/Library/Audio/Plug-Ins/CLAP` |
//! | Command | `/usr/local/bin/caecilia` | `~/.local/bin/caecilia` | `/usr/local/bin/caecilia` | `~/.local/bin/caecilia` |
//! | Launcher | `/usr/share/applications` | `~/.local/share/applications` | `/Applications` | `~/Applications` |
//!
//! Organs go to `~/.config/caecilia/organs` on both, where the console's
//! library reads them.

use std::path::{Component, Path, PathBuf};

use crate::payload::{Entry, Manifest, Root};

/// Who an installation is for.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Scope {
    /// The person installing. Needs no administrator.
    User,
    /// Everyone on the PC. Needs an administrator.
    Machine,
}

impl Scope {
    /// Its name on the command line and in a record.
    pub fn name(self) -> &'static str {
        match self {
            Scope::User => "user",
            Scope::Machine => "machine",
        }
    }

    /// The scope a command line or a record names.
    pub fn from_name(name: &str) -> Option<Scope> {
        match name {
            "user" => Some(Scope::User),
            "machine" => Some(Scope::Machine),
            _ => None,
        }
    }
}

/// The system whose conventions the folders follow.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flavor {
    /// Windows.
    Windows,
    /// Linux.
    Linux,
    /// macOS.
    Macos,
}

impl Flavor {
    /// The system this program was built for.
    pub fn host() -> Flavor {
        if cfg!(windows) {
            Flavor::Windows
        } else if cfg!(target_os = "macos") {
            Flavor::Macos
        } else {
            Flavor::Linux
        }
    }
}

/// The folders of the system an installation uses.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Places {
    /// The system they are on.
    pub flavor: Flavor,
    /// The person's home folder (Linux and macOS).
    pub home: PathBuf,
    /// Where the person's commands go (Linux and macOS).
    pub user_bin: PathBuf,
    /// Where everyone's commands go (Linux and macOS).
    pub common_bin: PathBuf,
    /// `%ProgramFiles%`.
    pub program_files: PathBuf,
    /// `%CommonProgramFiles%`.
    pub common_files: PathBuf,
    /// `%LOCALAPPDATA%\Programs`.
    pub user_programs: PathBuf,
    /// `%APPDATA%` of the person installing.
    pub app_data: PathBuf,
    /// The person's desktop.
    pub user_desktop: PathBuf,
    /// The Public desktop, which everyone sees.
    pub common_desktop: PathBuf,
    /// The person's Start menu programs.
    pub user_start_menu: PathBuf,
    /// The all-users Start menu programs.
    pub common_start_menu: PathBuf,
    /// A folder for temporary files.
    pub temp: PathBuf,
}

impl Places {
    /// Every folder under `base`, for tests: nothing of the real system.
    pub fn under(base: &Path) -> Places {
        Places {
            flavor: Flavor::Windows,
            home: base.join("Home"),
            user_bin: PathBuf::new(),
            common_bin: PathBuf::new(),
            program_files: base.join("Program Files"),
            common_files: base.join("Program Files").join("Common Files"),
            user_programs: base.join("LocalAppData").join("Programs"),
            app_data: base.join("AppData"),
            user_desktop: base.join("Desktop"),
            common_desktop: base.join("Public").join("Desktop"),
            user_start_menu: base.join("Start Menu").join("Programs"),
            common_start_menu: base.join("ProgramData").join("Start Menu").join("Programs"),
            temp: base.join("Temp"),
        }
    }

    /// The folders of Linux or macOS for the person whose home is `home`.
    pub fn for_unix(flavor: Flavor, home: &Path, temp: &Path) -> Places {
        let linux = flavor == Flavor::Linux;
        let library = home.join("Library");
        Places {
            flavor,
            home: home.to_owned(),
            user_bin: home.join(".local").join("bin"),
            common_bin: PathBuf::from("/usr/local/bin"),
            program_files: PathBuf::from(if linux {
                "/opt"
            } else {
                "/Library/Application Support"
            }),
            common_files: PathBuf::from(if linux {
                "/usr/lib"
            } else {
                "/Library/Audio/Plug-Ins"
            }),
            user_programs: if linux {
                home.join(".local").join("share")
            } else {
                library.join("Application Support")
            },
            app_data: home.join(".config"),
            user_desktop: home.join("Desktop"),
            // Neither system has a desktop everyone shares.
            common_desktop: PathBuf::new(),
            user_start_menu: if linux {
                home.join(".local").join("share").join("applications")
            } else {
                home.join("Applications")
            },
            common_start_menu: PathBuf::from(if linux {
                "/usr/share/applications"
            } else {
                "/Applications"
            }),
            temp: temp.to_owned(),
        }
    }

    /// Where Linux and macOS keep which installations there are, as
    /// *Apps & features* does on Windows.
    pub fn entries_dir(&self, scope: Scope) -> PathBuf {
        match (self.flavor, scope) {
            (Flavor::Linux, Scope::Machine) => PathBuf::from("/var/lib/caecilia-setup"),
            (_, Scope::Machine) => self.program_files.join("caecilia-setup"),
            (_, Scope::User) => self.user_programs.join("caecilia-setup"),
        }
    }

    /// The default program folder of `scope`.
    pub fn program_dir(&self, scope: Scope, product: &str) -> PathBuf {
        match scope {
            Scope::Machine => self.program_files.join(product),
            Scope::User => self.user_programs.join(product),
        }
    }

    /// Whether `dir` is a folder inside `%ProgramFiles%`, which only an
    /// administrator can change. An installation for everyone must go
    /// there: its uninstaller is run as an administrator, so whoever could
    /// replace it could have their own program run as one.
    pub fn protects(&self, dir: &Path) -> bool {
        let lower = |c: Component<'_>| c.as_os_str().to_string_lossy().to_lowercase();
        let base: Vec<String> = self.program_files.components().map(lower).collect();
        let parts: Vec<Component<'_>> = dir.components().collect();
        parts.len() > base.len()
            && parts.iter().zip(&base).all(|(&c, b)| lower(c) == *b)
            && parts[base.len()..]
                .iter()
                .all(|c| matches!(c, Component::Normal(_)))
    }

    /// The folder hosts scan for plugins of `format` (`VST3`, `CLAP`).
    pub fn plugin_dir(&self, scope: Scope, format: &str) -> PathBuf {
        let lower = format.to_ascii_lowercase();
        match (self.flavor, scope) {
            (Flavor::Windows, Scope::Machine) | (Flavor::Macos, Scope::Machine) => {
                self.common_files.join(format)
            }
            (Flavor::Windows, Scope::User) => self.user_programs.join("Common").join(format),
            (Flavor::Linux, Scope::Machine) => self.common_files.join(lower),
            (Flavor::Linux, Scope::User) => self.home.join(format!(".{lower}")),
            (Flavor::Macos, Scope::User) => self
                .home
                .join("Library")
                .join("Audio")
                .join("Plug-Ins")
                .join(format),
        }
    }

    /// The organs folder of the person installing, which the console's
    /// library reads.
    pub fn organs_dir(&self, product: &str) -> PathBuf {
        let organs = if self.flavor == Flavor::Windows {
            "Organs"
        } else {
            "organs"
        };
        self.user_data_dir(product).join(organs)
    }

    /// The folder of the person's own data for `product`: their organs and
    /// their last session.
    pub fn user_data_dir(&self, product: &str) -> PathBuf {
        if self.flavor == Flavor::Windows {
            self.app_data.join(product)
        } else {
            self.app_data.join(product.to_ascii_lowercase())
        }
    }
}

/// The components the person may choose; `core` is always placed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Components {
    /// The standalone application.
    pub app: bool,
    /// The VST3 plugin.
    pub vst3: bool,
    /// The CLAP plugin.
    pub clap: bool,
    /// The demonstration organ.
    pub organ: bool,
}

impl Components {
    /// Everything.
    pub const ALL: Components = Components {
        app: true,
        vst3: true,
        clap: true,
        organ: true,
    };

    /// Whether the component a payload entry names is chosen.
    pub fn includes(&self, component: &str) -> bool {
        match component {
            "core" => true,
            "app" => self.app,
            "vst3" => self.vst3,
            "clap" => self.clap,
            "organ" => self.organ,
            _ => false,
        }
    }

    /// The chosen ones, as `app,vst3,clap,organ`.
    pub fn list(&self) -> String {
        let all = [
            ("app", self.app),
            ("vst3", self.vst3),
            ("clap", self.clap),
            ("organ", self.organ),
        ];
        all.iter()
            .filter(|(_, on)| *on)
            .map(|(name, _)| *name)
            .collect::<Vec<_>>()
            .join(",")
    }

    /// The components a list names; `None` for a name it does not know.
    pub fn parse(list: &str) -> Option<Components> {
        let mut c = Components {
            app: false,
            vst3: false,
            clap: false,
            organ: false,
        };
        for name in list.split(',').map(str::trim).filter(|s| !s.is_empty()) {
            match name.to_ascii_lowercase().as_str() {
                "app" => c.app = true,
                "vst3" => c.vst3 = true,
                "clap" => c.clap = true,
                "organ" | "organs" => c.organ = true,
                _ => return None,
            }
        }
        Some(c)
    }

    /// Whether nothing is chosen.
    pub fn is_empty(&self) -> bool {
        !(self.app || self.vst3 || self.clap || self.organ)
    }
}

/// What the person chose.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Choices {
    /// Who it is for.
    pub scope: Scope,
    /// What to place.
    pub components: Components,
    /// The program folder, if not the scope's default.
    pub program_dir: Option<PathBuf>,
    /// The organs folder, if not that of whoever runs the installation:
    /// an elevated installation runs as the administrator, so the window
    /// passes the person's own.
    pub organs_dir: Option<PathBuf>,
    /// A shortcut on the desktop.
    pub desktop: bool,
    /// A shortcut in the Start menu.
    pub start_menu: bool,
}

impl Choices {
    /// Everything, for `scope`, in the default folders, with both shortcuts.
    pub fn defaults(scope: Scope) -> Choices {
        Choices {
            scope,
            components: Components::ALL,
            program_dir: None,
            organs_dir: None,
            desktop: true,
            start_menu: true,
        }
    }
}

/// A file to place.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlannedFile {
    /// Where it goes.
    pub dest: PathBuf,
    /// Its entry in the payload.
    pub entry: Entry,
}

/// A shortcut to make.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Shortcut {
    /// The `.lnk` file.
    pub link: PathBuf,
    /// What it opens.
    pub target: PathBuf,
    /// The icon it shows.
    pub icon: PathBuf,
    /// Its tooltip.
    pub description: String,
}

/// What *Apps & features* says of the installation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UninstallEntry {
    /// The product's name.
    pub name: String,
    /// The version installed.
    pub version: String,
    /// Who publishes it.
    pub publisher: String,
    /// The program folder.
    pub location: PathBuf,
    /// The uninstaller.
    pub uninstaller: PathBuf,
    /// The icon shown beside the name.
    pub icon: PathBuf,
    /// The space it takes, in KiB.
    pub size_kib: u32,
}

/// Everything an installation makes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Plan {
    /// The product.
    pub product: String,
    /// The version.
    pub version: String,
    /// Who it is for.
    pub scope: Scope,
    /// The components chosen.
    pub components: Components,
    /// The program folder.
    pub program_dir: PathBuf,
    /// The data folder of the person the installation is made by: their
    /// organs and settings, which a removal asked to purge removes.
    pub data_dir: PathBuf,
    /// The files, from the payload.
    pub files: Vec<PlannedFile>,
    /// Where the uninstaller goes: a copy of the installer.
    pub uninstaller: PathBuf,
    /// The shortcuts.
    pub shortcuts: Vec<Shortcut>,
    /// The entry in *Apps & features*.
    pub entry: UninstallEntry,
    /// Whether a desktop shortcut was asked for.
    pub desktop: bool,
    /// Whether a Start menu shortcut was asked for.
    pub start_menu: bool,
}

/// The name of the application's executable in the payload.
pub const APP_EXE: &str = if cfg!(windows) {
    "caecilia.exe"
} else {
    "caecilia"
};
/// The uninstaller's name in the program folder.
pub const UNINSTALLER: &str = if cfg!(windows) {
    "uninstall.exe"
} else {
    "uninstall"
};
/// The icon in the program folder.
pub const ICON: &str = "caecilia.ico";

impl Plan {
    /// The plan for `choices` on a system with `places`, of the files in
    /// `manifest`; the uninstaller is `uninstaller_size` bytes.
    pub fn new(
        manifest: &Manifest,
        choices: &Choices,
        places: &Places,
        uninstaller_size: u64,
    ) -> Plan {
        let product = manifest.product.as_str();
        let scope = choices.scope;
        let program_dir = choices
            .program_dir
            .clone()
            .unwrap_or_else(|| places.program_dir(scope, product));
        let organs_dir = choices
            .organs_dir
            .clone()
            .unwrap_or_else(|| places.organs_dir(product));
        // `<data>\Organs` when the organs folder is the usual one, which
        // the window passes for the person even to an elevated engine.
        let data_dir = organs_dir
            .parent()
            .filter(|d| {
                organs_dir
                    .file_name()
                    .is_some_and(|n| n.eq_ignore_ascii_case("organs"))
                    && d.file_name()
                        .is_some_and(|n| n.eq_ignore_ascii_case(product))
            })
            .map(Path::to_owned)
            .unwrap_or_else(|| places.user_data_dir(product));
        let root_dir = |root: Root| match root {
            Root::Program => program_dir.clone(),
            Root::Vst3 => places.plugin_dir(scope, "VST3"),
            Root::Clap => places.plugin_dir(scope, "CLAP"),
            Root::Organs => organs_dir.clone(),
        };
        let files: Vec<PlannedFile> = manifest
            .entries
            .iter()
            .filter(|e| choices.components.includes(&e.component))
            .map(|e| PlannedFile {
                dest: e
                    .path
                    .split('/')
                    .fold(root_dir(e.root), |p, part| p.join(part)),
                entry: e.clone(),
            })
            .collect();
        let app = program_dir.join(APP_EXE);
        let icon = program_dir.join(ICON);
        let mut shortcuts = Vec::new();
        if choices.components.app {
            let (desktop, start) = match scope {
                Scope::Machine => (&places.common_desktop, &places.common_start_menu),
                Scope::User => (&places.user_desktop, &places.user_start_menu),
            };
            let lower = product.to_ascii_lowercase();
            let name = match places.flavor {
                Flavor::Windows => format!("{product}.lnk"),
                Flavor::Linux => format!("{lower}.desktop"),
                Flavor::Macos => format!("{product}.command"),
            };
            let link = |dir: &Path, name: &str| Shortcut {
                link: dir.join(name),
                target: app.clone(),
                icon: icon.clone(),
                description: format!("{product}, the pipe organ"),
            };
            if choices.desktop && !desktop.as_os_str().is_empty() {
                shortcuts.push(link(desktop, &name));
            }
            if choices.start_menu {
                shortcuts.push(link(start, &name));
            }
            // The command, on Linux and macOS: it plays in a terminal.
            let bin = match scope {
                Scope::Machine => &places.common_bin,
                Scope::User => &places.user_bin,
            };
            if !bin.as_os_str().is_empty() {
                shortcuts.push(link(bin, &lower));
            }
        }
        let uninstaller = program_dir.join(UNINSTALLER);
        let bytes: u64 = files.iter().map(|f| f.entry.size).sum::<u64>() + uninstaller_size;
        Plan {
            product: product.into(),
            version: manifest.version.clone(),
            scope,
            components: choices.components,
            entry: UninstallEntry {
                name: product.into(),
                version: manifest.version.clone(),
                publisher: manifest.publisher.clone(),
                location: program_dir.clone(),
                uninstaller: uninstaller.clone(),
                icon,
                size_kib: bytes.div_ceil(1024).min(u64::from(u32::MAX)) as u32,
            },
            program_dir,
            data_dir,
            files,
            uninstaller,
            shortcuts,
            desktop: choices.desktop,
            start_menu: choices.start_menu,
        }
    }

    /// The bytes the plan places.
    pub fn bytes(&self) -> u64 {
        u64::from(self.entry.size_kib) * 1024
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::payload::{Payload, PayloadBuilder};

    fn manifest() -> Manifest {
        let mut b = PayloadBuilder::new("Caecilia", "2.0.0", "Alesson Queiroz");
        b.add("app", Root::Program, "caecilia.exe", b"app");
        b.add("core", Root::Program, "caecilia.ico", b"ico");
        b.add(
            "vst3",
            Root::Vst3,
            "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3",
            b"dll",
        );
        b.add("clap", Root::Clap, "Caecilia.clap", b"dll");
        b.add("organ", Root::Organs, "caecilia-demo.organ.json", b"{}");
        let bytes = b.build().unwrap();
        Payload::parse(&bytes).unwrap().manifest
    }

    fn dest(plan: &Plan, path: &str) -> PathBuf {
        plan.files
            .iter()
            .find(|f| f.entry.path == path)
            .map(|f| f.dest.clone())
            .unwrap_or_else(|| panic!("{path} is not planned"))
    }

    #[cfg(windows)]
    #[test]
    fn a_machine_installation_goes_where_every_daw_looks() {
        let base = Path::new("T:\\");
        let places = Places::under(base);
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::Machine), &places, 10);
        let pf = base.join("Program Files");
        assert_eq!(plan.program_dir, pf.join("Caecilia"));
        assert_eq!(
            dest(&plan, "caecilia.exe"),
            pf.join("Caecilia").join("caecilia.exe")
        );
        assert_eq!(
            dest(&plan, "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3"),
            pf.join("Common Files\\VST3\\Caecilia.vst3\\Contents\\x86_64-win\\Caecilia.vst3")
        );
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            pf.join("Common Files\\CLAP\\Caecilia.clap")
        );
        assert_eq!(
            dest(&plan, "caecilia-demo.organ.json"),
            base.join("AppData\\Caecilia\\Organs\\caecilia-demo.organ.json")
        );
        let links: Vec<_> = plan.shortcuts.iter().map(|s| s.link.clone()).collect();
        assert_eq!(
            links,
            [
                base.join("Public\\Desktop\\Caecilia.lnk"),
                base.join("ProgramData\\Start Menu\\Programs\\Caecilia.lnk")
            ]
        );
        assert_eq!(plan.uninstaller, pf.join("Caecilia\\uninstall.exe"));
        assert_eq!(plan.entry.location, plan.program_dir);
    }

    #[cfg(windows)]
    #[test]
    fn a_personal_installation_stays_in_the_persons_folders() {
        let base = Path::new("T:\\");
        let places = Places::under(base);
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::User), &places, 10);
        let programs = base.join("LocalAppData\\Programs");
        assert_eq!(plan.program_dir, programs.join("Caecilia"));
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            programs.join("Common\\CLAP\\Caecilia.clap")
        );
        assert!(
            dest(&plan, "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3")
                .starts_with(programs.join("Common\\VST3\\Caecilia.vst3"))
        );
        assert_eq!(plan.shortcuts[0].link, base.join("Desktop\\Caecilia.lnk"));
        assert_eq!(
            plan.shortcuts[1].link,
            base.join("Start Menu\\Programs\\Caecilia.lnk")
        );
    }

    #[cfg(windows)]
    #[test]
    fn choices_leave_out_components_folders_and_shortcuts() {
        let places = Places::under(Path::new("T:\\"));
        let mut choices = Choices::defaults(Scope::Machine);
        choices.components = Components::parse("vst3").unwrap();
        choices.program_dir = Some(PathBuf::from("D:\\Apps\\Caecilia"));
        choices.organs_dir = Some(PathBuf::from("U:\\Organs"));
        let plan = Plan::new(&manifest(), &choices, &places, 10);
        let paths: Vec<_> = plan.files.iter().map(|f| f.entry.path.as_str()).collect();
        assert_eq!(
            paths,
            [
                "caecilia.ico",
                "Caecilia.vst3/Contents/x86_64-win/Caecilia.vst3"
            ]
        );
        assert_eq!(
            dest(&plan, "caecilia.ico"),
            PathBuf::from("D:\\Apps\\Caecilia\\caecilia.ico")
        );
        assert!(plan.shortcuts.is_empty(), "no app, no shortcut to it");

        choices.components = Components::ALL;
        choices.desktop = false;
        let plan = Plan::new(&manifest(), &choices, &places, 10);
        assert_eq!(
            dest(&plan, "caecilia-demo.organ.json"),
            PathBuf::from("U:\\Organs\\caecilia-demo.organ.json")
        );
        assert_eq!(plan.shortcuts.len(), 1);
    }

    #[test]
    fn component_lists_read_and_write() {
        assert_eq!(
            Components::parse("app,VST3, clap,organ"),
            Some(Components::ALL)
        );
        assert_eq!(Components::ALL.list(), "app,vst3,clap,organ");
        assert_eq!(Components::parse("vst2"), None);
        assert!(Components::parse("").unwrap().is_empty());
    }

    fn unix_places(flavor: Flavor) -> Places {
        Places::for_unix(flavor, Path::new("/home/ana"), Path::new("/tmp"))
    }

    fn at(base: &str, parts: &[&str]) -> PathBuf {
        parts
            .iter()
            .fold(PathBuf::from(base), |p, part| p.join(part))
    }

    #[test]
    fn linux_installations_go_where_its_hosts_look() {
        let places = unix_places(Flavor::Linux);
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::Machine), &places, 10);
        assert_eq!(plan.program_dir, at("/opt", &["Caecilia"]));
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            at("/usr/lib", &["clap", "Caecilia.clap"])
        );
        let links: Vec<PathBuf> = plan.shortcuts.iter().map(|s| s.link.clone()).collect();
        assert_eq!(
            links,
            [
                at("/usr/share/applications", &["caecilia.desktop"]),
                at("/usr/local/bin", &["caecilia"]),
            ]
        );
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::User), &places, 10);
        assert_eq!(
            plan.program_dir,
            at("/home/ana", &[".local", "share", "Caecilia"])
        );
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            at("/home/ana", &[".clap", "Caecilia.clap"])
        );
        assert_eq!(
            dest(&plan, "caecilia-demo.organ.json"),
            at(
                "/home/ana",
                &[".config", "caecilia", "organs", "caecilia-demo.organ.json"]
            )
        );
        assert_eq!(plan.data_dir, at("/home/ana", &[".config", "caecilia"]));
        assert_eq!(
            plan.shortcuts.last().unwrap().link,
            at("/home/ana", &[".local", "bin", "caecilia"])
        );
    }

    #[test]
    fn macos_installations_go_where_its_hosts_look() {
        let places = unix_places(Flavor::Macos);
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::Machine), &places, 10);
        assert_eq!(
            plan.program_dir,
            at("/Library/Application Support", &["Caecilia"])
        );
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            at("/Library/Audio/Plug-Ins", &["CLAP", "Caecilia.clap"])
        );
        assert_eq!(
            plan.shortcuts[0].link,
            at("/Applications", &["Caecilia.command"])
        );
        let plan = Plan::new(&manifest(), &Choices::defaults(Scope::User), &places, 10);
        assert_eq!(
            dest(&plan, "Caecilia.clap"),
            at(
                "/home/ana",
                &["Library", "Audio", "Plug-Ins", "CLAP", "Caecilia.clap"]
            )
        );
    }

    #[cfg(windows)]
    #[test]
    fn only_folders_inside_program_files_are_protected() {
        let places = Places::under(Path::new("C:\\"));
        for dir in [
            "C:\\Program Files\\Caecilia",
            "c:\\program files\\Audio\\Caecilia",
        ] {
            assert!(places.protects(Path::new(dir)), "{dir}");
        }
        for dir in [
            "C:\\Program Files",
            "C:\\Program Files\\..\\Caecilia",
            "C:\\Program Files Extra\\Caecilia",
            "D:\\Apps\\Caecilia",
            "C:\\LocalAppData\\Programs\\Caecilia",
        ] {
            assert!(!places.protects(Path::new(dir)), "{dir}");
        }
    }
}
