//! What the console shows, read once a frame: the parameters, the
//! controller's history, the audio thread's telemetry. Every part draws
//! from the same reading, so they never disagree.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

use caecilia_model::{DivisionKind, Organ, StopId};
use caecilia_registration::{FACTORY_GENERALS, Reason, RegistrationEngine, RegistrationState};

use super::Shared;
use super::look::Look;
use crate::console::GENERALS;
use crate::params::{KEY_NAMES, TEMPERAMENT_NAMES};
use crate::telemetry::TELEMETRY_CHESTS;

/// Readings of a chest's wind kept for its trace: two seconds of frames.
const TRACE: usize = 120;

/// Pascals in a millimetre of water.
const PA_PER_MM: f32 = 9.806_65;

/// A run of drawstops under one plate.
pub(crate) struct Section {
    pub(crate) title: String,
    pub(crate) note: String,
    pub(crate) stops: Vec<usize>,
}

/// A keyboard: a manual or the pedalboard.
pub(crate) struct Keyboard {
    /// The division's name, as the keyboard is announced.
    pub(crate) name: String,
    pub(crate) title: String,
    pub(crate) note: String,
    /// 0-based MIDI channel, if it has one.
    pub(crate) channel: Option<usize>,
    pub(crate) low: u8,
    pub(crate) high: u8,
    /// The couplers that bring another division to it.
    pub(crate) coupled_by: Vec<usize>,
}

/// Where everything of an organ goes on the console, worked out once.
pub(crate) struct Layout {
    /// The bottom manual's division and the pedal's, on the left jamb.
    pub(crate) left: Vec<Section>,
    /// The other manuals', on the right.
    pub(crate) right: Vec<Section>,
    /// The manuals, top first.
    pub(crate) manuals: Vec<Keyboard>,
    pub(crate) pedal: Option<Keyboard>,
    /// Whether a chest has a tremulant.
    pub(crate) tremulant: bool,
    /// What the swell shoe closes.
    pub(crate) swell: String,
}

fn roman(n: usize) -> &'static str {
    ["I", "II", "III", "IV", "V", "VI"]
        .get(n)
        .copied()
        .unwrap_or("")
}

fn channel_note(channel: i32) -> String {
    if channel >= 0 {
        format!("ch {}", channel + 1)
    } else {
        String::new()
    }
}

impl Layout {
    pub(crate) fn of(organ: &Organ) -> Layout {
        let mut manuals: Vec<_> = organ
            .manuals()
            .iter()
            .filter(|m| {
                organ
                    .division(m.division)
                    .is_some_and(|d| d.kind == DivisionKind::Manual)
            })
            .collect();
        manuals.sort_by_key(|m| m.index);
        let pedal = organ.manuals().iter().find(|m| {
            organ
                .division(m.division)
                .is_some_and(|d| d.kind == DivisionKind::Pedal)
        });
        let section = |division, numeral: Option<usize>, channel: i32| {
            let d = organ.division(division);
            let name = d.map_or("", |d| d.name.as_str());
            let channel = channel_note(channel);
            let note = match numeral {
                Some(n) if channel.is_empty() => roman(n).to_string(),
                Some(n) => format!("{}  \u{b7}  {channel}", roman(n)),
                None => channel,
            };
            Section {
                title: name.to_uppercase(),
                note,
                stops: organ
                    .stops()
                    .iter()
                    .enumerate()
                    .filter(|(_, s)| s.division == division)
                    .map(|(n, _)| n)
                    .collect(),
            }
        };
        let mut left = Vec::new();
        let mut right = Vec::new();
        for (i, m) in manuals.iter().enumerate() {
            let s = section(m.division, Some(i), m.midi_channel);
            if i == 0 { left.push(s) } else { right.push(s) }
        }
        if let Some(p) = pedal {
            left.push(section(p.division, None, p.midi_channel));
        }
        let keyboard = |m: &caecilia_model::Manual, title: String| Keyboard {
            name: organ
                .division(m.division)
                .map_or_else(String::new, |d| d.name.clone()),
            title,
            note: channel_note(m.midi_channel),
            channel: usize::try_from(m.midi_channel).ok(),
            low: m.low_note,
            high: m.high_note,
            coupled_by: organ
                .couplers()
                .iter()
                .enumerate()
                .filter(|(_, c)| c.to == m.division)
                .map(|(n, _)| n)
                .collect(),
        };
        let division_name = |m: &caecilia_model::Manual| {
            organ
                .division(m.division)
                .map_or_else(String::new, |d| d.name.to_uppercase())
        };
        let mut keyboards: Vec<Keyboard> = manuals
            .iter()
            .enumerate()
            .map(|(i, m)| keyboard(m, format!("{}  {}", roman(i), division_name(m))))
            .collect();
        keyboards.reverse();
        let swell: Vec<&str> = organ
            .divisions()
            .iter()
            .filter(|d| d.enclosed)
            .map(|d| d.name.as_str())
            .collect();
        Layout {
            left,
            right,
            manuals: keyboards,
            pedal: pedal.map(|p| keyboard(p, division_name(p))),
            tremulant: organ.windchests().iter().any(|c| c.has_tremulant),
            swell: if swell.is_empty() {
                "Swell".into()
            } else {
                swell.join(", ")
            },
        }
    }
}

/// A chest's wind as its gauge shows it.
#[derive(Clone, PartialEq)]
pub(crate) struct ChestReading {
    pub(crate) name: String,
    pub(crate) nominal_mm: f32,
    pub(crate) now_mm: f32,
    pub(crate) trace: Vec<f32>,
    pub(crate) note: String,
}

/// Why a stop is, or is not, drawn: the stop, and the rest of the sentence.
#[derive(Clone, PartialEq, Hash)]
pub(crate) struct Why {
    pub(crate) drawn: bool,
    pub(crate) lead: String,
    pub(crate) rest: String,
}

/// The registration panel's text: its summary, the last steps (true where
/// the registration is), and why.
type RegistrationText = (String, Vec<(String, bool)>, Vec<Why>);

/// One frame's reading.
pub(crate) struct Snapshot {
    pub(crate) look: Look,
    pub(crate) organ: Arc<Organ>,
    pub(crate) layout: Rc<Layout>,
    pub(crate) drawn: Vec<bool>,
    pub(crate) couplers: Vec<bool>,
    pub(crate) tremulant: bool,
    /// The general last recalled.
    pub(crate) general: Option<usize>,
    /// Which generals hold something.
    pub(crate) generals_set: [bool; GENERALS],
    pub(crate) swell: f32,
    /// Keys held, by MIDI channel.
    pub(crate) held: [u128; 16],
    pub(crate) chests: Vec<ChestReading>,
    pub(crate) summary: String,
    /// The way from the opening to here, the last few steps; true marks
    /// where the registration is.
    pub(crate) history: Vec<(String, bool)>,
    pub(crate) why: Vec<Why>,
    pub(crate) tuning: String,
    pub(crate) wind_steady: bool,
    pub(crate) cpu: String,
}

fn hash_of(f: impl FnOnce(&mut DefaultHasher)) -> u64 {
    let mut h = DefaultHasher::new();
    f(&mut h);
    h.finish()
}

impl Snapshot {
    /// What the jambs show.
    pub(crate) fn stops_key(&self) -> u64 {
        hash_of(|h| (&self.drawn, &self.couplers, self.tremulant).hash(h))
    }

    /// What the key slip and the toe studs show.
    pub(crate) fn generals_key(&self) -> u64 {
        hash_of(|h| (self.general, self.generals_set).hash(h))
    }

    /// What the shoes show.
    pub(crate) fn shoes_key(&self) -> u64 {
        hash_of(|h| ((self.swell * 100.0).round() as i32, self.general).hash(h))
    }

    /// What the pedalboard shows.
    pub(crate) fn pedal_key(&self) -> u64 {
        let channel = self.layout.pedal.as_ref().and_then(|p| p.channel);
        hash_of(|h| {
            (channel.map(|c| self.held[c]), self.general).hash(h);
        })
    }

    /// What the registration panel shows.
    pub(crate) fn registration_key(&self) -> u64 {
        hash_of(|h| (&self.summary, &self.history, &self.why).hash(h))
    }

    /// What the wind panel shows.
    pub(crate) fn wind_key(&self) -> u64 {
        hash_of(|h| {
            for c in &self.chests {
                c.now_mm.to_bits().hash(h);
                c.note.hash(h);
                for v in &c.trace {
                    v.to_bits().hash(h);
                }
            }
        })
    }

    /// What the status chips show.
    pub(crate) fn status_key(&self) -> u64 {
        hash_of(|h| (&self.tuning, self.wind_steady, &self.cpu).hash(h))
    }

    /// What a keyboard's plate shows.
    pub(crate) fn couplers_key(&self) -> u64 {
        hash_of(|h| self.couplers.hash(h))
    }
}

/// The stops drawn on a run of sections, and the couplers drawn.
pub(crate) fn describe_stops(snapshot: &Snapshot, sections: &[Section], couplers: bool) -> String {
    let organ = &snapshot.organ;
    let mut drawn: Vec<String> = sections
        .iter()
        .flat_map(|s| s.stops.iter())
        .filter(|&&n| snapshot.drawn.get(n).copied().unwrap_or(false))
        .filter_map(|&n| organ.stops().get(n).map(|s| s.name.clone()))
        .collect();
    if couplers {
        drawn.extend(
            organ
                .couplers()
                .iter()
                .zip(&snapshot.couplers)
                .filter(|(_, on)| **on)
                .map(|(c, _)| c.name.clone()),
        );
        if snapshot.tremulant && snapshot.layout.tremulant {
            drawn.push("Tremulant".into());
        }
    }
    if drawn.is_empty() {
        "nothing drawn".into()
    } else {
        format!("{} drawn", drawn.join(", "))
    }
}

/// A key's name: C4 for 60.
pub(crate) fn note_name(note: u8) -> String {
    const NAMES: [&str; 12] = [
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
    ];
    format!(
        "{}{}",
        NAMES[usize::from(note % 12)],
        i32::from(note / 12) - 1
    )
}

/// A stop's name without its pitch: "Basson-Hautbois" of "Basson-Hautbois 8".
pub(crate) fn bare_name(name: &str) -> &str {
    let words: Vec<&str> = name.split_whitespace().collect();
    let is_pitch = |w: &str| {
        w.chars().all(|c| c.is_ascii_digit() || c == '/')
            || w.chars().all(|c| matches!(c, 'I' | 'V' | 'X'))
    };
    match words.iter().rposition(|w| !is_pitch(w)) {
        Some(i) if i + 1 < words.len() => {
            let end = name.find(words[i + 1]).unwrap_or(name.len());
            name[..end].trim_end()
        }
        _ => name,
    }
}

/// The reading, made at most once a frame, and the wind's recent past.
pub(crate) struct Source {
    shared: Shared,
    look: Look,
    organ: Arc<Organ>,
    layout: Rc<Layout>,
    current: RefCell<Option<(Duration, Rc<Snapshot>)>>,
    traces: RefCell<Vec<VecDeque<f32>>>,
    /// The registration's text when the controller was last free.
    last_registration: RefCell<RegistrationText>,
}

impl Source {
    pub(crate) fn new(shared: Shared, look: Look) -> Rc<Source> {
        let organ = shared.params.organ.organ();
        let layout = Rc::new(Layout::of(&organ));
        Rc::new(Source {
            shared,
            look,
            organ,
            layout,
            current: RefCell::new(None),
            traces: RefCell::default(),
            last_registration: RefCell::default(),
        })
    }

    pub(crate) fn look(&self) -> Look {
        self.look
    }

    pub(crate) fn layout(&self) -> Rc<Layout> {
        Rc::clone(&self.layout)
    }

    pub(crate) fn organ(&self) -> Arc<Organ> {
        Arc::clone(&self.organ)
    }

    /// This frame's reading.
    pub(crate) fn get(&self, now: Duration) -> Rc<Snapshot> {
        if let Some((at, snapshot)) = self.current.borrow().as_ref() {
            if *at == now {
                return Rc::clone(snapshot);
            }
        }
        let snapshot = Rc::new(self.read());
        *self.current.borrow_mut() = Some((now, Rc::clone(&snapshot)));
        snapshot
    }

    fn read(&self) -> Snapshot {
        let params = &self.shared.params;
        let telemetry = &self.shared.telemetry;
        let organ = &self.organ;
        let stops = params.stop_mask();
        let couplers = params.coupler_mask();
        let console = &params.console;
        let general = console.current_general();
        let mut generals_set = [false; GENERALS];
        for (n, set) in generals_set.iter_mut().enumerate() {
            *set = console.general(n).is_some_and(|g| g != (0, 0));
        }
        let mut held = [0u128; 16];
        for (c, keys) in held.iter_mut().enumerate() {
            *keys = telemetry.held(c);
        }
        let tremulant = params.tremulant.value();

        // The wind, and its last two seconds.
        let mut traces = self.traces.borrow_mut();
        let chests: Vec<ChestReading> = organ
            .windchests()
            .iter()
            .take(TELEMETRY_CHESTS)
            .enumerate()
            .map(|(i, chest)| {
                if traces.len() <= i {
                    traces.push(VecDeque::with_capacity(TRACE));
                }
                let nominal = chest.nominal_pressure_pa / PA_PER_MM;
                let reported = telemetry.pressure_pa(i);
                // Before the first block the chest stands at its pressure.
                let now = if reported > 0.0 {
                    reported / PA_PER_MM
                } else {
                    nominal
                };
                let trace = &mut traces[i];
                if trace.len() == TRACE {
                    trace.pop_front();
                }
                trace.push_back(now);
                let trace: Vec<f32> = trace.iter().copied().collect();
                let (lo, hi) = trace
                    .iter()
                    .fold((f32::MAX, f32::MIN), |(lo, hi), v| (lo.min(*v), hi.max(*v)));
                let swing = (hi - lo) / 2.0 / nominal.max(1e-3) * 100.0;
                let below = (nominal - now) / nominal.max(1e-3) * 100.0;
                let note = if tremulant && chest.has_tremulant && swing > 0.3 {
                    format!("tremulant \u{b1}{swing:.1} %")
                } else if below > 0.5 {
                    format!("\u{2212}{below:.1} % under the chord")
                } else {
                    "steady".into()
                };
                ChestReading {
                    name: self.chest_name(i),
                    nominal_mm: nominal,
                    now_mm: now,
                    trace,
                    note,
                }
            })
            .collect();
        drop(traces);
        let wind_steady = chests
            .iter()
            .all(|c| (c.now_mm - c.nominal_mm).abs() / c.nominal_mm.max(1e-3) < 0.04);

        // The registration, when the controller is free; else as it was.
        if let Ok(mut controller) = self.shared.controller.try_lock() {
            let engine = controller.engine();
            let text = registration_text(engine, organ, console, general, stops);
            *self.last_registration.borrow_mut() = text;
        }
        let (summary, history, why) = self.last_registration.borrow().clone();

        let temperament = TEMPERAMENT_NAMES
            .get(params.temperament.index())
            .copied()
            .unwrap_or("");
        let key = params.temperament_key.index();
        let tuning = if key == 0 {
            format!("{temperament} \u{b7} A {:.0}", params.pitch.value())
        } else {
            format!(
                "{temperament} on {} \u{b7} A {:.0}",
                KEY_NAMES.get(key).copied().unwrap_or(""),
                params.pitch.value()
            )
        };
        let cpu = format!(
            "CPU {:.0} % \u{b7} {} voices",
            (telemetry.load() * 100.0).max(0.0),
            telemetry.voices()
        );

        Snapshot {
            look: self.look,
            organ: Arc::clone(organ),
            layout: Rc::clone(&self.layout),
            drawn: (0..organ.stops().len())
                .map(|n| stops & (1 << n) != 0)
                .collect(),
            couplers: (0..organ.couplers().len())
                .map(|n| couplers & (1 << n) != 0)
                .collect(),
            tremulant,
            general,
            generals_set,
            swell: params.swell.value(),
            held,
            chests,
            summary,
            history,
            why,
            tuning,
            wind_steady,
            cpu,
        }
    }

    /// A chest by the division it feeds, if one: "Récit" for "Récit chest".
    fn chest_name(&self, index: usize) -> String {
        let organ = &self.organ;
        let Some(chest) = organ.windchests().get(index) else {
            return String::new();
        };
        organ
            .divisions()
            .iter()
            .find(|d| d.windchests.first() == Some(&chest.id))
            .map_or_else(|| chest.name.clone(), |d| d.name.clone())
    }
}

/// How a step of the history reads: a general it matches, else what it
/// drew and retired.
fn step_label(
    organ: &Organ,
    console: &crate::console::ConsoleMemory,
    state: &RegistrationState,
    before: Option<&RegistrationState>,
) -> String {
    let Some(before) = before else {
        return "Opening".into();
    };
    let stops = state.stops.to_mask();
    let couplers = state.couplers.iter().fold(0u32, |m, c| {
        m | 1u32.checked_shl(u32::from(c.0)).unwrap_or(0)
    });
    if let Some(n) = (0..GENERALS).find(|&n| {
        console
            .general(n)
            .is_some_and(|g| g != (0, 0) && g == (stops, couplers))
    }) {
        return format!("General {}", n + 1);
    }
    let name = |id: StopId| {
        organ
            .stops()
            .get(id.index())
            .map_or_else(String::new, |s| bare_name(&s.name).to_string())
    };
    let added: Vec<StopId> = state
        .stops
        .ids()
        .iter()
        .copied()
        .filter(|s| !before.stops.contains(*s))
        .collect();
    let removed: Vec<StopId> = before
        .stops
        .ids()
        .iter()
        .copied()
        .filter(|s| !state.stops.contains(*s))
        .collect();
    match (added.as_slice(), removed.as_slice()) {
        ([], []) => "Couplers".into(),
        ([one], []) => format!("+ {}", name(*one)),
        ([a, b], []) => format!("+ {}, {}", name(*a), name(*b)),
        ([], [one]) => format!("\u{2212} {}", name(*one)),
        ([], _) if state.stops.is_empty() => "Cancel".into(),
        _ => format!("+{} \u{2212}{}", added.len(), removed.len()),
    }
}

/// The registration panel's text: the summary, the last steps, and why the
/// last step's stops are drawn.
fn registration_text(
    engine: &RegistrationEngine,
    organ: &Organ,
    console: &crate::console::ConsoleMemory,
    general: Option<usize>,
    stops: u64,
) -> RegistrationText {
    let drawn = stops.count_ones();
    let summary = match general {
        Some(n) => {
            let tag = FACTORY_GENERALS.get(n).map(|g| g.tag);
            match tag {
                Some(tag) => format!("{drawn} stops drawn \u{b7} General {} ({tag})", n + 1),
                None => format!("{drawn} stops drawn \u{b7} General {}", n + 1),
            }
        }
        None => format!("{drawn} stops drawn"),
    };
    let nodes = engine.history();
    let mut path = vec![engine.current_node()];
    while let Some(parent) = path
        .last()
        .and_then(|&n| nodes.get(n))
        .and_then(|n| n.parent)
    {
        path.push(parent);
    }
    path.reverse();
    let label_of = |i: usize| {
        let node = &nodes[i];
        let before = node.parent.map(|p| &nodes[p].state);
        step_label(organ, console, &node.state, before)
    };
    let last = path.len().saturating_sub(5);
    let history = path[last..]
        .iter()
        .map(|&i| (label_of(i), i == engine.current_node()))
        .collect();

    let current = &nodes[engine.current_node()];
    let mut why = Vec::new();
    if let Some(parent) = current.parent {
        let before = &nodes[parent].state;
        for stop in current
            .state
            .stops
            .ids()
            .iter()
            .copied()
            .filter(|s| !before.stops.contains(*s))
            .take(2)
        {
            let explained = engine.explain(stop);
            let by = match explained.reason {
                Reason::NotEngaged => continue,
                Reason::Direct => "drawn by hand".to_string(),
                Reason::SelectorMatch => "drawn by a selector".to_string(),
                Reason::PlenumPrincipal | Reason::PlenumMixture | Reason::PlenumReed => {
                    "drawn for the plenum".to_string()
                }
                Reason::IntentPort => "drawn from another organ's registration".to_string(),
                Reason::SnapshotRecall => match label_of(explained.node).as_str() {
                    l if l.starts_with("General") => format!("drawn by {l}"),
                    _ => "drawn from the host".to_string(),
                },
            };
            why.push(Why {
                drawn: true,
                lead: organ
                    .stops()
                    .get(stop.index())
                    .map_or_else(String::new, |s| s.name.clone()),
                rest: format!(" {by}"),
            });
        }
        for stop in before
            .stops
            .ids()
            .iter()
            .copied()
            .filter(|s| !current.state.stops.contains(*s))
            .take(2usize.saturating_sub(why.len()))
        {
            why.push(Why {
                drawn: false,
                lead: organ
                    .stops()
                    .get(stop.index())
                    .map_or_else(String::new, |s| s.name.clone()),
                rest: " retired".into(),
            });
        }
    } else {
        why.push(Why {
            drawn: false,
            lead: "The opening registration.".into(),
            rest: " Every change can be undone, and every stop says why it is drawn.".into(),
        });
    }
    (summary, history, why)
}
