// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demonstrations: pieces the instrument plays on itself, so an organ
//! can be heard without a keyboard, each with its registrations.
//!
//! A piece comes from a MIDI file in the Demos folder (see
//! [`crate::demo_files`]) and is written for three parts -- the great, the
//! swell and the pedal -- to which the console gives the MIDI channels of
//! the organ playing. The files are read once, off the audio thread, into
//! scores of key events in time; the audio thread only walks them, each key
//! on its own sample, and the keys light as they would under a player's
//! hands. Besides the pieces there is the tour's phrase, which each stop
//! plays alone.

use std::sync::OnceLock;

use caecilia_model::TonalFamily;

/// The parts a piece is written for.
pub(crate) const GREAT: u8 = 0;
pub(crate) const SWELL: u8 = 1;
pub(crate) const PEDAL: u8 = 2;

/// The stops a part draws: by name, as the Caecilia Pipe Organ has them;
/// on an organ without any of those names, every stop of the families.
pub(crate) struct Draw {
    pub(crate) part: u8,
    pub(crate) names: &'static [&'static str],
    pub(crate) families: &'static [TonalFamily],
}

/// A registration: the stops each part draws, the couplers by name, and
/// the tremulant.
pub(crate) struct Registration {
    pub(crate) draws: &'static [Draw],
    pub(crate) couplers: &'static [&'static str],
    pub(crate) tremulant: bool,
}

/// A registration from `seconds` into a piece on; what the history calls it.
pub(crate) struct Change {
    pub(crate) seconds: f32,
    pub(crate) label: &'static str,
    pub(crate) registration: Registration,
}

/// The silence left after a piece before it says it is over.
const TAIL_SECONDS: f32 = 0.6;

/// What the console calls the tour.
pub(crate) const TOUR_NAME: &str = "Every stop, one by one";
/// What the history calls the tour's registrations.
pub(crate) const TOUR_SHORT: &str = "Tour";

/// The phrase each stop plays alone in the tour: up and down a chord, each
/// key its time in seconds.
const PHRASE: [(u8, f32); 7] = [
    (60, 0.2),
    (64, 0.2),
    (67, 0.2),
    (72, 0.2),
    (67, 0.2),
    (64, 0.2),
    (60, 0.6),
];

/// The index that means the tour's phrase.
pub(crate) const PHRASE_INDEX: u8 = u8::MAX;

/// A key going down or up, `at` seconds into the piece.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct Event {
    pub(crate) at: f32,
    pub(crate) part: u8,
    pub(crate) key: u8,
    pub(crate) on: bool,
}

/// A piece's key events in time, ups before downs at the same moment, and
/// how long it lasts.
pub(crate) struct Score {
    events: Vec<Event>,
    seconds: f32,
}

#[cfg(test)]
impl Score {
    /// How long it lasts, in seconds, its last key lifted.
    pub(crate) fn seconds(&self) -> f32 {
        self.seconds
    }

    /// How many keys go down or up in it.
    pub(crate) fn event_count(&self) -> usize {
        self.events.len()
    }
}

/// A piece as the console lists it and the audio thread plays it.
pub(crate) struct Demo {
    pub(crate) name: String,
    /// What a row calls it.
    pub(crate) short: String,
    /// The registrations, the first from the start.
    pub(crate) changes: &'static [Change],
    pub(crate) score: Score,
}

static CATALOG: OnceLock<Vec<Demo>> = OnceLock::new();

/// Read every piece; done when the instrument is made, so the audio thread
/// only ever reads the scores.
pub(crate) fn prepare() {
    catalog();
}

/// The tour's phrase as a score.
fn phrase() -> Score {
    let mut at = 0.0;
    let mut events = Vec::new();
    for (key, seconds) in PHRASE {
        for (at, on) in [(at, true), (at + seconds * 0.94, false)] {
            events.push(Event {
                at,
                part: GREAT,
                key,
                on,
            });
        }
        at += seconds;
    }
    score_of(events)
}

/// The pieces, then the tour's phrase.
fn catalog() -> &'static [Demo] {
    CATALOG.get_or_init(|| {
        let mut demos = crate::demo_files::load();
        demos.push(Demo {
            name: TOUR_NAME.to_string(),
            short: TOUR_NAME.to_string(),
            changes: &[],
            score: phrase(),
        });
        demos
    })
}

/// The pieces the console lists, in its order.
pub(crate) fn demos() -> &'static [Demo] {
    let all = catalog();
    &all[..all.len() - 1]
}

/// The score of piece `index`, or of the tour's phrase.
pub(crate) fn score(index: u8) -> &'static Score {
    let all = catalog();
    &all.get(usize::from(index))
        .unwrap_or(&all[all.len() - 1])
        .score
}

/// Key events as a score, in time.
pub(crate) fn score_of(mut raw: Vec<Event>) -> Score {
    raw.sort_by(|a, b| a.at.total_cmp(&b.at).then(a.on.cmp(&b.on)));
    // Two lines on one key of one part: it goes down with the first and up
    // with the last.
    let mut down = [[0u16; 128]; 3];
    let mut events = Vec::with_capacity(raw.len());
    let mut seconds: f32 = 0.0;
    for e in raw {
        let count = &mut down[usize::from(e.part.min(2))][usize::from(e.key & 0x7F)];
        if e.on {
            *count += 1;
            if *count == 1 {
                events.push(e);
            }
        } else {
            *count = count.saturating_sub(1);
            if *count == 0 {
                events.push(e);
            }
        }
        seconds = seconds.max(e.at);
    }
    Score { events, seconds }
}

/// A piece playing, on the audio thread: nothing here allocates.
pub(crate) struct Player {
    /// The piece playing, if any, and its score.
    playing: Option<(u8, &'static Score)>,
    /// The MIDI channel of each part.
    channels: [u8; 3],
    /// Semitones added to every key.
    shift: i8,
    /// Samples played since it began.
    elapsed: u64,
    /// The next event of the score.
    next: usize,
    /// The keys down, by part.
    held: [u128; 3],
}

impl Player {
    pub(crate) const fn new() -> Self {
        Self {
            playing: None,
            channels: [0; 3],
            shift: 0,
            elapsed: 0,
            next: 0,
            held: [0; 3],
        }
    }

    /// The piece playing, if any.
    pub(crate) fn playing(&self) -> Option<u8> {
        self.playing.map(|p| p.0)
    }

    fn key(&self, part: u8, key: u8) -> (u8, u8) {
        let channel = self.channels[usize::from(part.min(2))];
        (channel, key.saturating_add_signed(self.shift).min(127))
    }

    /// Begin piece `index` with its parts on `channels`, after lifting
    /// whatever was playing.
    pub(crate) fn start(
        &mut self,
        index: u8,
        channels: [u8; 3],
        shift: i8,
        emit: impl FnMut(u32, u8, u8, bool),
    ) {
        self.stop(emit);
        self.playing = Some((index, score(index)));
        self.channels = channels;
        self.shift = shift;
        self.elapsed = 0;
        self.next = 0;
    }

    /// Lift every key the piece holds, and stop.
    pub(crate) fn stop(&mut self, mut emit: impl FnMut(u32, u8, u8, bool)) {
        if self.playing.take().is_none() {
            return;
        }
        for part in 0..3u8 {
            let mut held = self.held[usize::from(part)];
            while held != 0 {
                let key = held.trailing_zeros() as u8;
                held &= held - 1;
                let (c, k) = self.key(part, key);
                emit(0, c, k, false);
            }
        }
        self.held = [0; 3];
    }

    /// The keys that go down or up in the next `frames` samples, each at its
    /// offset in the block.
    pub(crate) fn render(
        &mut self,
        frames: u32,
        sample_rate: f64,
        mut emit: impl FnMut(u32, u8, u8, bool),
    ) {
        let Some((_, score)) = self.playing else {
            return;
        };
        let from = self.elapsed;
        let to = from + u64::from(frames);
        let samples = |s: f32| (f64::from(s) * sample_rate) as u64;
        while let Some(e) = score.events.get(self.next) {
            let at = samples(e.at);
            if at >= to {
                break;
            }
            let (c, k) = self.key(e.part, e.key);
            emit(at.saturating_sub(from) as u32, c, k, e.on);
            let bit = 1u128 << (e.key & 0x7F);
            let held = &mut self.held[usize::from(e.part.min(2))];
            if e.on {
                *held |= bit;
            } else {
                *held &= !bit;
            }
            self.next += 1;
        }
        self.elapsed = to;
        if self.next >= score.events.len() && to > samples(score.seconds + TAIL_SECONDS) {
            self.playing = None;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_key_that_goes_down_comes_up() {
        let mut player = Player::new();
        let mut held = [0u128; 16];
        let mut downs = 0;
        player.start(PHRASE_INDEX, [0, 1, 2], 0, |_, _, _, _| {});
        while player.playing().is_some() {
            player.render(256, 48_000.0, |at, c, k, on| {
                assert!(at < 256);
                let bit = 1u128 << k;
                if on {
                    assert_eq!(held[usize::from(c)] & bit, 0, "{} twice", k);
                    held[usize::from(c)] |= bit;
                    downs += 1;
                } else {
                    held[usize::from(c)] &= !bit;
                }
            });
        }
        assert_eq!(downs, PHRASE.len());
        assert_eq!(held, [0; 16]);
    }

    #[test]
    fn a_key_two_lines_share_goes_down_with_the_first_and_up_with_the_last() {
        let e = |at, on| Event {
            at,
            part: GREAT,
            key: 60,
            on,
        };
        let s = score_of(vec![
            e(0.0, true),
            e(2.0, false),
            e(1.0, true),
            e(3.0, false),
        ]);
        assert_eq!(s.events, [e(0.0, true), e(3.0, false)]);
        assert_eq!(s.seconds(), 3.0);
    }

    #[test]
    fn stopping_lifts_the_keys_held() {
        let mut player = Player::new();
        let mut held = 0u32;
        player.start(PHRASE_INDEX, [0, 1, 2], 0, |_, _, _, _| {});
        player.render(256, 48_000.0, |_, _, _, on| {
            if on {
                held += 1;
            }
        });
        assert_eq!(held, 1);
        player.stop(|_, _, _, on| {
            assert!(!on);
            held -= 1;
        });
        assert_eq!(held, 0);
        assert!(player.playing().is_none());
    }
}
