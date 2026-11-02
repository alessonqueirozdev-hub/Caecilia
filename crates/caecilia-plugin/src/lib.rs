// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Caecilia as a plugin: CLAP and VST3 from one library, on Manguebyte.
//!
//! MIDI channels play divisions (the organ's manuals say which; a channel
//! no manual claims plays the primary one, so a single keyboard needs no
//! setup). CC 11 is the swell shoe of the channel's division, CC 64 its
//! sustain, CC 120 and 123 stop everything. Drawstops and couplers are host
//! parameters, so a host can automate a registration and save it with the
//! session; changes land at the next block, as a drawstop takes a moment.
//!
//! The console: sixteen general pistons (the factory crescendo, then the
//! organist's own), recalled by program change 0-15 or a learned control;
//! MIDI learn binds any controller or spare key to a stop, a coupler, a
//! piston, the sequencer's next and previous, or cancel. Both are saved with
//! the session. Behind it all a [`RegistrationController`] offers selectors,
//! undo and redo across everything that changed the registration (the host's
//! automation and the pistons included), `explain`, and capturing pistons.
//!
//! The organ can change while it plays ([`CaeciliaParams::load_organ_file`]):
//! the new one is built off the audio thread, held keys carry over, the old
//! one fades out, and the session remembers the file.
//!
//! # Real time
//!
//! `prepare` builds the engine and allocates. `process` allocates nothing,
//! nor frees, an organ change included: `tests/realtime.rs` plays it under a
//! counting allocator.

mod console;
mod controller;
mod demo;
mod demo_files;
mod devices;
mod params;
mod sound;
mod switch;
mod telemetry;
mod ui;

use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use caecilia_engine::{Command, EngagedTable, OrganEngine, Timed};
use caecilia_model::{CouplerId, Diagnostic, DivisionId, Organ, StopId, caecilia_organ};
use caecilia_registration::MASK_CAPACITY;
use caecilia_wind::MAX_TREMULANT_DEPTH_FRACTION;
use manguebyte_core::AudioBuffer;
use manguebyte_core::spsc::{Consumer, Producer, channel};
use manguebyte_dsp::event::EventKind;
use manguebyte_dsp::tuning::{Scale, Temperament, TuningSettings};
use manguebyte_dsp::{Limiter, ProcessContext, ProcessSpec, Processor, Reverb, Tuning};
use manguebyte_midi::MidiMessage;
use manguebyte_plugin::{AudioLayout, Plugin, PluginInfo, PluginKind, apply_param_events};

pub use console::{Action, Binding, ConsoleMemory, GENERALS, LEARN_SLOTS, Source};
pub use controller::{Origin, Played, RegistrationController};
pub use devices::{DeviceEntry, DeviceRequest, DevicesView, serve_devices, stop_serving_devices};
pub use params::{
    CaeciliaParams, KEY_NAMES, MAX_COUPLERS, TEMPERAMENT_NAMES, coupler_param_id, stop_param_id,
};
pub use sound::{EQ_BANDS, EqBand, ROOM_NAMES};
pub use switch::{FADE_SECONDS, OrganLoadError, OrganSlot};
pub use telemetry::{TELEMETRY_CHESTS, Telemetry};
pub use ui::EDITOR_SIZE;

use switch::{Swap, SwitchAudio, build_engine, channels_of};

/// The organ the next instance plays, and its file, if an application chose
/// one.
type ChosenOrgan = (Arc<Organ>, Option<PathBuf>);
static NEXT_ORGAN: Mutex<Option<ChosenOrgan>> = Mutex::new(None);

/// Play `organ` in every instance made from now on.
pub fn set_organ_for_new_instances(organ: Organ) {
    if let Ok(mut next) = NEXT_ORGAN.lock() {
        *next = Some((Arc::new(organ), None));
    }
}

/// Play the organ in `path` in every instance made from now on; the
/// standalone application calls it with the file it was given, before it
/// starts. Returns the file's warnings.
pub fn set_organ_file_for_new_instances(path: &Path) -> Result<Vec<Diagnostic>, OrganLoadError> {
    let (organ, warnings) = OrganSlot::read(path)?;
    if let Ok(mut next) = NEXT_ORGAN.lock() {
        *next = Some((Arc::new(organ), Some(path.to_path_buf())));
    }
    Ok(warnings)
}

/// Whether new instances leave their console for the application.
static KEEP_CONSOLE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
/// The console of the last instance made while [`KEEP_CONSOLE`] was set.
static KEPT_CONSOLE: Mutex<Option<ui::Shared>> = Mutex::new(None);

/// Every instance made from now on leaves its console for
/// [`console_of_last_instance`]: the standalone application, which never
/// holds the plugin it plays, opens it in a window of its own.
pub fn keep_console_of_new_instances() {
    KEEP_CONSOLE.store(true, std::sync::atomic::Ordering::Relaxed);
}

/// The console of the last instance made since
/// [`keep_console_of_new_instances`], once, with the parameters its gestures
/// set.
pub fn console_of_last_instance() -> Option<(manguebyte_gui::Editor, Arc<CaeciliaParams>)> {
    let shared = KEPT_CONSOLE.lock().ok()?.take()?;
    let params = Arc::clone(&shared.params);
    Some((ui::editor(shared), params))
}

/// The environment variable naming an organ file for instances in a host,
/// which passes a plugin no arguments.
pub const ORGAN_ENV: &str = "CAECILIA_ORGAN";

/// The organ a new instance plays: the one an application chose, else the
/// file `CAECILIA_ORGAN` names, else the demonstration organ. A file that
/// does not load is reported on stderr and the demonstration organ plays:
/// an instrument that opens silent reads as broken.
fn organ_for_new_instance() -> ChosenOrgan {
    if let Some(chosen) = NEXT_ORGAN.lock().ok().and_then(|n| n.clone()) {
        return chosen;
    }
    if let Some(path) = std::env::var_os(ORGAN_ENV) {
        let path = PathBuf::from(path);
        match OrganSlot::read(&path) {
            Ok((organ, _)) => return (Arc::new(organ), Some(path)),
            Err(e) => eprintln!("Caecilia: {} did not load: {e}", path.display()),
        }
    }
    (Arc::new(caecilia_organ()), None)
}

/// What the console sends the audio thread.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum UiNote {
    /// A key or pedal played: its MIDI channel, its key, and whether it went
    /// down.
    Key { channel: u8, key: u8, on: bool },
    /// Play a demonstration (see [`demo::demo`]), its parts on `channels`
    /// and every key moved by `shift` semitones.
    Play {
        piece: u8,
        channels: [u8; 3],
        shift: i8,
    },
    /// Stop the demonstration.
    Stop,
}

/// Orders `events` by offset, keeping the order of those on one sample:
/// an insertion sort, in place, for the audio thread.
fn sort_by_offset(events: &mut [Timed]) {
    for i in 1..events.len() {
        let mut j = i;
        while j > 0 && events[j - 1].offset > events[j].offset {
            events.swap(j - 1, j);
            j -= 1;
        }
    }
}

/// Console notes that can wait for a block.
const UI_NOTES: usize = 256;

/// Commands one block can carry.
const MAX_BLOCK_EVENTS: usize = 2048;

/// The ceiling no output sample passes, dBFS. A safety net, not a bus
/// compressor: the voices' own headroom keeps a tutti under it.
const OUTPUT_CEILING_DB: f64 = -1.0;

/// The instrument.
pub struct Caecilia {
    organ: Arc<Organ>,
    params: Arc<CaeciliaParams>,
    engine: Option<OrganEngine>,
    division_of_channel: [DivisionId; 16],
    events: Vec<Timed>,
    stop_mask: u64,
    coupler_mask: u32,
    tremulant: Option<(bool, f32, f32)>,
    swell: Option<f32>,
    reverb: Reverb,
    /// The room the reverb is in, once set.
    room: Option<usize>,
    eq: sound::MasterEq,
    limiter: Limiter,
    gain: f32,
    controller: Arc<Mutex<RegistrationController>>,
    played: Producer<Played>,
    /// Keys a learned control swallowed, per channel, so their note-offs go too.
    swallowed: [u128; 16],
    /// Manual switches held, per channel: pressed together, their divisions
    /// add up.
    switches_held: [u128; 16],
    /// The divisions each channel plays, as bits, once a manual switch has
    /// chosen them; 0 for the organ's own channel map.
    routes: [u16; 16],
    /// The divisions each held key went down on, so it comes up there
    /// whatever was switched since.
    sounding: [[u16; 128]; 16],
    /// Each controller's last value, for firing learned controls on the press.
    controller_values: [[u8; 128]; 16],
    /// Who changed the registration this block, if the console did.
    origin: Option<Origin>,
    /// Every temperament on every key at A4 = 440, built once: building a
    /// tuning is not for the audio thread.
    temperaments: Vec<Tuning>,
    /// (temperament, key, A4) last sent to the engine.
    tuning_choice: Option<(usize, usize, f32)>,
    /// Tunings the controller built (Scala and the like).
    custom_tunings: Consumer<Tuning>,
    /// The one playing, if the controller's: it outlives an organ change.
    custom_tuning: Option<Tuning>,
    /// The tuning goes to the engine again (a new engine, a new tuning).
    retune: bool,
    /// Organs coming in, and going back to be dropped.
    switch: SwitchAudio,
    /// The organ being left, fading out.
    fading: Option<Box<Swap>>,
    /// Frames of the fade played, and its length.
    fade_done: usize,
    fade_len: usize,
    /// Its output, before the fade is applied.
    fade_left: Vec<f32>,
    fade_right: Vec<f32>,
    /// An organ done fading that the return ring had no room for yet.
    retired: Option<Box<Swap>>,
    /// Keys held, per channel, which an organ change carries over.
    held: [u128; 16],
    /// The organ slot's generation of the organ playing.
    generation: u32,
    /// What the console shows of the audio thread.
    telemetry: Arc<Telemetry>,
    /// Keys and pedals played on the console.
    ui_notes: Consumer<UiNote>,
    /// The console's end of that ring, behind a lock only the console takes.
    ui_notes_in: Arc<Mutex<Producer<UiNote>>>,
    /// The demonstration playing, if any.
    demo: demo::Player,
    sample_rate: f64,
}

/// Every temperament of [`TEMPERAMENT_NAMES`] on every key, at A4 = 440.
fn temperament_tables() -> Vec<Tuning> {
    let all = [
        Temperament::equal(),
        Temperament::quarter_comma_meantone(),
        Temperament::werckmeister_iii(),
        Temperament::kirnberger_iii(),
        Temperament::vallotti(),
        Temperament::young(),
        Temperament::pythagorean(),
        Temperament::just(),
    ];
    all.iter()
        .flat_map(|t| {
            (0..12u8).map(move |key| {
                TuningSettings {
                    scale: Scale::Temperament {
                        temperament: *t,
                        key,
                    },
                    ..TuningSettings::default()
                }
                .build()
                .unwrap_or_default()
            })
        })
        .collect()
}

impl Caecilia {
    /// The organ it plays.
    pub fn organ(&self) -> &Organ {
        &self.organ
    }

    /// The registration controller: undo, redo, selectors, pistons, learn.
    /// Off the audio thread.
    pub fn controller(&self) -> Arc<Mutex<RegistrationController>> {
        Arc::clone(&self.controller)
    }

    /// Recall general `n` here, on the audio thread: the host parameters
    /// change, and the host hears about it before the next block.
    fn recall_general(&mut self, n: usize) {
        if let Some((stops, couplers)) = self.params.console.general(n) {
            self.set_masks(stops, couplers);
            self.params.console.set_current_general(n);
            self.origin = Some(Origin::General(n as u16));
        }
    }

    fn set_masks(&self, stops: u64, couplers: u32) {
        for (n, p) in self.params.stops.iter().enumerate() {
            p.set(stops & (1 << n) != 0);
        }
        for (n, p) in self.params.couplers.iter().enumerate() {
            p.set(couplers & (1 << n) != 0);
        }
    }

    fn perform(&mut self, action: Action) {
        match action {
            Action::ToggleStop(n) => {
                if let Some(p) = self.params.stops.get(usize::from(n)) {
                    p.set(!p.value());
                    self.origin.get_or_insert(Origin::Control);
                }
            }
            Action::ToggleCoupler(n) => {
                if let Some(p) = self.params.couplers.get(usize::from(n)) {
                    p.set(!p.value());
                    self.origin.get_or_insert(Origin::Control);
                }
            }
            Action::General(n) => self.recall_general(usize::from(n)),
            Action::NextGeneral | Action::PreviousGeneral => {
                let next = match (self.params.console.current_general(), action) {
                    (None, _) => 0,
                    (Some(c), Action::NextGeneral) => (c + 1).min(GENERALS - 1),
                    (Some(c), _) => c.saturating_sub(1),
                };
                self.recall_general(next);
            }
            Action::Cancel => {
                self.set_masks(0, 0);
                self.origin.get_or_insert(Origin::Control);
            }
            // Taken where the key's channel is known (`console_takes`).
            Action::Manual(_) => {}
        }
    }

    /// A key or controller the console takes rather than the pipes: learning
    /// captures it, a learned binding fires it. Returns whether it was taken.
    fn console_takes(&mut self, source: Source, pressed: bool) -> bool {
        let console = &self.params.console;
        if console.armed().is_some() {
            return console.capture(source);
        }
        match console.lookup(source) {
            Some(Action::Manual(division)) => {
                if pressed {
                    match source {
                        Source::Key { channel, note } => {
                            self.switch_manual(channel, Some(note), division);
                        }
                        Source::Controller { channel, .. } => {
                            self.switch_manual(channel, None, division);
                        }
                    }
                }
                true
            }
            Some(action) => {
                if pressed {
                    self.perform(action);
                }
                true
            }
            None => false,
        }
    }

    /// What the audio thread reports to the console: wind, keys, voices,
    /// load.
    pub fn telemetry(&self) -> Arc<Telemetry> {
        Arc::clone(&self.telemetry)
    }

    /// The console, as `editor` offers it; tests open it headless.
    pub fn editor_spec(&self) -> manguebyte_gui::Editor {
        ui::editor(ui::Shared {
            params: Arc::clone(&self.params),
            controller: Arc::clone(&self.controller),
            telemetry: Arc::clone(&self.telemetry),
            notes: Arc::clone(&self.ui_notes_in),
        })
    }

    /// The demonstrations' names, in their order.
    pub fn demonstrations() -> Vec<&'static str> {
        demo::demos().iter().map(|d| d.name.as_str()).collect()
    }

    /// Play demonstration `n` on the organ playing, its stops left as they
    /// are; false if there is no such piece or it could not be sent.
    pub fn play_demonstration(&self, n: usize) -> bool {
        if n >= demo::demos().len() {
            return false;
        }
        let channels = ui::demo_channels(&self.params.organ.organ());
        self.ui_notes_in.lock().is_ok_and(|mut notes| {
            notes
                .push(UiNote::Play {
                    piece: n as u8,
                    channels,
                    shift: 0,
                })
                .is_ok()
        })
    }

    /// The engine, once prepared.
    pub fn engine(&self) -> Option<&OrganEngine> {
        self.engine.as_ref()
    }

    /// The table for the drawn stops and couplers, built on the stack.
    fn table(&self, engine: &OrganEngine) -> EngagedTable {
        let mut stops = [StopId(0); MASK_CAPACITY];
        let mut n = 0;
        for bit in 0..MASK_CAPACITY {
            if self.stop_mask & (1 << bit) != 0 {
                stops[n] = StopId(bit as u16);
                n += 1;
            }
        }
        let mut couplers = [CouplerId(0); MAX_COUPLERS];
        let mut m = 0;
        for bit in 0..MAX_COUPLERS {
            if self.coupler_mask & (1 << bit) != 0 {
                couplers[m] = CouplerId(bit as u16);
                m += 1;
            }
        }
        engine.table_for(&self.organ, &stops[..n], &couplers[..m])
    }

    /// A key the console or a demonstration plays, at `offset` in the block.
    fn play_key(&mut self, offset: u32, channel: u8, key: u8, on: bool) {
        let c = usize::from(channel & 0x0F);
        let bit = 1u128 << (key & 0x7F);
        let division = self.division_of_channel[c];
        let note = key & 0x7F;
        if on {
            self.held[c] |= bit;
            self.push(offset, Command::NoteOn { division, note });
        } else {
            self.held[c] &= !bit;
            self.push(offset, Command::NoteOff { division, note });
        }
    }

    /// The divisions channel `c` plays, as bits.
    fn route(&self, c: usize) -> u16 {
        match self.routes[c] {
            0 => 1u16
                .checked_shl(u32::from(self.division_of_channel[c].0))
                .unwrap_or(0),
            r => r,
        }
    }

    /// A key down or up on each division of `divisions`.
    fn push_divisions(&mut self, offset: u32, divisions: u16, note: u8, on: bool) {
        let mut left = divisions;
        while left != 0 {
            let division = DivisionId(left.trailing_zeros() as u16);
            left &= left - 1;
            let command = if on {
                Command::NoteOn { division, note }
            } else {
                Command::NoteOff { division, note }
            };
            self.push(offset, command);
        }
    }

    /// A manual switch pressed on `channel`: that keyboard plays `division`
    /// from now on, or `division` too while another switch is held.
    fn switch_manual(&mut self, channel: u8, note: Option<u8>, division: u16) {
        if usize::from(division) >= self.organ.divisions().len().min(16) {
            return;
        }
        let c = usize::from(channel & 0x0F);
        let bit = 1u16 << division;
        let others = note.map_or(0, |n| self.switches_held[c] & !(1u128 << (n & 0x7F)));
        self.routes[c] = if others != 0 {
            self.route(c) | bit
        } else {
            bit
        };
        if let Some(n) = note {
            self.switches_held[c] |= 1u128 << (n & 0x7F);
        }
        self.telemetry.set_route(channel, self.routes[c]);
    }

    fn push(&mut self, offset: u32, command: Command) {
        if self.events.len() < self.events.capacity() {
            self.events.push(Timed { offset, command });
        }
    }

    /// Hand back an organ done with; keep it if the ring is full, never
    /// dropping it here.
    fn retire(&mut self, swap: Box<Swap>) {
        if let Err(swap) = self.switch.garbage.push(swap) {
            self.retired = Some(swap);
        }
    }

    /// Take a new organ if one has come and the last change is done: its
    /// engine in, the old one fading, everything sent again, the held keys
    /// pressed on it. Call with `events` cleared.
    fn take_new_organ(&mut self) {
        if let Some(swap) = self.retired.take() {
            self.retire(swap);
        }
        if self.fading.is_some() || self.retired.is_some() {
            return;
        }
        let Some(mut swap) = self.switch.incoming.pop() else {
            return;
        };
        if swap.engine.is_none() || self.engine.is_none() {
            // Built before anything was prepared: `prepare` builds it.
            self.retire(swap);
            return;
        }
        std::mem::swap(&mut self.organ, &mut swap.organ);
        std::mem::swap(&mut self.engine, &mut swap.engine);
        self.generation = swap.generation;
        self.division_of_channel = channels_of(&self.organ);
        if let Some((stops, couplers)) = swap.registration.take() {
            self.set_masks(stops, couplers);
        }
        // The swap now holds the organ being left.
        self.fading = Some(swap);
        self.fade_done = 0;
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
        self.retune = true;
        // The manual switches named the old organ's divisions: the new one
        // plays by its own channel map until a switch is pressed again.
        self.routes = [0; 16];
        self.telemetry.set_route(0, 0);
        for channel in 0..16 {
            let mut keys = self.held[channel];
            while keys != 0 {
                let note = keys.trailing_zeros() as u8;
                keys &= keys - 1;
                let divisions = self.route(channel);
                self.sounding[channel][usize::from(note)] = divisions;
                self.push_divisions(0, divisions, note, true);
            }
        }
    }

    /// Add the organ being left, fading, to the block.
    fn fade_out(&mut self, buffer: &mut AudioBuffer<f32>) {
        let Some(mut old) = self.fading.take() else {
            return;
        };
        let frames = buffer.num_frames().min(self.fade_left.len());
        if let Some(engine) = old.engine.as_mut() {
            if self.fade_done < self.fade_len {
                let (l, r) = (
                    &mut self.fade_left[..frames],
                    &mut self.fade_right[..frames],
                );
                engine.process(l, r, &[]);
                let (left, right) = buffer.channel_pair_mut(0, 1);
                let len = self.fade_len as f32;
                for i in 0..frames {
                    let g = 1.0 - ((self.fade_done + i) as f32 / len).min(1.0);
                    left[i] += l[i] * g;
                    right[i] += r[i] * g;
                }
            }
        }
        self.fade_done += frames;
        if self.fade_done >= self.fade_len {
            self.retire(old);
        } else {
            self.fading = Some(old);
        }
    }

    /// Host parameters that turn into engine commands, when they change.
    fn follow_parameters(&mut self) {
        let p = Arc::clone(&self.params);
        let trem = (
            p.tremulant.value(),
            p.tremulant_rate.value(),
            p.tremulant_depth.value(),
        );
        if self.tremulant != Some(trem) {
            self.tremulant = Some(trem);
            for i in 0..self.organ.windchests().len() {
                if self.organ.windchests()[i].has_tremulant {
                    let chest = self.organ.windchests()[i].id;
                    self.push(
                        0,
                        Command::Tremulant {
                            chest,
                            enabled: trem.0,
                            rate_hz: trem.1,
                            depth_fraction: trem.2 * MAX_TREMULANT_DEPTH_FRACTION,
                        },
                    );
                }
            }
        }
        let swell = p.swell.value();
        if self.swell != Some(swell) {
            self.swell = Some(swell);
            for i in 0..self.organ.divisions().len() {
                if self.organ.divisions()[i].enclosed {
                    let division = self.organ.divisions()[i].id;
                    self.push(
                        0,
                        Command::Expression {
                            division,
                            position: swell,
                        },
                    );
                }
            }
        }
    }
}

impl Plugin for Caecilia {
    const INFO: PluginInfo = PluginInfo {
        id: "org.caecilia.organ",
        name: "Caecilia",
        vendor: "Alesson Queiroz",
        version: env!("CARGO_PKG_VERSION"),
        description: "A modeled pipe organ: living wind, every rank its own voice",
        kind: PluginKind::Instrument,
    };
    const LAYOUT: AudioLayout = AudioLayout::STEREO_INSTRUMENT;
    type Params = CaeciliaParams;

    fn new() -> Self {
        // The demonstrations' scores, compiled here so the audio thread
        // only reads them.
        demo::prepare();
        let (organ, path) = organ_for_new_instance();
        let (params, switch) = CaeciliaParams::new(Arc::clone(&organ), path);
        let params = Arc::new(params);
        let (played, from_audio) = channel(256);
        let (to_audio, custom_tunings) = channel(4);
        let (ui_notes_in, ui_notes) = channel(UI_NOTES);
        let controller = Arc::new(Mutex::new(RegistrationController::new(
            Arc::clone(&organ),
            Arc::clone(&params),
            from_audio,
            to_audio,
        )));
        let division_of_channel = channels_of(&organ);
        let mut reverb = Reverb::new();
        sound::set_room(&mut reverb, sound::DEFAULT_ROOM);
        let mut limiter = Limiter::new();
        limiter.set_ceiling_db(OUTPUT_CEILING_DB);
        limiter.set_lookahead_ms(0.0);
        limiter.set_release_ms(200.0);
        let ui_notes_in = Arc::new(Mutex::new(ui_notes_in));
        let telemetry: Arc<Telemetry> = Arc::default();
        if KEEP_CONSOLE.load(std::sync::atomic::Ordering::Relaxed) {
            if let Ok(mut kept) = KEPT_CONSOLE.lock() {
                *kept = Some(ui::Shared {
                    params: Arc::clone(&params),
                    controller: Arc::clone(&controller),
                    telemetry: Arc::clone(&telemetry),
                    notes: Arc::clone(&ui_notes_in),
                });
            }
        }
        Self {
            organ,
            params,
            engine: None,
            division_of_channel,
            events: Vec::with_capacity(MAX_BLOCK_EVENTS),
            stop_mask: 0,
            coupler_mask: 0,
            tremulant: None,
            swell: None,
            reverb,
            room: None,
            eq: sound::MasterEq::new(),
            limiter,
            gain: 1.0,
            controller,
            played,
            swallowed: [0; 16],
            switches_held: [0; 16],
            routes: [0; 16],
            sounding: [[0; 128]; 16],
            controller_values: [[0; 128]; 16],
            origin: None,
            temperaments: temperament_tables(),
            tuning_choice: None,
            custom_tunings,
            custom_tuning: None,
            retune: true,
            switch,
            fading: None,
            fade_done: 0,
            fade_len: 0,
            fade_left: Vec::new(),
            fade_right: Vec::new(),
            retired: None,
            held: [0; 16],
            generation: 0,
            telemetry,
            ui_notes,
            ui_notes_in,
            demo: demo::Player::new(),
            sample_rate: 48_000.0,
        }
    }

    fn params(&self) -> Arc<CaeciliaParams> {
        Arc::clone(&self.params)
    }

    fn prepare(&mut self, spec: &ProcessSpec) {
        // The organ of record, whatever was chosen while nothing played.
        let (organ, generation, registration) =
            self.params
                .organ
                .prepare(&mut self.switch, spec.sample_rate, spec.max_block_size);
        self.organ = organ;
        self.generation = generation;
        self.division_of_channel = channels_of(&self.organ);
        if let Some((stops, couplers)) = registration {
            self.set_masks(stops, couplers);
        }
        self.engine = Some(build_engine(
            &self.organ,
            spec.sample_rate,
            spec.max_block_size,
        ));
        self.fading = None;
        self.retired = None;
        self.fade_len = (FADE_SECONDS * spec.sample_rate).round().max(1.0) as usize;
        self.fade_left = vec![0.0; spec.max_block_size];
        self.fade_right = vec![0.0; spec.max_block_size];
        self.held = [0; 16];
        self.sounding = [[0; 128]; 16];
        self.switches_held = [0; 16];
        self.sample_rate = spec.sample_rate;
        // Everything is sent again on the first block.
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
        self.retune = true;
        self.reverb.prepare(spec);
        self.eq.prepare(spec);
        self.room = None;
        self.limiter.prepare(spec);
    }

    fn process(&mut self, buffer: &mut AudioBuffer<f32>, context: &ProcessContext<'_, f32>) {
        let started = std::time::Instant::now();
        self.events.clear();
        self.take_new_organ();
        let Some(mut engine) = self.engine.take() else {
            buffer.clear();
            return;
        };
        apply_param_events(&*self.params, context.events);

        // The console first: pistons and learned controls change the
        // registration, which then applies to this block from its top.
        self.origin = None;
        for event in context.events {
            match event.kind {
                EventKind::Midi(MidiMessage::ProgramChange { program, .. }) => {
                    if usize::from(program) < GENERALS {
                        self.recall_general(usize::from(program));
                    }
                }
                EventKind::NoteOn { note, .. } => {
                    let source = Source::Key {
                        channel: note.channel,
                        note: note.key,
                    };
                    if self.console_takes(source, true) {
                        self.swallowed[usize::from(note.channel & 0x0F)] |= 1 << (note.key & 0x7F);
                    }
                }
                EventKind::Midi(MidiMessage::ControlChange {
                    channel,
                    controller,
                    value,
                }) if !matches!(controller, 120 | 123) => {
                    let (c, n) = (usize::from(channel & 0x0F), usize::from(controller & 0x7F));
                    let rising = value >= 64 && self.controller_values[c][n] < 64;
                    self.controller_values[c][n] = value;
                    let source = Source::Controller {
                        channel,
                        number: controller,
                    };
                    // Swell and sustain stay what they are unless learning took them.
                    self.console_takes(source, rising);
                }
                _ => {}
            }
        }

        // The registration, at the top of the block.
        let (stops, couplers) = (self.params.stop_mask(), self.params.coupler_mask());
        if stops != self.stop_mask || couplers != self.coupler_mask {
            self.stop_mask = stops;
            self.coupler_mask = couplers;
            let table = self.table(&engine);
            engine.set_engaged(table);
            // Into the history, off this thread. A full ring drops the note
            // rather than wait: the sound has changed either way.
            let origin = self.origin.unwrap_or(Origin::Host);
            let _ = self.played.push(Played {
                stops,
                couplers,
                origin,
                generation: self.generation,
            });
        }
        self.follow_parameters();

        // The tuning: a temperament the host chose, or one the controller built.
        let choice = (
            self.params.temperament.index(),
            self.params.temperament_key.index(),
            self.params.pitch.value(),
        );
        if self.tuning_choice != Some(choice) {
            self.tuning_choice = Some(choice);
            self.custom_tuning = None;
            self.retune = true;
        }
        while let Some(t) = self.custom_tunings.pop() {
            self.custom_tuning = Some(t);
            self.retune = true;
        }
        if self.retune {
            self.retune = false;
            match &self.custom_tuning {
                Some(t) => {
                    engine.set_tuning(t.clone());
                    engine.set_pitch_scale(1.0);
                }
                None => {
                    if let Some(t) = self.temperaments.get(choice.0 * 12 + choice.1) {
                        engine.set_tuning(t.clone());
                        engine.set_pitch_scale(f64::from(choice.2) / 440.0);
                    }
                }
            }
        }

        // Keys played on the console, at the top of the block.
        let rate = self.sample_rate;
        while let Some(n) = self.ui_notes.pop() {
            match n {
                UiNote::Key { channel, key, on } => self.play_key(0, channel, key, on),
                UiNote::Play {
                    piece,
                    channels,
                    shift,
                } => {
                    let mut demo = std::mem::replace(&mut self.demo, demo::Player::new());
                    demo.start(piece, channels, shift, |at, c, k, on| {
                        self.play_key(at, c, k, on)
                    });
                    self.demo = demo;
                }
                UiNote::Stop => {
                    let mut demo = std::mem::replace(&mut self.demo, demo::Player::new());
                    demo.stop(|at, c, k, on| self.play_key(at, c, k, on));
                    self.demo = demo;
                }
            }
        }
        let first_host_event = self.events.len();
        let division_of_channel = self.division_of_channel;
        for event in context.events {
            let channel_division = |c: u8| division_of_channel[usize::from(c & 0x0F)];
            let command = match event.kind {
                EventKind::NoteOn { note, .. } => {
                    let c = usize::from(note.channel & 0x0F);
                    let key = note.key & 0x7F;
                    let bit = 1u128 << key;
                    if self.swallowed[c] & bit == 0 {
                        self.held[c] |= bit;
                        // Every division the keyboard is switched to.
                        let divisions = self.route(c);
                        self.sounding[c][usize::from(key)] = divisions;
                        self.push_divisions(event.offset, divisions, key, true);
                    }
                    None
                }
                EventKind::NoteOff { note, .. } | EventKind::NoteChoke { note } => {
                    let c = usize::from(note.channel & 0x0F);
                    let key = note.key & 0x7F;
                    let bit = 1u128 << key;
                    if self.swallowed[c] & bit != 0 {
                        self.swallowed[c] &= !bit;
                        self.switches_held[c] &= !bit;
                    } else {
                        self.held[c] &= !bit;
                        let divisions =
                            match std::mem::take(&mut self.sounding[c][usize::from(key)]) {
                                0 => self.route(c),
                                d => d,
                            };
                        self.push_divisions(event.offset, divisions, key, false);
                    }
                    None
                }
                EventKind::Midi(MidiMessage::ControlChange {
                    channel,
                    controller,
                    value,
                }) => {
                    let learned = self.params.console.lookup(Source::Controller {
                        channel,
                        number: controller,
                    });
                    match controller {
                        _ if learned.is_some() => None,
                        11 => Some(Command::Expression {
                            division: channel_division(channel),
                            position: f32::from(value) / 127.0,
                        }),
                        64 => Some(Command::Sustain {
                            division: channel_division(channel),
                            down: value >= 64,
                        }),
                        120 | 123 => {
                            self.held = [0; 16];
                            self.sounding = [[0; 128]; 16];
                            Some(Command::Panic)
                        }
                        _ => None,
                    }
                }
                _ => None,
            };
            if let Some(command) = command {
                self.push(event.offset, command);
            }
        }
        // The demonstration's keys, each on its sample, among the host's.
        if self.demo.playing().is_some() {
            let host_events = self.events.len();
            let mut demo = std::mem::replace(&mut self.demo, demo::Player::new());
            demo.render(buffer.num_frames() as u32, rate, |at, c, k, on| {
                self.play_key(at, c, k, on)
            });
            self.demo = demo;
            if self.events.len() > host_events {
                sort_by_offset(&mut self.events[first_host_event..]);
            }
        }
        self.telemetry.set_demo(self.demo.playing());

        let (left, right) = buffer.channel_pair_mut(0, 1);
        engine.process(left, right, &self.events);
        let chests = self.organ.windchests();
        for (i, chest) in chests.iter().take(telemetry::TELEMETRY_CHESTS).enumerate() {
            self.telemetry
                .set_pressure(i, engine.wind().pressure_at(chest.id, 0));
        }
        self.telemetry.set_voices(engine.active_voices());
        self.engine = Some(engine);
        self.fade_out(buffer);

        let room = self.params.room.index();
        if self.room != Some(room) {
            self.room = Some(room);
            sound::set_room(&mut self.reverb, room);
        }
        self.reverb.set_mix(f64::from(self.params.reverb.value()));
        self.reverb.process(buffer);
        let gains = std::array::from_fn(|i| self.params.eq_bands[i].value());
        self.eq.process(buffer, self.params.eq.value(), gains);
        let target = 10.0f32.powf(self.params.level.value() / 20.0);
        buffer.apply_gain_ramp(self.gain, target);
        self.gain = target;
        self.limiter.process(buffer);

        self.telemetry.set_held(&self.held);
        let block = buffer.num_frames() as f64 / self.sample_rate.max(1.0);
        if block > 0.0 {
            let share = started.elapsed().as_secs_f64() / block;
            self.telemetry.add_load(share as f32);
        }
    }

    fn reset(&mut self) {
        if let Some(e) = self.engine.as_mut() {
            e.reset();
        }
        // The organ being left stops now; it is handed back next block.
        self.fade_done = self.fade_len;
        self.held = [0; 16];
        self.sounding = [[0; 128]; 16];
        self.switches_held = [0; 16];
        self.demo = demo::Player::new();
        self.retune = true;
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
        self.reverb.reset();
        self.eq.reset();
        self.limiter.reset();
    }

    fn editor(&self) -> Option<Box<dyn manguebyte_plugin::editor::EditorSpec>> {
        Some(Box::new(self.editor_spec()))
    }

    fn latency_samples(&self) -> usize {
        self.limiter.latency_samples()
    }
}

/// The entry points hosts look for. `no_mangle` is what makes them findable,
/// and it is the one thing in Caecilia the `unsafe_code` lint has to allow.
#[allow(unsafe_code)]
mod entry {
    manguebyte_clap::export_clap!(super::Caecilia);
    manguebyte_vst3::export_vst3!(super::Caecilia);
}
