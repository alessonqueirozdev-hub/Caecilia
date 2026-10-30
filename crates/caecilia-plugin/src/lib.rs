//! Caecilia as a plugin: CLAP and VST3 from one library, on Manguebyte.
//!
//! MIDI channels play divisions (the organ's manuals say which; a channel
//! no manual claims plays the primary one, so a single keyboard needs no
//! setup). CC 11 is the swell shoe of the channel's division, CC 64 its
//! sustain, CC 120 and 123 stop everything. Drawstops and couplers are host
//! parameters, so a host can automate a registration and save it with the
//! session; changes land at the next block, as a drawstop takes a moment.
//!
//! # Real time
//!
//! `prepare` builds the engine and allocates. `process` allocates nothing:
//! `tests/realtime.rs` plays it under a counting allocator.

mod params;

use std::sync::Arc;

use caecilia_engine::{Command, DEFAULT_POLYPHONY, EngagedTable, OrganEngine, Timed};
use caecilia_model::{CouplerId, DivisionId, Organ, StopId, demo_organ};
use caecilia_registration::MASK_CAPACITY;
use caecilia_wind::MAX_TREMULANT_DEPTH_FRACTION;
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::EventKind;
use manguebyte_dsp::{Limiter, ProcessContext, ProcessSpec, Processor, Reverb};
use manguebyte_midi::MidiMessage;
use manguebyte_plugin::{AudioLayout, Plugin, PluginInfo, PluginKind, apply_param_events};

pub use params::{CaeciliaParams, MAX_COUPLERS, coupler_param_id, stop_param_id};

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
}

impl Caecilia {
    /// The organ it plays.
    pub fn organ(&self) -> &Organ {
        &self.organ
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
        }
    }

    fn params(&self) -> Arc<CaeciliaParams> {
        Arc::clone(&self.params)
    }

    fn prepare(&mut self, spec: &ProcessSpec) {
        let engine = OrganEngine::new(
            &self.organ,
            spec.sample_rate,
            spec.max_block_size,
            DEFAULT_POLYPHONY,
        );
        self.engine = Some(engine);
        // Everything is sent again on the first block.
        self.stop_mask = u64::MAX;
        self.coupler_mask = u32::MAX;
        self.tremulant = None;
        self.swell = None;
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

        // The registration, at the top of the block.
        let (stops, couplers) = (self.params.stop_mask(), self.params.coupler_mask());
        if stops != self.stop_mask || couplers != self.coupler_mask {
            self.stop_mask = stops;
            self.coupler_mask = couplers;
            let table = self.table(&engine);
            engine.set_engaged(table);
        }
        self.follow_parameters();

        for event in context.events {
            let channel_division = |c: u8| self.division_of_channel[usize::from(c & 0x0F)];
            let command = match event.kind {
                EventKind::NoteOn { note, .. } => Some(Command::NoteOn {
                    division: channel_division(note.channel),
                    note: note.key,
                }),
                EventKind::NoteOff { note, .. } | EventKind::NoteChoke { note } => {
                    Some(Command::NoteOff {
                        division: channel_division(note.channel),
                        note: note.key,
                    })
                }
                EventKind::Midi(MidiMessage::ControlChange {
                    channel,
                    controller,
                    value,
                }) => match controller {
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
                },
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
