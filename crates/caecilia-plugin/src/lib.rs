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
//! # Real time
//!
//! `prepare` builds the engine and allocates. `process` allocates nothing:
//! `tests/realtime.rs` plays it under a counting allocator.

mod console;
mod controller;
mod params;

use std::sync::{Arc, Mutex};

use caecilia_engine::{Command, DEFAULT_POLYPHONY, EngagedTable, OrganEngine, PoolConfig, Timed};
use caecilia_model::{Calibration, CouplerId, DivisionId, Organ, StopId, demo_organ};
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
pub use params::{
    CaeciliaParams, KEY_NAMES, MAX_COUPLERS, TEMPERAMENT_NAMES, coupler_param_id, stop_param_id,
};

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
    limiter: Limiter,
    gain: f32,
    controller: Arc<Mutex<RegistrationController>>,
    played: Producer<Played>,
    /// Keys a learned control swallowed, per channel, so their note-offs go too.
    swallowed: [u128; 16],
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
            Some(action) => {
                if pressed {
                    self.perform(action);
                }
                true
            }
            None => false,
        }
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

    fn push(&mut self, offset: u32, command: Command) {
        if self.events.len() < self.events.capacity() {
            self.events.push(Timed { offset, command });
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
        let organ = Arc::new(demo_organ());
        let params = Arc::new(CaeciliaParams::new(&organ));
        let (played, from_audio) = channel(256);
        let (to_audio, custom_tunings) = channel(4);
        let controller = Arc::new(Mutex::new(RegistrationController::new(
            Arc::clone(&organ),
            Arc::clone(&params),
            from_audio,
            to_audio,
        )));
        let primary = organ.primary_manual();
        let mut division_of_channel = [primary; 16];
        for m in organ.manuals() {
            if let Some(slot) = usize::try_from(m.midi_channel)
                .ok()
                .and_then(|c| division_of_channel.get_mut(c))
            {
                *slot = m.division;
            }
        }
        let mut reverb = Reverb::new();
        reverb.set_decay_seconds(3.2);
        reverb.set_size(0.85);
        reverb.set_damping(0.35);
        let mut limiter = Limiter::new();
        limiter.set_ceiling_db(OUTPUT_CEILING_DB);
        limiter.set_lookahead_ms(0.0);
        limiter.set_release_ms(200.0);
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
            limiter,
            gain: 1.0,
            controller,
            played,
            swallowed: [0; 16],
            controller_values: [[0; 128]; 16],
            origin: None,
            temperaments: temperament_tables(),
            tuning_choice: None,
            custom_tunings,
        }
    }

    fn params(&self) -> Arc<CaeciliaParams> {
        Arc::clone(&self.params)
    }

    fn prepare(&mut self, spec: &ProcessSpec) {
        // Every core but the audio thread's renders voices. A worker may hold a
        // job for at most half the block before the audio thread gives up on it:
        // a late group is a block of silence for its voices, a late callback is
        // a click for everything.
        let block = std::time::Duration::from_secs_f64(
            spec.max_block_size as f64 / spec.sample_rate.max(1.0),
        );
        let engine = OrganEngine::with_config(
            &self.organ,
            spec.sample_rate,
            spec.max_block_size,
            DEFAULT_POLYPHONY,
            Calibration::V2,
            PoolConfig::all_cores().wait_limit(block / 2),
        );
        self.engine = Some(engine);
        // Everything is sent again on the first block.
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
        self.tuning_choice = None;
        self.reverb.prepare(spec);
        self.limiter.prepare(spec);
    }

    fn process(&mut self, buffer: &mut AudioBuffer<f32>, context: &ProcessContext<'_, f32>) {
        let Some(mut engine) = self.engine.take() else {
            buffer.clear();
            return;
        };
        self.events.clear();
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
            if let Some(t) = self.temperaments.get(choice.0 * 12 + choice.1) {
                engine.set_tuning(t.clone());
                engine.set_pitch_scale(f64::from(choice.2) / 440.0);
            }
        }
        while let Some(t) = self.custom_tunings.pop() {
            engine.set_tuning(t);
            engine.set_pitch_scale(1.0);
        }

        for event in context.events {
            let channel_division = |c: u8| self.division_of_channel[usize::from(c & 0x0F)];
            let command = match event.kind {
                EventKind::NoteOn { note, .. } => {
                    let bit = 1u128 << (note.key & 0x7F);
                    if self.swallowed[usize::from(note.channel & 0x0F)] & bit != 0 {
                        None
                    } else {
                        Some(Command::NoteOn {
                            division: channel_division(note.channel),
                            note: note.key,
                        })
                    }
                }
                EventKind::NoteOff { note, .. } | EventKind::NoteChoke { note } => {
                    let c = usize::from(note.channel & 0x0F);
                    let bit = 1u128 << (note.key & 0x7F);
                    if self.swallowed[c] & bit != 0 {
                        self.swallowed[c] &= !bit;
                        None
                    } else {
                        Some(Command::NoteOff {
                            division: channel_division(note.channel),
                            note: note.key,
                        })
                    }
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
                        120 | 123 => Some(Command::Panic),
                        _ => None,
                    }
                }
                _ => None,
            };
            if let Some(command) = command {
                self.push(event.offset, command);
            }
        }

        let (left, right) = buffer.channel_pair_mut(0, 1);
        engine.process(left, right, &self.events);
        self.engine = Some(engine);

        self.reverb.set_mix(f64::from(self.params.reverb.value()));
        self.reverb.process(buffer);
        let target = 10.0f32.powf(self.params.level.value() / 20.0);
        buffer.apply_gain_ramp(self.gain, target);
        self.gain = target;
        self.limiter.process(buffer);
    }

    fn reset(&mut self) {
        if let Some(e) = self.engine.as_mut() {
            e.reset();
        }
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
        self.reverb.reset();
        self.limiter.reset();
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
