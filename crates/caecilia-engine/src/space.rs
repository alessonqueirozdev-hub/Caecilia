// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Where the pipes stand, and the room around them.
//!
//! Every pipe of every rank has its own place: the great's across the front
//! of the case on its C and C# sides, the swell's behind and above it, the
//! pedal's along the back, wide. A listener some way down the nave hears each
//! from its own angle and distance. A real listener would also hear the whole
//! organ late, by the time sound takes to arrive; the player must not, so
//! only the differences between pipes are kept as delay. The nave's first
//! reflections follow, one set per room, with the chest's sound as their
//! source.

use caecilia_model::{DivisionKind, MidiNote, Organ, RankVoicing, StopId};
use manguebyte_dsp::space::{
    Arrangement, Chest, Listener, Placement, Position, ReflectionSettings, Reflections, Room,
    Speakers, Wall,
};

/// The rooms, in the order [`OrganEngine::set_room`](crate::OrganEngine::set_room)
/// takes them: room, chamber, hall, cathedral, plate.
pub const ROOMS: usize = 5;

/// How far down the nave the listener stands from the front of the case, in
/// metres: where a pipe's distance gain is one.
const LISTENING_DISTANCE: f64 = 9.0;
/// The ear's height, in metres.
const EAR_HEIGHT: f64 = 1.7;
/// How high the great's chest stands, in metres.
const CASE_HEIGHT: f64 = 5.0;
/// What the loudspeakers span either side of centre, in degrees: about what
/// the case spans, so the organ fills the image without collapsing on it.
const IMAGE_HALF_WIDTH: f64 = 24.0;
/// How far behind each other a chest's ranks stand, in metres.
const RANK_ROW: f64 = 0.3;

/// The longest delay a reflection can have, in seconds; the reverb is the
/// tail from there.
const LATEST_REFLECTION: f64 = 0.2;
/// The reflection order: 62 paths for each chest.
const REFLECTION_ORDER: usize = 3;

/// A room's shape: width, length and height in metres, what its walls absorb
/// and above what they darken, and how strong its reflections are against
/// the direct sound (as the root of their summed energy).
struct RoomShape {
    size: (f64, f64, f64),
    absorption: f64,
    high_cut_hz: f64,
    level: f64,
}

/// In the order of [`ROOMS`]. The plate is a device, not a room: no reflections.
const ROOM_SHAPES: [RoomShape; ROOMS] = [
    RoomShape {
        size: (8.0, 16.0, 7.5),
        absorption: 0.3,
        high_cut_hz: 5000.0,
        level: 0.4,
    },
    RoomShape {
        size: (12.0, 20.0, 8.5),
        absorption: 0.22,
        high_cut_hz: 6000.0,
        level: 0.45,
    },
    RoomShape {
        size: (18.0, 32.0, 13.0),
        absorption: 0.15,
        high_cut_hz: 7000.0,
        level: 0.5,
    },
    RoomShape {
        size: (24.0, 60.0, 25.0),
        absorption: 0.08,
        high_cut_hz: 8000.0,
        level: 0.6,
    },
    RoomShape {
        size: (12.0, 20.0, 8.5),
        absorption: 0.2,
        high_cut_hz: 6000.0,
        level: 0.0,
    },
];

/// A chest's place in the case: across, back and up from the front centre of
/// the case in metres, how wide it is, and how its pipes are set out.
struct Spot {
    back: f64,
    up: f64,
    width: f64,
    arrangement: Arrangement,
}

fn spot_of(organ: &Organ, chest: usize) -> Spot {
    let divisions = || {
        organ
            .divisions()
            .iter()
            .filter(|d| d.windchests.iter().any(|c| c.index() == chest))
    };
    if divisions().any(|d| d.kind == DivisionKind::Pedal) {
        Spot {
            back: 2.5,
            up: -1.0,
            width: 9.0,
            arrangement: Arrangement::CAndCSharp {
                largest_outside: true,
            },
        }
    } else if divisions().any(|d| d.enclosed) {
        Spot {
            back: 1.5,
            up: 2.0,
            width: 3.5,
            arrangement: Arrangement::CAndCSharp {
                largest_outside: false,
            },
        }
    } else {
        Spot {
            back: 0.0,
            up: 0.0,
            width: 6.0,
            arrangement: Arrangement::CAndCSharp {
                largest_outside: true,
            },
        }
    }
}

/// Which pipes a stop has in its chest's windchest.
#[derive(Clone, Copy, Debug)]
struct StopPipes {
    first: usize,
    low: MidiNote,
    high: MidiNote,
}

/// Where every pipe of an organ stands, and what the rooms make of it.
pub(crate) struct Layout {
    stops: Vec<Option<StopPipes>>,
    /// Each chest's pipes' placements.
    pub(crate) placements: Vec<Vec<Placement>>,
    /// The latest a pipe arrives, in seconds, for sizing the windchests.
    pub(crate) latest_pipe: f64,
    /// For each room, each chest's reflections and how much to send them.
    pub(crate) rooms: Vec<Vec<(Reflections, f32)>>,
}

fn listener_at(position: Position) -> Option<Listener> {
    let speakers = Speakers::stereo_with_width(IMAGE_HALF_WIDTH).ok()?;
    let mut listener = Listener::new(position, speakers);
    listener.reference_distance = LISTENING_DISTANCE;
    Some(listener)
}

impl Layout {
    /// The layout of `organ`, whose ranks are voiced as `voicings`; `None` if
    /// the space cannot be built. Allocates: off the audio thread.
    pub(crate) fn new(organ: &Organ, voicings: &[RankVoicing], chests: usize) -> Option<Self> {
        let listener = listener_at(Position::new(0.0, 0.0, EAR_HEIGHT))?;
        let mut stops = vec![None; organ.stops().len()];
        let mut placements: Vec<Vec<Placement>> = vec![Vec::new(); chests];
        let mut rows = vec![0usize; chests];
        for (i, stop) in organ.stops().iter().enumerate() {
            let chest = voicings.get(i)?.chest.index();
            let chest = if chest < chests { chest } else { 0 };
            let division = organ.division(stop.division)?;
            let (low, high) = (division.low_note, division.high_note);
            let count = usize::from(high.saturating_sub(low)) + 1;
            let spot = spot_of(organ, chest);
            let row = Chest::new(
                Position::new(
                    0.0,
                    LISTENING_DISTANCE + spot.back + rows[chest] as f64 * RANK_ROW,
                    CASE_HEIGHT + spot.up,
                ),
                spot.width,
                spot.arrangement,
            );
            rows[chest] += 1;
            let first = placements[chest].len();
            let positions = row.positions(&vec![0.0; count]);
            placements[chest].extend(positions.into_iter().map(|p| listener.place(p)));
            stops[i] = Some(StopPipes { first, low, high });
        }
        // The player hears the differences, not the walk down the nave.
        let earliest = placements
            .iter()
            .flatten()
            .map(|p| p.delay_seconds)
            .fold(f64::INFINITY, f64::min);
        let mut latest_pipe: f64 = 0.0;
        for p in placements.iter_mut().flatten() {
            p.delay_seconds -= earliest;
            latest_pipe = latest_pipe.max(p.delay_seconds);
        }

        let settings = ReflectionSettings {
            max_order: REFLECTION_ORDER,
            direct: false,
            max_delay_seconds: LATEST_REFLECTION,
        };
        let mut rooms = Vec::with_capacity(ROOMS);
        for shape in &ROOM_SHAPES {
            let (width, length, height) = shape.size;
            let wall = Wall::new(shape.absorption).with_high_cut(shape.high_cut_hz);
            let room = Room::new(width, length, height, wall);
            // The case against the far wall, the listener down the nave; a
            // small room brings the walls in, not the listener closer.
            let front = length - 4.0;
            let listener = listener_at(Position::new(
                width / 2.0,
                (front - LISTENING_DISTANCE).max(1.0),
                EAR_HEIGHT,
            ))?;
            let mut each_chest = Vec::with_capacity(chests);
            for chest in 0..chests {
                let spot = spot_of(organ, chest);
                let source = Position::new(
                    width / 2.0,
                    front + spot.back,
                    (CASE_HEIGHT + spot.up).min(height - 0.5),
                );
                let reflections = Reflections::compute(&room, source, &listener, &settings).ok()?;
                let energy: f64 = reflections.taps().iter().map(|t| t.gain * t.gain).sum();
                let send = if energy > 0.0 {
                    shape.level / energy.sqrt()
                } else {
                    0.0
                };
                each_chest.push((reflections, send as f32));
            }
            rooms.push(each_chest);
        }
        Some(Self {
            stops,
            placements,
            latest_pipe,
            rooms,
        })
    }

    /// The most paths a chest's reflections can have, in any room.
    pub(crate) fn most_paths() -> usize {
        ReflectionSettings::paths_up_to(REFLECTION_ORDER)
    }

    /// The longest delay a chest's reflections can have, in any room.
    pub(crate) fn latest_reflection() -> f64 {
        LATEST_REFLECTION
    }

    /// `stop`'s pipe for `note` in its chest's windchest, if it has one.
    pub(crate) fn pipe(&self, stop: StopId, note: MidiNote) -> Option<usize> {
        let s = (*self.stops.get(stop.index())?)?;
        (s.low <= note && note <= s.high).then(|| s.first + usize::from(note - s.low))
    }
}
