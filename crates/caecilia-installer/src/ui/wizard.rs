//! The wizard: one widget that draws the whole window, page by page, and
//! keeps where each of its controls is so that the pointer, the keyboard
//! and assistive technology reach the same controls.

use std::cell::{Cell, RefCell};
use std::path::{Path, PathBuf};
use std::rc::Rc;

use manguebyte_gfx::{Color, Point, Rect, RoundedRect, Scene, Size};
use manguebyte_ui::access::{AccessItem, AccessNode, Action, Actions, Role};
use manguebyte_ui::{AccessCx, Event, EventCx, Handled, Key, MeasureCx, PaintCx, UpdateCx, Widget};
use manguebyte_window::{Dialogs, FileDialog};

use super::look::{DAY, Look, NIGHT};
use super::paint::{self, Button, StepState, style};
use crate::args::Args;
use crate::job::{Job, Launcher, StartError};
use crate::payload::Manifest;
use crate::plan::{APP_EXE, Choices, Places, Scope};
use crate::run::Found;
use crate::text::{Lang, Text, fill, size};
use crate::version;

/// The window's size, in logical pixels.
pub const SIZE: Size = Size::new(760.0, 520.0);

/// What the installer knows when it opens.
pub struct Facts {
    /// What the payload holds, or why it cannot be used.
    pub manifest: Result<Manifest, String>,
    /// The system's folders.
    pub places: Places,
    /// The installation already here.
    pub found: Option<Found>,
    /// An installation in *Apps & features* whose record is missing or
    /// damaged: who it is for and its folder.
    pub unreadable: Option<(Scope, PathBuf)>,
    /// Started as the uninstaller.
    pub uninstall: bool,
    /// Caecilia 1.x is installed too.
    pub version_1: bool,
    /// The process already has an administrator's rights.
    pub elevated: bool,
    /// The licence's text.
    pub licence: String,
    /// The size of the installer, which becomes the uninstaller.
    pub self_size: u64,
    /// Where logs go.
    pub log_dir: PathBuf,
    /// The language to start in.
    pub lang: Lang,
}

/// What the wizard does to the world, given so tests can pretend.
pub struct Env {
    /// Starts the work.
    pub launcher: Box<dyn Launcher>,
    /// The window's file dialogs, once it is open.
    pub dialogs: Rc<RefCell<Option<Dialogs>>>,
    /// Ends the application.
    pub quit: Box<dyn Fn()>,
    /// Opens a file: the application, or the log in its viewer.
    pub open: Box<dyn Fn(&Path)>,
    /// Whether a file is held by a running program.
    pub in_use: Box<dyn Fn(&Path) -> bool>,
    /// Set while work runs, so the window refuses to close.
    pub busy: Rc<Cell<bool>>,
}

/// A page.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Page {
    /// What will be installed.
    Welcome,
    /// The licence, to accept.
    Licence,
    /// Who for, what, where.
    Options,
    /// Installing or removing.
    Progress,
    /// Installed.
    Done,
    /// Not installed, or not removed.
    Failed,
    /// Already installed: update, repair, change, remove.
    Maintain,
    /// Remove?
    Remove,
    /// Removed.
    Removed,
    /// Installed, but its record is missing or damaged: not removable.
    Unreadable,
    /// The installer is damaged.
    Damaged,
}

#[derive(Clone, Debug, PartialEq)]
enum Failure {
    Error(String),
    Cancelled,
    Declined,
}

/// What a control does.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Act {
    Next,
    Back,
    Options,
    Install,
    Accept,
    Scope(Scope),
    Component(usize),
    Desktop,
    StartMenu,
    PickFolder,
    Cancel,
    Finish,
    Close,
    Retry,
    CloseSheet,
    Update,
    Modify,
    AskRemove,
    Remove,
    Purge,
    OpenAfter,
    Details,
    ShowLog,
    Night,
    Lang(Lang),
    JustMe,
}

/// A control, where it was last drawn.
#[derive(Clone, Debug)]
struct Control {
    rect: Rect,
    act: Act,
    role: Role,
    name: String,
    checked: Option<bool>,
    enabled: bool,
    /// What Enter does when nothing has the focus.
    default: bool,
}

/// The wizard.
pub struct Wizard {
    facts: Facts,
    env: Env,
    lang: Lang,
    night: bool,
    page: Page,
    /// Where Back goes from the options.
    options_from: Page,
    choices: Choices,
    scope_locked: bool,
    accepted: bool,
    scroll: f32,
    job: Option<Job>,
    removing: bool,
    exit: Option<i32>,
    failure: Option<Failure>,
    last_log: Option<PathBuf>,
    open_after: bool,
    purge: bool,
    sheet: bool,
    details: bool,
    hint: Option<fn(&Text) -> &'static str>,
    controls: Vec<Control>,
    focus: Option<usize>,
    pressed: Option<usize>,
    /// Bumped by every change worth drawing.
    changes: u64,
    drawn: Option<u64>,
    asked_focus: bool,
}

const COMPONENTS: [&str; 4] = ["app", "vst3", "clap", "organ"];

impl Wizard {
    /// A wizard for what `facts` says, acting through `env`.
    pub fn new(facts: Facts, env: Env) -> Wizard {
        let page = if facts.manifest.is_err() {
            Page::Damaged
        } else if facts.uninstall {
            if facts.found.is_some() {
                Page::Remove
            } else if facts.unreadable.is_some() {
                Page::Unreadable
            } else {
                Page::Removed
            }
        } else if facts.found.is_some() {
            Page::Maintain
        } else {
            Page::Welcome
        };
        let scope_locked = facts.unreadable.is_some();
        let mut choices = Choices::defaults(Scope::Machine);
        if let Some(found) = &facts.found {
            choices = choices_of(found);
        } else if let Some((scope, dir)) = &facts.unreadable {
            // Installed over, where it is and for whom.
            choices.scope = *scope;
            choices.program_dir = Some(dir.clone());
        }
        Wizard {
            lang: facts.lang,
            facts,
            env,
            night: false,
            page,
            options_from: Page::Welcome,
            choices,
            scope_locked,
            accepted: false,
            scroll: 0.0,
            job: None,
            removing: false,
            exit: None,
            failure: None,
            last_log: None,
            open_after: true,
            purge: false,
            sheet: false,
            details: true,
            hint: None,
            controls: Vec::new(),
            focus: None,
            pressed: None,
            changes: 0,
            drawn: None,
            asked_focus: false,
        }
    }

    /// The page shown.
    pub fn page(&self) -> Page {
        self.page
    }

    fn look(&self) -> &'static Look {
        if self.night { &NIGHT } else { &DAY }
    }

    fn t(&self) -> &'static Text {
        self.lang.text()
    }

    fn product(&self) -> &str {
        self.facts
            .manifest
            .as_ref()
            .map_or("Caecilia", |m| m.product.as_str())
    }

    fn version(&self) -> &str {
        self.facts
            .manifest
            .as_ref()
            .map_or("", |m| m.version.as_str())
    }

    fn changed(&mut self) {
        self.changes += 1;
    }

    fn go(&mut self, page: Page) {
        self.page = page;
        self.focus = None;
        self.hint = None;
        self.changed();
    }

    fn program_dir(&self) -> PathBuf {
        self.choices.program_dir.clone().unwrap_or_else(|| {
            self.facts
                .places
                .program_dir(self.choices.scope, self.product())
        })
    }

    fn needs_admin(&self, scope: Scope) -> bool {
        scope == Scope::Machine && !self.facts.elevated
    }

    /// Where each component goes with the present choices.
    fn component_path(&self, i: usize) -> PathBuf {
        let places = &self.facts.places;
        let scope = self.choices.scope;
        let product = self.product();
        match COMPONENTS[i] {
            "app" => self.program_dir(),
            "vst3" => places
                .plugin_dir(scope, "VST3")
                .join(format!("{product}.vst3")),
            "clap" => places
                .plugin_dir(scope, "CLAP")
                .join(format!("{product}.clap")),
            _ => self
                .choices
                .organs_dir
                .clone()
                .unwrap_or_else(|| places.organs_dir(product)),
        }
    }

    fn component_on(&self, i: usize) -> bool {
        let c = &self.choices.components;
        [c.app, c.vst3, c.clap, c.organ][i]
    }

    fn component_name(&self, i: usize) -> (&'static str, &'static str) {
        let t = self.t();
        [t.app, t.vst3, t.clap, t.organ][i]
    }

    /// The bytes the present choices place.
    fn bytes_needed(&self) -> u64 {
        let Ok(m) = &self.facts.manifest else {
            return 0;
        };
        let mut seen = std::collections::HashSet::new();
        m.entries
            .iter()
            .filter(|e| self.choices.components.includes(&e.component))
            .filter(|e| seen.insert((e.root, e.path.clone())))
            .map(|e| e.size)
            .sum::<u64>()
            + self.facts.self_size
    }

    fn new_log(&self, what: &str) -> PathBuf {
        let stamp = crate::log::stamp(std::time::SystemTime::now());
        self.facts
            .log_dir
            .join(format!("{}-{what}-{stamp}.log", self.product()))
    }

    // ---- what the controls do ----

    fn act(&mut self, act: Act) {
        match act {
            Act::Next => self.go(Page::Licence),
            Act::Back => {
                let to = match self.page {
                    Page::Licence => Page::Welcome,
                    Page::Options => self.options_from,
                    _ => Page::Welcome,
                };
                self.go(to);
            }
            Act::Options => {
                self.options_from = self.page;
                self.go(Page::Options);
            }
            Act::Install => {
                if self.choices.components.is_empty() {
                    self.hint = Some(|t| t.choose_something);
                    self.changed();
                } else if !self.accepted {
                    if self.page == Page::Licence {
                        self.hint = Some(|t| t.licence_needed);
                        self.changed();
                    } else {
                        self.go(Page::Licence);
                    }
                } else {
                    self.start_install();
                }
            }
            Act::Accept => {
                self.accepted = !self.accepted;
                self.hint = None;
                self.changed();
            }
            Act::Scope(scope) => {
                if !self.scope_locked {
                    self.choices.scope = scope;
                    self.choices.program_dir = None;
                    self.changed();
                }
            }
            Act::Component(i) => {
                let c = &mut self.choices.components;
                let flag = [&mut c.app, &mut c.vst3, &mut c.clap, &mut c.organ];
                *flag[i] = !*flag[i];
                self.hint = None;
                self.changed();
            }
            Act::Desktop => {
                self.choices.desktop = !self.choices.desktop;
                self.changed();
            }
            Act::StartMenu => {
                self.choices.start_menu = !self.choices.start_menu;
                self.changed();
            }
            Act::PickFolder => self.pick_folder(),
            Act::Cancel => {
                if let Some(job) = &self.job {
                    if job.exit.is_none() {
                        job.cancel();
                    }
                }
                self.changed();
            }
            Act::Finish => {
                if self.open_after && self.choices.components.app {
                    (self.env.open)(&self.program_dir().join(APP_EXE));
                }
                (self.env.quit)();
            }
            Act::Close => (self.env.quit)(),
            Act::Retry => {
                self.sheet = false;
                self.act(Act::Install);
            }
            Act::CloseSheet => {
                self.sheet = false;
                self.changed();
            }
            Act::Update => {
                if let Some(found) = &self.facts.found {
                    self.choices = choices_of(found);
                }
                self.act(Act::Install);
            }
            Act::Modify => {
                self.scope_locked = true;
                self.options_from = Page::Maintain;
                self.go(Page::Options);
            }
            Act::AskRemove => self.go(Page::Remove),
            Act::Remove => self.start_uninstall(),
            Act::Purge => {
                self.purge = !self.purge;
                self.changed();
            }
            Act::OpenAfter => {
                self.open_after = !self.open_after;
                self.changed();
            }
            Act::Details => {
                self.details = !self.details;
                self.changed();
            }
            Act::ShowLog => {
                if let Some(log) = &self.last_log {
                    (self.env.open)(log);
                }
            }
            Act::Night => {
                self.night = !self.night;
                self.changed();
            }
            Act::Lang(lang) => {
                self.lang = lang;
                self.changed();
            }
            Act::JustMe => {
                self.choices.scope = Scope::User;
                self.choices.program_dir = None;
                self.failure = None;
                self.options_from = Page::Licence;
                self.go(Page::Options);
            }
        }
    }

    fn pick_folder(&mut self) {
        let dialogs = self.env.dialogs.borrow().clone();
        let Some(dialogs) = dialogs else {
            return;
        };
        let current = self.program_dir();
        let start = current.parent().map(Path::to_owned).unwrap_or(current);
        let dialog = FileDialog::new()
            .with_title(self.t().pick_folder)
            .in_folder(start);
        if let Ok(Some(dir)) = dialogs.pick_folder(&dialog) {
            // A folder of its own inside the one picked, unless it already
            // is one.
            let product = self.product().to_owned();
            let dir = if dir
                .file_name()
                .is_some_and(|n| n.eq_ignore_ascii_case(&product))
            {
                dir
            } else {
                dir.join(product)
            };
            if self.choices.scope == Scope::Machine && !self.facts.places.protects(&dir) {
                self.hint = Some(|t| t.program_files_only);
            } else {
                self.hint = None;
                self.choices.program_dir = Some(dir);
            }
            self.changed();
        }
    }

    /// The data folder of the person who installed `r`: what a purge
    /// removes.
    fn data_dir(&self, r: &crate::record::Record) -> PathBuf {
        r.data_dir
            .clone()
            .unwrap_or_else(|| self.facts.places.user_data_dir(&r.product))
    }

    fn start_install(&mut self) {
        let exe = self.program_dir().join(APP_EXE);
        if (self.env.in_use)(&exe) {
            self.sheet = true;
            self.changed();
            return;
        }
        let mut choices = self.choices.clone();
        choices.program_dir = Some(self.program_dir());
        // The person's own organs folder, whoever the work runs as.
        choices.organs_dir = Some(self.component_path(3));
        let log = self.new_log("Setup");
        let mut args = Args::for_engine_install(&choices, &log, self.lang);
        if self.facts.found.is_none() && self.facts.unreadable.is_some() {
            // Over the installation whose record is lost: a repair.
            args.push("--force".into());
        }
        let elevate = self.needs_admin(choices.scope);
        self.removing = false;
        self.start(args, elevate, log);
    }

    fn start_uninstall(&mut self) {
        let Some(found) = &self.facts.found else {
            return;
        };
        let purge = self.purge.then(|| self.data_dir(&found.record));
        let log = self.new_log("Uninstall");
        let args = Args::for_engine_uninstall(&found.record.program_dir, purge.as_deref(), &log);
        let elevate = self.needs_admin(found.scope);
        self.removing = true;
        self.start(args, elevate, log);
    }

    fn start(&mut self, args: Vec<String>, elevate: bool, log: PathBuf) {
        self.last_log = Some(log.clone());
        self.exit = None;
        match self.env.launcher.start(&args, elevate) {
            Ok(running) => {
                self.job = Some(Job::new(running, &log));
                self.env.busy.set(true);
                self.go(Page::Progress);
            }
            Err(StartError::Declined) => {
                self.failure = Some(Failure::Declined);
                self.go(Page::Failed);
            }
            Err(StartError::Failed(e)) => {
                self.failure = Some(Failure::Error(e));
                self.go(Page::Failed);
            }
        }
    }

    /// Follows the work. Returns whether anything changed.
    fn poll(&mut self) -> bool {
        let Some(job) = &mut self.job else {
            return false;
        };
        if !job.poll() {
            return false;
        }
        if let Some(code) = job.exit {
            let error = job.progress.error.clone();
            self.exit = Some(code);
            self.env.busy.set(false);
            match code {
                crate::exit::OK | crate::exit::RESTART => {
                    self.go(if self.removing {
                        Page::Removed
                    } else {
                        Page::Done
                    });
                }
                crate::exit::CANCELLED => {
                    self.failure = Some(Failure::Cancelled);
                    self.go(Page::Failed);
                }
                code => {
                    self.failure = Some(Failure::Error(
                        error.unwrap_or_else(|| format!("exit code {code}")),
                    ));
                    self.go(Page::Failed);
                }
            }
        }
        self.changed();
        true
    }

    // ---- drawing, and where the controls are ----

    fn draw(&self, scene: &mut Scene, r: Rect) -> Vec<Control> {
        let look = self.look();
        let t = self.t();
        let mut out = Vec::new();
        scene.fill(r, look.ground);
        self.draw_jamb(
            scene,
            Rect::new(r.x + 12.0, r.y + 12.0, 216.0, r.height - 24.0),
            &mut out,
        );
        let panel = Rect::new(r.x + 240.0, r.y + 12.0, r.width - 252.0, r.height - 24.0);
        paint::panel(scene, look, panel);
        let mut p = Pane {
            scene,
            look,
            x: panel.x + 26.0,
            y: panel.y + 22.0,
            w: panel.width - 52.0,
            bottom: panel.bottom() - 18.0,
            out: &mut out,
            focus: self.focus,
            pressed: self.pressed,
        };
        match self.page {
            Page::Welcome => self.welcome(&mut p),
            Page::Licence => self.licence(&mut p),
            Page::Options => self.options(&mut p),
            Page::Progress => self.progress(&mut p),
            Page::Done => self.done(&mut p),
            Page::Failed => self.failed(&mut p),
            Page::Maintain => self.maintain(&mut p),
            Page::Remove => self.remove(&mut p),
            Page::Removed => self.removed(&mut p),
            Page::Unreadable => {
                let dir = self
                    .facts
                    .unreadable
                    .as_ref()
                    .map(|(_, d)| d.display().to_string())
                    .unwrap_or_default();
                p.title(t.unreadable_title);
                p.lead(&fill(t.unreadable_lead, &[&dir]));
                p.actions(&[], &[(t.close, Act::Close, Button::Primary, false)]);
            }
            Page::Damaged => {
                p.title(t.damaged_title);
                p.lead(t.damaged_lead);
                let msg = self
                    .facts
                    .manifest
                    .as_ref()
                    .err()
                    .cloned()
                    .unwrap_or_default();
                p.note(&msg);
                p.actions(&[], &[(t.close, Act::Close, Button::Primary, false)]);
            }
        }
        if self.sheet {
            let base = p.out.len();
            self.draw_sheet(&mut p, panel);
            // Only the sheet's controls while it is up.
            p.out.drain(..base);
        }
        out
    }

    fn draw_jamb(&self, scene: &mut Scene, rect: Rect, out: &mut Vec<Control>) {
        let look = self.look();
        let t = self.t();
        paint::jamb(scene, look, rect);
        paint::plate(
            scene,
            look,
            Rect::new(rect.x + 18.0, rect.y + 20.0, rect.width - 36.0, 50.0),
            &self.product().to_uppercase(),
            self.version(),
        );
        let steps: Vec<(&str, StepState)> =
            if matches!(self.page, Page::Remove | Page::Removed | Page::Unreadable)
                || (self.removing && matches!(self.page, Page::Progress | Page::Failed))
            {
                vec![(t.step_remove, StepState::Now)]
            } else {
                let now = match self.page {
                    Page::Welcome | Page::Maintain | Page::Damaged => 0,
                    Page::Licence => 1,
                    Page::Options => 2,
                    Page::Progress | Page::Failed => 3,
                    Page::Done => 4,
                    _ => 0,
                };
                t.steps
                    .iter()
                    .enumerate()
                    .map(|(i, s)| {
                        let state = match i.cmp(&now) {
                            std::cmp::Ordering::Less => StepState::Done,
                            std::cmp::Ordering::Equal => StepState::Now,
                            std::cmp::Ordering::Greater => StepState::Later,
                        };
                        (*s, state)
                    })
                    .collect()
            };
        for (i, (label, state)) in steps.iter().enumerate() {
            let r = Rect::new(
                rect.x + 16.0,
                rect.y + 92.0 + i as f32 * 43.0,
                rect.width - 32.0,
                34.0,
            );
            paint::step(scene, look, r, &(i + 1).to_string(), label, *state);
        }
        // The foot: language and light, the licence, the credits.
        let foot = rect.bottom() - 132.0;
        let busy = self.page == Page::Progress;
        for (i, lang) in Lang::ALL.into_iter().enumerate() {
            let r = Rect::new(rect.x + 18.0 + i as f32 * 38.0, foot, 34.0, 22.0);
            let on = lang == self.lang;
            pill(scene, look, r, &lang.code().to_uppercase(), on);
            if !busy {
                out.push(Control {
                    rect: r,
                    act: Act::Lang(lang),
                    role: Role::Button,
                    name: format!("{} {}", t.language, lang.code().to_uppercase()),
                    checked: Some(on),
                    enabled: true,
                    default: false,
                });
            }
        }
        let r = Rect::new(
            rect.x + 18.0 + 3.0 * 38.0,
            foot,
            rect.width - 36.0 - 3.0 * 38.0,
            22.0,
        );
        let to = if self.night {
            t.night_day.1
        } else {
            t.night_day.0
        };
        pill(scene, look, r, to, false);
        out.push(Control {
            rect: r,
            act: Act::Night,
            role: Role::Button,
            name: to.into(),
            checked: None,
            enabled: true,
            default: false,
        });
        let w = rect.width - 36.0;
        let mut y = foot + 34.0;
        y += paint::paragraph(
            scene,
            t.free,
            style(10.5, 600.0, look.on_wood),
            Point::new(rect.x + 18.0, y),
            w,
        ) + 4.0;
        y += paint::paragraph(
            scene,
            t.built_with,
            style(10.5, 450.0, look.on_wood),
            Point::new(rect.x + 18.0, y),
            w,
        ) + 4.0;
        paint::paragraph(
            scene,
            t.copyright,
            style(9.5, 450.0, look.on_wood.with_alpha(0xC0)),
            Point::new(rect.x + 18.0, y),
            w,
        );
    }

    fn welcome(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.welcome_title);
        p.lead(t.welcome_lead);
        p.section(t.will_install);
        self.component_rows(p, false);
        let dir = self.program_dir().display().to_string();
        let line = if self.choices.scope == Scope::Machine {
            t.for_everyone
        } else {
            t.for_me
        };
        p.y += 6.0;
        p.note(&fill(line, &[&dir]));
        if self.facts.version_1 {
            p.note(t.old_version);
        }
        p.actions(
            &[(t.options, Act::Options, Button::Quiet, false)],
            &[(t.next, Act::Next, Button::Primary, false)],
        );
    }

    fn licence(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.licence_title);
        p.lead(t.licence_lead);
        let box_bottom = p.bottom - 34.0 - 54.0;
        let rect = Rect::new(p.x, p.y + 4.0, p.w, box_bottom - p.y - 4.0);
        paint::field(p.scene, p.look, rect);
        let s = style(11.0, 450.0, p.look.text2);
        let inner = p.w - 28.0;
        let full = paint::height_of(&self.facts.licence, s, inner);
        let max = (full - (rect.height - 20.0)).max(0.0);
        let offset = self.scroll.clamp(0.0, max);
        let text = self.facts.licence.as_str();
        p.scene.clipped(
            RoundedRect::new(
                Rect::new(
                    rect.x + 1.0,
                    rect.y + 1.0,
                    rect.width - 2.0,
                    rect.height - 2.0,
                ),
                5.0,
            ),
            |scene| {
                paint::paragraph(
                    scene,
                    text,
                    s,
                    Point::new(rect.x + 12.0, rect.y + 10.0 - offset),
                    inner,
                );
            },
        );
        if max > 0.0 {
            let track = rect.height - 12.0;
            let thumb = (track * (rect.height / full)).max(24.0);
            let at = rect.y + 6.0 + (track - thumb) * (offset / max);
            p.scene.fill(
                RoundedRect::new(Rect::new(rect.right() - 8.0, at, 4.0, thumb), 2.0),
                p.look.muted.with_alpha(0x90),
            );
        }
        p.out.push(Control {
            rect,
            act: Act::Details, // never pressed: the box only scrolls
            role: Role::ScrollView,
            name: t.licence_title.into(),
            checked: None,
            enabled: false,
            default: false,
        });
        p.y = box_bottom + 12.0;
        p.check_row(t.licence_accept, self.accepted, Act::Accept);
        let shield = self.needs_admin(self.choices.scope);
        if let Some(hint) = self.hint {
            p.hint(hint(t));
        }
        p.actions(
            &[
                (t.back, Act::Back, Button::Quiet, false),
                (t.options, Act::Options, Button::Quiet, false),
            ],
            &[(t.install, Act::Install, Button::Primary, shield)],
        );
        if !self.accepted {
            if let Some(c) = p.out.iter_mut().rev().find(|c| c.act == Act::Install) {
                c.enabled = false;
            }
        }
    }

    fn options(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.options_title);
        p.section(t.who);
        let half = (p.w - 10.0) / 2.0;
        for (i, (scope, (name, about))) in [(Scope::Machine, t.everyone), (Scope::User, t.just_me)]
            .into_iter()
            .enumerate()
        {
            let rect = Rect::new(p.x + i as f32 * (half + 10.0), p.y, half, 74.0);
            let chosen = self.choices.scope == scope;
            paint::card(p.scene, p.look, rect, chosen);
            let mut x = rect.x + 12.0;
            if scope == Scope::Machine {
                paint::uac_shield(p.scene, Rect::new(x, rect.y + 10.0, 11.0, 13.0));
                x += 17.0;
            }
            paint::text(
                p.scene,
                name,
                style(13.0, 650.0, p.look.text),
                Point::new(x, rect.y + 9.0),
                0.0,
                Some(rect.right() - x - 10.0),
            );
            let mut s = style(11.0, 450.0, p.look.muted);
            s.max_lines = Some(3);
            paint::paragraph(
                p.scene,
                about,
                s,
                Point::new(rect.x + 12.0, rect.y + 28.0),
                rect.width - 24.0,
            );
            if self.scope_locked && !chosen {
                p.scene.fill(
                    RoundedRect::new(rect, 10.0),
                    p.look.panel_bottom.with_alpha(0xA0),
                );
            }
            p.push(
                rect,
                Act::Scope(scope),
                Role::Switch,
                name,
                Some(chosen),
                !self.scope_locked,
                10.0,
            );
        }
        p.y += 80.0;
        if self.scope_locked {
            p.note(t.scope_fixed);
        }
        p.section(t.what);
        self.component_rows(p, true);
        p.section(t.folder);
        let field = Rect::new(p.x, p.y, p.w - 112.0, 32.0);
        paint::field(p.scene, p.look, field);
        let dir = self.program_dir().display().to_string();
        paint::text(
            p.scene,
            &dir,
            style(12.0, 500.0, p.look.text),
            Point::new(field.x + 10.0, field.y + 8.0),
            0.0,
            Some(field.width - 20.0),
        );
        let change = Rect::new(p.x + p.w - 102.0, p.y, 102.0, 32.0);
        let locked = self.scope_locked;
        p.button(
            change,
            t.change,
            Act::PickFolder,
            Button::Secondary,
            false,
            !locked,
            false,
        );
        p.y += 42.0;
        // Shortcuts, and the space needed.
        let mut x = p.x;
        for (label, on, act) in [
            (t.desktop, self.choices.desktop, Act::Desktop),
            (t.start_menu, self.choices.start_menu, Act::StartMenu),
        ] {
            let w = p.check_at(x, label, on, act, self.choices.components.app);
            x += w + 18.0;
        }
        paint::text(
            p.scene,
            &fill(t.needs, &[&size(self.bytes_needed())]),
            style(11.5, 450.0, p.look.muted),
            Point::new(p.x + p.w, p.y + 2.0),
            1.0,
            None,
        );
        p.y += 26.0;
        if let Some(hint) = self.hint {
            p.hint(hint(t));
        }
        let shield = self.needs_admin(self.choices.scope);
        let (label, act, shield) = if self.accepted {
            (t.install, Act::Install, shield)
        } else {
            (t.next, Act::Install, false)
        };
        p.actions(
            &[(t.back, Act::Back, Button::Quiet, false)],
            &[(label, act, Button::Primary, shield)],
        );
    }

    fn component_rows(&self, p: &mut Pane<'_>, interactive: bool) {
        let rows = 4;
        let card = Rect::new(p.x, p.y, p.w, rows as f32 * 28.0 + 12.0);
        if !interactive {
            paint::card(p.scene, p.look, card, false);
        }
        for i in 0..rows {
            let y = card.y + 6.0 + i as f32 * 28.0;
            let row = Rect::new(card.x + 6.0, y, card.width - 12.0, 28.0);
            let on = self.component_on(i);
            paint::knob(
                p.scene,
                p.look,
                Point::new(row.x + 14.0, row.center().y),
                on,
            );
            let (name, about) = self.component_name(i);
            let ns = style(13.0, 620.0, p.look.text);
            let w = paint::width_of(name, ns);
            paint::text(
                p.scene,
                name,
                ns,
                Point::new(row.x + 34.0, y + 6.0),
                0.0,
                None,
            );
            let path_w = 180.0;
            let room = row.width - 34.0 - w - 6.0 - path_w - 12.0;
            if room > 30.0 {
                paint::text(
                    p.scene,
                    about,
                    style(11.0, 450.0, p.look.muted),
                    Point::new(row.x + 40.0 + w, y + 8.0),
                    0.0,
                    Some(room),
                );
            }
            let path = self.component_path(i).display().to_string();
            paint::text(
                p.scene,
                &path,
                style(10.5, 450.0, p.look.muted),
                Point::new(row.right() - 4.0, y + 8.0),
                1.0,
                Some(path_w),
            );
            if interactive {
                p.push(
                    row,
                    Act::Component(i),
                    Role::Switch,
                    name,
                    Some(on),
                    true,
                    6.0,
                );
            }
        }
        p.y = card.bottom() + 4.0;
    }

    fn progress(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(if self.removing {
            t.removing_title
        } else {
            t.installing_title
        });
        let progress = self
            .job
            .as_ref()
            .map(|j| j.progress.clone())
            .unwrap_or_default();
        let c = Point::new(p.x + 84.0, p.y + 92.0);
        paint::gauge(p.scene, p.look, c, 80.0, progress.fraction(), t.wind);
        let step = progress.step.as_deref().unwrap_or("");
        let words = t
            .step_names
            .iter()
            .find(|(k, _)| *k == step)
            .map_or(t.step_names[10].1, |(_, w)| *w);
        let x = p.x + 190.0;
        let w = p.w - 190.0;
        let mut y = p.y + 50.0;
        y += paint::paragraph(
            p.scene,
            words,
            style(15.0, 620.0, p.look.text),
            Point::new(x, y),
            w,
        ) + 6.0;
        if let Some(path) = &progress.path {
            let mut s = style(10.5, 450.0, p.look.muted);
            s.max_lines = Some(3);
            paint::paragraph(p.scene, path, s, Point::new(x, y), w);
        }
        p.y += 190.0;
        let size = paint::text(
            p.scene,
            t.details,
            style(12.0, 600.0, p.look.accent),
            Point::new(p.x + 14.0, p.y),
            0.0,
            None,
        );
        paint::fold(
            p.scene,
            Point::new(p.x + 4.0, p.y + size.height / 2.0),
            self.details,
            p.look.accent,
        );
        p.push(
            Rect::new(p.x - 2.0, p.y - 2.0, size.width + 18.0, size.height + 4.0),
            Act::Details,
            Role::Button,
            t.details,
            Some(self.details),
            true,
            4.0,
        );
        p.y += size.height + 6.0;
        if self.details {
            let rect = Rect::new(p.x, p.y, p.w, p.bottom - 34.0 - 12.0 - p.y);
            paint::field(p.scene, p.look, rect);
            let s = style(10.5, 450.0, p.look.text2);
            let lines = ((rect.height - 12.0) / 15.0).floor().max(1.0) as usize;
            let shown = &progress.shown[progress.shown.len().saturating_sub(lines)..];
            for (i, line) in shown.iter().enumerate() {
                paint::text(
                    p.scene,
                    line,
                    s,
                    Point::new(rect.x + 10.0, rect.y + 6.0 + i as f32 * 15.0),
                    0.0,
                    Some(rect.width - 20.0),
                );
            }
        }
        let can_cancel = !self.removing && self.job.as_ref().is_some_and(|j| j.exit.is_none());
        p.actions(&[], &[(t.cancel, Act::Cancel, Button::Secondary, false)]);
        if !can_cancel {
            if let Some(c) = p.out.iter_mut().rev().find(|c| c.act == Act::Cancel) {
                c.enabled = false;
            }
        }
    }

    fn placed_rows(&self, p: &mut Pane<'_>, rows: &[(String, String)], placed: bool, color: Color) {
        if rows.is_empty() {
            return;
        }
        let card = Rect::new(p.x, p.y, p.w, rows.len() as f32 * 24.0 + 14.0);
        paint::card(p.scene, p.look, card, false);
        for (i, (name, path)) in rows.iter().enumerate() {
            let y = card.y + 8.0 + i as f32 * 24.0;
            if placed {
                paint::tick(
                    p.scene,
                    Rect::new(card.x + 11.0, y + 3.0, 11.0, 11.0),
                    color,
                    2.0,
                );
            } else {
                p.scene.fill(
                    RoundedRect::new(Rect::new(card.x + 12.0, y + 8.0, 9.0, 2.2), 1.0),
                    color,
                );
            }
            paint::text(
                p.scene,
                name,
                style(12.5, 600.0, p.look.text),
                Point::new(card.x + 32.0, y + 2.0),
                0.0,
                Some(150.0),
            );
            paint::text(
                p.scene,
                path,
                style(10.5, 450.0, p.look.muted),
                Point::new(card.x + 190.0, y + 4.0),
                0.0,
                Some(card.width - 200.0),
            );
        }
        p.y = card.bottom() + 8.0;
    }

    fn done(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.done_title);
        p.lead(t.done_lead);
        let rows: Vec<(String, String)> = (0..4)
            .filter(|&i| self.component_on(i))
            .map(|i| {
                (
                    self.component_name(i).0.to_owned(),
                    self.component_path(i).display().to_string(),
                )
            })
            .collect();
        self.placed_rows(p, &rows, true, p.look.good);
        let c = &self.choices.components;
        if c.vst3 || c.clap {
            p.callout(t.rescan);
        }
        if self.exit == Some(crate::exit::RESTART) {
            p.note(t.restart_note);
        }
        let mut skip = 0.0;
        if c.app {
            // On the line of the buttons, at their left.
            let y = p.y;
            p.y = p.bottom - 26.0;
            skip = p.check_at(p.x, t.open_now, self.open_after, Act::OpenAfter, true) + 20.0;
            p.y = y;
        }
        p.actions_from(
            skip,
            &[(t.show_log, Act::ShowLog, Button::Quiet, false)],
            &[(t.finish, Act::Finish, Button::Primary, false)],
        );
    }

    fn failed(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.failed_title);
        let mut right = vec![(t.close, Act::Close, Button::Primary, false)];
        match &self.failure {
            Some(Failure::Cancelled) => p.lead(t.cancelled_lead),
            Some(Failure::Declined) => {
                p.lead(t.declined_lead);
                right.insert(
                    0,
                    (t.just_me_instead, Act::JustMe, Button::Secondary, false),
                );
            }
            Some(Failure::Error(e)) => {
                p.lead(t.failed_lead);
                p.note(e);
            }
            None => {}
        }
        let left: Vec<_> = if self.last_log.as_ref().is_some_and(|l| l.exists()) {
            vec![(t.show_log, Act::ShowLog, Button::Quiet, false)]
        } else {
            vec![]
        };
        p.actions(&left, &right);
    }

    fn maintain(&self, p: &mut Pane<'_>) {
        let t = self.t();
        let Some(found) = &self.facts.found else {
            return;
        };
        let installed = found.record.version.as_str();
        let who = if found.scope == Scope::Machine {
            t.scope_words.0
        } else {
            t.scope_words.1
        };
        p.title(t.installed_title);
        p.lead(&fill(t.installed_lead, &[installed, who, self.version()]));
        let order = version::compare(self.version(), installed);
        if order == std::cmp::Ordering::Less {
            p.note(&fill(t.newer_lead, &[installed]));
        }
        let mut tiles: Vec<(String, &str, Act, bool)> = Vec::new();
        match order {
            std::cmp::Ordering::Greater => tiles.push((
                fill(t.update.0, &[self.version()]),
                t.update.1,
                Act::Update,
                true,
            )),
            std::cmp::Ordering::Equal => {
                tiles.push((t.repair.0.into(), t.repair.1, Act::Update, true))
            }
            std::cmp::Ordering::Less => {}
        }
        if order != std::cmp::Ordering::Less {
            tiles.push((t.modify.0.into(), t.modify.1, Act::Modify, false));
        }
        tiles.push((t.uninstall.0.into(), t.uninstall.1, Act::AskRemove, false));
        let w = (p.w - 20.0) / 3.0;
        p.y += 6.0;
        for (i, (name, about, act, main)) in tiles.iter().enumerate() {
            let rect = Rect::new(p.x + i as f32 * (w + 10.0), p.y, w, 104.0);
            paint::card(p.scene, p.look, rect, *main);
            let mut s = style(13.5, 650.0, p.look.text);
            s.max_lines = Some(2);
            let h = paint::paragraph(
                p.scene,
                name,
                s,
                Point::new(rect.x + 12.0, rect.y + 12.0),
                rect.width - 24.0,
            );
            let mut s = style(11.0, 450.0, p.look.muted);
            s.max_lines = Some(3);
            paint::paragraph(
                p.scene,
                about,
                s,
                Point::new(rect.x + 12.0, rect.y + 16.0 + h),
                rect.width - 24.0,
            );
            p.push(rect, *act, Role::Button, name, None, true, 10.0);
        }
        p.y += 114.0;
        if self.facts.version_1 {
            p.note(t.old_version);
        }
        p.actions(&[], &[(t.close, Act::Close, Button::Secondary, false)]);
    }

    fn remove(&self, p: &mut Pane<'_>) {
        let t = self.t();
        let Some(found) = &self.facts.found else {
            return;
        };
        p.title(t.remove_title);
        p.lead(t.remove_lead);
        let r = &found.record;
        let find = |needle: &str| {
            r.files
                .iter()
                .find(|f| f.path.to_string_lossy().to_lowercase().contains(needle))
                .map(|f| f.path.display().to_string())
        };
        let mut rows = Vec::new();
        let names = [t.app.0, t.vst3.0, t.clap.0, t.organ.0];
        let on = [
            r.components.app,
            r.components.vst3,
            r.components.clap,
            r.components.organ,
        ];
        for i in 0..4 {
            if !on[i] {
                continue;
            }
            let path = match i {
                0 => Some(r.program_dir.display().to_string()),
                1 => find(".vst3"),
                2 => find(".clap"),
                _ => find(".organ.json"),
            };
            rows.push((names[i].to_owned(), path.unwrap_or_default()));
        }
        if !on[0] {
            rows.insert(
                0,
                (
                    self.product().to_owned(),
                    r.program_dir.display().to_string(),
                ),
            );
        }
        self.placed_rows(p, &rows, false, p.look.set_red);
        p.y += 4.0;
        p.check_row(t.purge.0, self.purge, Act::Purge);
        let data = self.data_dir(r).display().to_string();
        p.note_indented(&format!("{data} — {}", t.purge.1));
        let shield = self.needs_admin(found.scope);
        let keep = if self.facts.uninstall {
            Act::Close
        } else {
            Act::Back
        };
        p.actions(
            &[],
            &[
                (t.keep, keep, Button::Secondary, false),
                (t.remove, Act::Remove, Button::Danger, shield),
            ],
        );
    }

    fn removed(&self, p: &mut Pane<'_>) {
        let t = self.t();
        p.title(t.removed_title);
        p.lead(t.removed_lead);
        let kept: Vec<(String, String)> = self
            .job
            .as_ref()
            .map(|j| {
                j.progress
                    .shown
                    .iter()
                    .filter_map(|l| l.strip_prefix("kept "))
                    .map(|path| (String::new(), path.to_owned()))
                    .collect()
            })
            .unwrap_or_default();
        if !kept.is_empty() {
            p.note(t.kept);
            let card = Rect::new(p.x, p.y, p.w, kept.len().min(6) as f32 * 20.0 + 12.0);
            paint::card(p.scene, p.look, card, false);
            for (i, (_, path)) in kept.iter().take(6).enumerate() {
                paint::text(
                    p.scene,
                    path,
                    style(10.5, 450.0, p.look.text2),
                    Point::new(card.x + 12.0, card.y + 7.0 + i as f32 * 20.0),
                    0.0,
                    Some(card.width - 24.0),
                );
            }
            p.y = card.bottom() + 8.0;
        }
        if self.exit == Some(crate::exit::RESTART) {
            p.note(t.restart_note);
        }
        p.actions(&[], &[(t.close, Act::Close, Button::Primary, false)]);
    }

    fn draw_sheet(&self, p: &mut Pane<'_>, panel: Rect) {
        let t = self.t();
        p.scene
            .fill(RoundedRect::new(panel, 12.0), Color::hex_rgba(0x0000_0070));
        let card = Rect::new(
            panel.center().x - 200.0,
            panel.center().y - 90.0,
            400.0,
            180.0,
        );
        paint::panel(p.scene, p.look, card);
        let (x, w, bottom) = (p.x, p.w, p.bottom);
        p.x = card.x + 20.0;
        p.w = card.width - 40.0;
        p.y = card.y + 18.0;
        p.bottom = card.bottom() - 16.0;
        p.title(t.busy_title);
        p.lead(t.busy_lead);
        p.actions(
            &[],
            &[
                (t.cancel, Act::CloseSheet, Button::Secondary, false),
                (t.retry, Act::Retry, Button::Primary, false),
            ],
        );
        (p.x, p.w, p.bottom) = (x, w, bottom);
    }

    fn activate(&mut self, i: usize) {
        if let Some(c) = self.controls.get(i).cloned() {
            if c.enabled || c.act == Act::Install {
                self.act(c.act);
            }
        }
    }

    fn hit(&self, at: Point) -> Option<usize> {
        self.controls
            .iter()
            .rposition(|c| c.role != Role::ScrollView && c.rect.contains(at))
    }
}

/// The choices that would install again what `found` has.
fn choices_of(found: &Found) -> Choices {
    let r = &found.record;
    Choices {
        scope: found.scope,
        components: r.components,
        program_dir: Some(r.program_dir.clone()),
        organs_dir: r.organs_dir.clone(),
        desktop: r.desktop,
        start_menu: r.start_menu,
    }
}

fn pill(scene: &mut Scene, look: &Look, rect: Rect, label: &str, on: bool) {
    let shape = RoundedRect::new(rect, rect.height / 2.0);
    if on {
        scene.fill(shape, look.brass);
    } else {
        scene.fill(
            shape,
            Color::hex_rgba(if look.night { 0x0000_0050 } else { 0xFFFF_FF60 }),
        );
    }
    scene.stroke(
        shape,
        manguebyte_gfx::Stroke::new(1.0),
        look.brass.with_alpha(0xA0),
    );
    let s = style(10.5, 700.0, if on { look.plate_ink } else { look.on_wood });
    let h = manguebyte_gfx::TextLayout::new(label, &s, None)
        .size()
        .height;
    paint::text(
        scene,
        label,
        s,
        Point::new(rect.center().x, rect.center().y - h / 2.0),
        0.5,
        Some(rect.width - 8.0),
    );
}

/// The panel being laid out: where the next thing goes, and the controls
/// so far.
struct Pane<'a> {
    scene: &'a mut Scene,
    look: &'static Look,
    x: f32,
    y: f32,
    w: f32,
    bottom: f32,
    out: &'a mut Vec<Control>,
    focus: Option<usize>,
    pressed: Option<usize>,
}

type ActionSpec<'s> = (&'s str, Act, Button, bool);

impl Pane<'_> {
    #[allow(clippy::too_many_arguments)]
    fn push(
        &mut self,
        rect: Rect,
        act: Act,
        role: Role,
        name: &str,
        checked: Option<bool>,
        enabled: bool,
        radius: f32,
    ) {
        if self.focus == Some(self.out.len()) {
            paint::focus_ring(self.scene, self.look, rect, radius);
        }
        self.out.push(Control {
            rect,
            act,
            role,
            name: name.into(),
            checked,
            enabled,
            default: false,
        });
    }

    fn title(&mut self, s: &str) {
        let h = paint::paragraph(
            self.scene,
            s,
            style(22.0, 600.0, self.look.text),
            Point::new(self.x, self.y),
            self.w,
        );
        self.y += h + 6.0;
    }

    fn lead(&mut self, s: &str) {
        let h = paint::paragraph(
            self.scene,
            s,
            style(14.0, 450.0, self.look.text2),
            Point::new(self.x, self.y),
            self.w,
        );
        self.y += h + 10.0;
    }

    fn note(&mut self, s: &str) {
        let h = paint::paragraph(
            self.scene,
            s,
            style(12.0, 450.0, self.look.muted),
            Point::new(self.x, self.y),
            self.w,
        );
        self.y += h + 8.0;
    }

    fn note_indented(&mut self, s: &str) {
        let h = paint::paragraph(
            self.scene,
            s,
            style(11.0, 450.0, self.look.muted),
            Point::new(self.x + 26.0, self.y - 2.0),
            self.w - 26.0,
        );
        self.y += h + 8.0;
    }

    fn hint(&mut self, s: &str) {
        let h = paint::paragraph(
            self.scene,
            s,
            style(12.0, 650.0, self.look.set_red),
            Point::new(self.x, self.y),
            self.w,
        );
        self.y += h + 6.0;
    }

    fn callout(&mut self, s: &str) {
        let s_style = style(12.5, 450.0, self.look.text2);
        let h = paint::height_of(s, s_style, self.w - 22.0);
        let rect = Rect::new(self.x, self.y, self.w, h + 14.0);
        self.scene.fill(
            RoundedRect::new(rect, 6.0),
            self.look.accent.with_alpha(0x1C),
        );
        self.scene.fill(
            Rect::new(rect.x, rect.y, 3.0, rect.height),
            self.look.accent,
        );
        paint::paragraph(
            self.scene,
            s,
            s_style,
            Point::new(rect.x + 14.0, rect.y + 7.0),
            self.w - 22.0,
        );
        self.y = rect.bottom() + 8.0;
    }

    fn section(&mut self, s: &str) {
        let s = s.to_uppercase();
        paint::text(
            self.scene,
            &s,
            style(10.5, 700.0, self.look.muted),
            Point::new(self.x, self.y + 4.0),
            0.0,
            None,
        );
        self.y += 22.0;
    }

    /// A check box and its label at `x` on this line; returns its width.
    fn check_at(&mut self, x: f32, label: &str, on: bool, act: Act, enabled: bool) -> f32 {
        let s = style(
            12.5,
            500.0,
            if enabled {
                self.look.text2
            } else {
                self.look.muted
            },
        );
        paint::check(
            self.scene,
            self.look,
            Rect::new(x, self.y + 1.0, 16.0, 16.0),
            on,
        );
        let size = paint::text(
            self.scene,
            label,
            s,
            Point::new(x + 23.0, self.y + 1.0),
            0.0,
            Some(self.w - 30.0),
        );
        let w = 23.0 + size.width;
        self.push(
            Rect::new(x - 2.0, self.y - 2.0, w + 4.0, 22.0),
            act,
            Role::Switch,
            label,
            Some(on),
            enabled,
            4.0,
        );
        w
    }

    fn check_row(&mut self, label: &str, on: bool, act: Act) {
        let x = self.x;
        self.check_at(x, label, on, act, true);
        self.y += 26.0;
    }

    #[allow(clippy::too_many_arguments)]
    fn button(
        &mut self,
        rect: Rect,
        label: &str,
        act: Act,
        kind: Button,
        shield: bool,
        enabled: bool,
        default: bool,
    ) {
        let pressed = self.pressed == Some(self.out.len());
        paint::button(
            self.scene, self.look, rect, label, kind, shield, enabled, pressed,
        );
        self.push(rect, act, Role::Button, label, None, enabled, 6.0);
        if let Some(c) = self.out.last_mut() {
            c.default = default;
        }
    }

    fn button_width(label: &str, shield: bool) -> f32 {
        (paint::width_of(label, style(13.0, 620.0, Color::BLACK))
            + 36.0
            + if shield { 20.0 } else { 0.0 })
        .max(96.0)
    }

    /// The row of buttons at the bottom: `left` from the left edge,
    /// `right` from the right edge, the last on the right the default.
    fn actions(&mut self, left: &[ActionSpec<'_>], right: &[ActionSpec<'_>]) {
        self.actions_from(0.0, left, right);
    }

    fn actions_from(&mut self, skip: f32, left: &[ActionSpec<'_>], right: &[ActionSpec<'_>]) {
        let y = self.bottom - 34.0;
        let mut x = self.x + skip;
        for (label, act, kind, shield) in left {
            let w = Pane::button_width(label, *shield);
            self.button(
                Rect::new(x, y, w, 34.0),
                label,
                *act,
                *kind,
                *shield,
                true,
                false,
            );
            x += w + 10.0;
        }
        let widths: Vec<f32> = right
            .iter()
            .map(|(l, _, _, s)| Pane::button_width(l, *s))
            .collect();
        let mut x = self.x + self.w
            - widths.iter().sum::<f32>()
            - 10.0 * (widths.len().saturating_sub(1)) as f32;
        for (i, (label, act, kind, shield)) in right.iter().enumerate() {
            let last = i + 1 == right.len();
            self.button(
                Rect::new(x, y, widths[i], 34.0),
                label,
                *act,
                *kind,
                *shield,
                true,
                last,
            );
            x += widths[i] + 10.0;
        }
    }
}

impl Widget for Wizard {
    fn access(&self, _cx: &AccessCx<'_>) -> AccessNode {
        let t = self.t();
        let title = match self.page {
            Page::Welcome => t.welcome_title,
            Page::Licence => t.licence_title,
            Page::Options => t.options_title,
            Page::Progress if self.removing => t.removing_title,
            Page::Progress => t.installing_title,
            Page::Done => t.done_title,
            Page::Failed => t.failed_title,
            Page::Maintain => t.installed_title,
            Page::Remove => t.remove_title,
            Page::Removed => t.removed_title,
            Page::Unreadable => t.unreadable_title,
            Page::Damaged => t.damaged_title,
        };
        AccessNode::new(Role::Dialog, fill(t.title, &[self.version()]))
            .with_value(title)
            .with_actions(Actions {
                focus: true,
                ..Actions::NONE
            })
    }

    fn access_items(&self, _cx: &AccessCx<'_>, items: &mut Vec<AccessItem>) {
        for (key, c) in self.controls.iter().enumerate() {
            let mut node = AccessNode::new(c.role, c.name.as_str());
            if c.role != Role::ScrollView {
                node = node.with_actions(Actions::PRESS);
            }
            if let Some(on) = c.checked {
                node = node.with_checked(on);
            }
            if !c.enabled {
                node = node.disabled();
            }
            items.push(AccessItem::new(key, node, c.rect));
        }
    }

    fn act_item(&mut self, cx: &mut EventCx<'_>, key: usize, action: Action) -> bool {
        if action != Action::Press {
            return false;
        }
        self.activate(key);
        cx.request_paint();
        true
    }

    fn measure(&mut self, _cx: &MeasureCx<'_>) -> Size {
        SIZE
    }

    fn update(&mut self, cx: &mut UpdateCx<'_>) {
        if !self.asked_focus {
            self.asked_focus = true;
            cx.request_focus();
        }
        self.poll();
        if self.drawn != Some(self.changes) {
            cx.request_paint();
        }
    }

    fn paint(&mut self, cx: &PaintCx<'_>, scene: &mut Scene) {
        self.controls = self.draw(scene, cx.rect());
        self.drawn = Some(self.changes);
    }

    fn event(&mut self, cx: &mut EventCx<'_>, event: &Event) -> Handled {
        let before = self.changes;
        let handled = match event {
            Event::PointerDown { at, .. } => {
                self.pressed = self.hit(*at);
                cx.capture_pointer();
                self.changed();
                Handled::Yes
            }
            Event::PointerUp { at, .. } => {
                let pressed = self.pressed.take();
                self.changed();
                if let Some(i) = pressed.filter(|&i| self.hit(*at) == Some(i)) {
                    self.focus = None;
                    self.activate(i);
                }
                Handled::Yes
            }
            Event::Wheel { at, delta, .. } if self.page == Page::Licence => {
                if self
                    .controls
                    .iter()
                    .any(|c| c.role == Role::ScrollView && c.rect.contains(*at))
                {
                    self.scroll = (self.scroll - delta.y * 48.0).max(0.0);
                    self.changed();
                    Handled::Yes
                } else {
                    Handled::No
                }
            }
            Event::Key(k) if k.pressed => self.key(k.key, k.modifiers.shift),
            _ => Handled::No,
        };
        if self.changes != before {
            cx.request_paint();
        }
        handled
    }
}

impl Wizard {
    fn key(&mut self, key: Key, shift: bool) -> Handled {
        let usable: Vec<usize> = (0..self.controls.len())
            .filter(|&i| self.controls[i].enabled || self.controls[i].act == Act::Install)
            .collect();
        match key {
            Key::Tab => {
                if usable.is_empty() {
                    return Handled::No;
                }
                let at = self.focus.and_then(|f| usable.iter().position(|&i| i == f));
                let next = match (at, shift) {
                    (None, false) => 0,
                    (None, true) => usable.len() - 1,
                    (Some(a), false) => (a + 1) % usable.len(),
                    (Some(a), true) => (a + usable.len() - 1) % usable.len(),
                };
                self.focus = Some(usable[next]);
                self.changed();
                Handled::Yes
            }
            Key::Enter => {
                let target = self
                    .focus
                    .or_else(|| self.controls.iter().rposition(|c| c.default));
                match target {
                    Some(i) => {
                        self.activate(i);
                        self.changed();
                        Handled::Yes
                    }
                    None => Handled::No,
                }
            }
            Key::Character(' ') => match self.focus {
                Some(i) => {
                    self.activate(i);
                    self.changed();
                    Handled::Yes
                }
                None => Handled::No,
            },
            Key::Escape => {
                if self.sheet {
                    self.act(Act::CloseSheet);
                } else if matches!(self.page, Page::Licence | Page::Options) {
                    self.act(Act::Back);
                } else {
                    return Handled::No;
                }
                Handled::Yes
            }
            Key::Down | Key::PageDown | Key::Up | Key::PageUp if self.page == Page::Licence => {
                let step = match key {
                    Key::Down => 40.0,
                    Key::Up => -40.0,
                    Key::PageDown => 240.0,
                    _ => -240.0,
                };
                self.scroll = (self.scroll + step).max(0.0);
                self.changed();
                Handled::Yes
            }
            _ => Handled::No,
        }
    }
}
