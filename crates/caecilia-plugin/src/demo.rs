// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demonstrations: short pieces the instrument plays on itself, so an
//! organ can be heard without a keyboard.
//!
//! A piece is written for three parts -- the great, the swell and the pedal
//! -- and the console gives each part the MIDI channel of the organ playing.
//! The audio thread plays it, each note on its own sample, and the keys
//! light as they would under a player's hands.

use caecilia_model::TonalFamily;

/// The parts a piece is written for.
pub(crate) const GREAT: u8 = 0;
pub(crate) const SWELL: u8 = 1;
pub(crate) const PEDAL: u8 = 2;

/// One step of a line: how long, in beats, and the keys held for it (none
/// for a rest).
type Step = (f32, &'static [u8]);

/// A line of a piece: one part, from `start` beats on.
pub(crate) struct Line {
    part: u8,
    start: f32,
    steps: &'static [Step],
}

/// A piece: its name, its pace, the stops it wants drawn, and its lines.
pub(crate) struct Piece {
    pub(crate) name: &'static str,
    /// What the history calls the registration it draws.
    pub(crate) short: &'static str,
    /// Seconds a beat lasts.
    beat: f32,
    /// For each part, the families of the stops drawn on it.
    pub(crate) registration: &'static [(u8, &'static [TonalFamily])],
    lines: &'static [Line],
}

/// How much of a step a key is held: organists lift between notes.
const LEGATO: f32 = 0.94;

/// The silence left after a piece before it says it is over.
const TAIL_SECONDS: f32 = 0.6;

use TonalFamily::{Flute, Mixture, Principal, Reed};

/// The pieces the console offers, in its order.
pub(crate) static PIECES: [Piece; 4] = [
    Piece {
        name: "J. S. Bach \u{2014} Toccata in D minor, BWV 565 (opening)",
        short: "Toccata",
        beat: 0.5,
        registration: &[
            (GREAT, &[Principal, Mixture, Reed]),
            (PEDAL, &[Principal, Reed, Flute]),
        ],
        lines: &[
            Line {
                part: GREAT,
                start: 0.0,
                steps: &[
                    (0.25, &[69, 81]),
                    (0.25, &[67, 79]),
                    (1.5, &[69, 81]),
                    (0.5, &[]),
                    (0.25, &[67, 79]),
                    (0.25, &[65, 77]),
                    (0.25, &[64, 76]),
                    (0.25, &[62, 74]),
                    (1.0, &[61, 73]),
                    (1.5, &[62, 74]),
                    (0.5, &[]),
                    (5.0, &[62, 65, 69, 74]),
                ],
            },
            Line {
                part: PEDAL,
                start: 6.5,
                steps: &[(5.0, &[38])],
            },
        ],
    },
    Piece {
        name: "J. S. Bach \u{2014} Jesu, Joy of Man's Desiring (theme)",
        short: "Jesu",
        beat: 0.36,
        registration: &[(SWELL, &[Flute]), (PEDAL, &[Flute])],
        lines: &[
            Line {
                part: SWELL,
                start: 0.0,
                steps: &[
                    (1.0, &[67]),
                    (1.0, &[69]),
                    (1.0, &[71]),
                    (1.0, &[74]),
                    (1.0, &[72]),
                    (1.0, &[72]),
                    (1.0, &[76]),
                    (1.0, &[74]),
                    (1.0, &[74]),
                    (1.0, &[79]),
                    (1.0, &[78]),
                    (1.0, &[79]),
                    (1.0, &[74]),
                    (1.0, &[71]),
                    (1.0, &[67]),
                    (1.0, &[69]),
                    (1.0, &[71]),
                    (1.0, &[72]),
                    (1.0, &[74]),
                    (1.0, &[76]),
                    (1.0, &[74]),
                    (1.0, &[72]),
                    (1.0, &[71]),
                    (1.0, &[69]),
                    (3.0, &[67]),
                ],
            },
            Line {
                part: PEDAL,
                start: 0.0,
                steps: &[
                    (3.0, &[43]),
                    (3.0, &[36]),
                    (3.0, &[43]),
                    (3.0, &[38]),
                    (3.0, &[43]),
                    (3.0, &[45]),
                    (3.0, &[47]),
                    (3.0, &[38]),
                    (3.0, &[43]),
                ],
            },
        ],
    },
    Piece {
        name: "D. Buxtehude \u{2014} Praeludium (excerpt)",
        short: "Praeludium",
        beat: 0.42,
        registration: &[
            (GREAT, &[Principal, Mixture, Flute]),
            (PEDAL, &[Principal, Flute]),
        ],
        lines: &[
            Line {
                part: GREAT,
                start: 0.0,
                steps: &[
                    (0.5, &[74]),
                    (0.5, &[77]),
                    (0.5, &[81]),
                    (0.5, &[79]),
                    (0.5, &[77]),
                    (0.5, &[74]),
                    (0.5, &[73]),
                    (1.0, &[74]),
                ],
            },
            Line {
                part: PEDAL,
                start: 4.85,
                steps: &[
                    (1.0, &[38]),
                    (1.0, &[41]),
                    (1.0, &[43]),
                    (1.0, &[45]),
                    (1.0, &[43]),
                    (1.0, &[41]),
                    (2.0, &[38]),
                ],
            },
        ],
    },
    Piece {
        name: "A cadence in C, on the full organ",
        short: "Cadence",
        beat: 0.8,
        registration: &[
            (GREAT, &[Principal, Mixture, Reed, Flute]),
            (PEDAL, &[Principal, Reed, Flute]),
        ],
        lines: &[
            Line {
                part: GREAT,
                start: 0.0,
                steps: &[
                    (2.0, &[60, 64, 67, 72]),
                    (2.0, &[60, 65, 69, 72]),
                    (1.0, &[60, 64, 67, 72]),
                    (1.0, &[62, 65, 67, 71]),
                    (4.0, &[60, 64, 67, 72]),
                ],
            },
            Line {
                part: PEDAL,
                start: 0.0,
                steps: &[
                    (2.0, &[36]),
                    (2.0, &[41]),
                    (1.0, &[43]),
                    (1.0, &[43]),
                    (4.0, &[36]),
                ],
            },
        ],
    },
];

/// The phrase each stop plays alone in the tour of the stops: up and
/// down a chord, on whatever part it is sent to.
pub(crate) static PHRASE: Piece = Piece {
    name: "Every stop, one by one",
    short: "Tour",
    beat: 0.2,
    registration: &[],
    lines: &[Line {
        part: GREAT,
        start: 0.0,
        steps: &[
            (1.0, &[60]),
            (1.0, &[64]),
            (1.0, &[67]),
            (1.0, &[72]),
            (1.0, &[67]),
            (1.0, &[64]),
            (3.0, &[60]),
        ],
    }],
};

/// Which piece: one of [`PIECES`], or the tour's [`PHRASE`].
pub(crate) fn piece(index: u8) -> &'static Piece {
    PIECES.get(usize::from(index)).unwrap_or(&PHRASE)
}

/// The index that means [`PHRASE`].
pub(crate) const PHRASE_INDEX: u8 = u8::MAX;

impl Piece {
    /// How long it lasts, in seconds, its last key lifted.
    pub(crate) fn seconds(&self) -> f32 {
        self.lines
            .iter()
            .map(|l| (l.start + l.steps.iter().map(|s| s.0).sum::<f32>()) * self.beat)
            .fold(0.0, f32::max)
    }

    /// Every key it plays: its part, its key, and when it goes down and up,
    /// in seconds.
    fn each(&self, mut f: impl FnMut(u8, u8, f32, f32)) {
        for line in self.lines {
            let mut t = line.start;
            for (len, keys) in line.steps {
                let on = t * self.beat;
                let off = (t + len * LEGATO) * self.beat;
                for &key in *keys {
                    f(line.part, key, on, off);
                }
                t += len;
            }
        }
    }
}

/// A piece playing, on the audio thread: nothing here allocates.
pub(crate) struct Player {
    piece: Option<&'static Piece>,
    /// The MIDI channel of each part.
    channels: [u8; 3],
    /// Semitones added to every key.
    shift: i8,
    /// Samples played since it began.
    elapsed: u64,
}

impl Player {
    pub(crate) const fn new() -> Self {
        Self {
            piece: None,
            channels: [0; 3],
            shift: 0,
            elapsed: 0,
        }
    }

    /// The piece playing, if any.
    pub(crate) fn playing(&self) -> Option<&'static Piece> {
        self.piece
    }

    fn key(&self, part: u8, key: u8) -> (u8, u8) {
        let channel = self.channels[usize::from(part.min(2))];
        (channel, key.saturating_add_signed(self.shift).min(127))
    }

    /// Begin `piece` with its parts on `channels`, after lifting whatever
    /// was playing.
    pub(crate) fn start(
        &mut self,
        piece: &'static Piece,
        channels: [u8; 3],
        shift: i8,
        sample_rate: f64,
        emit: impl FnMut(u32, u8, u8, bool),
    ) {
        self.stop(sample_rate, emit);
        self.piece = Some(piece);
        self.channels = channels;
        self.shift = shift;
        self.elapsed = 0;
    }

    /// Lift every key the piece holds, and stop.
    pub(crate) fn stop(&mut self, sample_rate: f64, mut emit: impl FnMut(u32, u8, u8, bool)) {
        let Some(piece) = self.piece.take() else {
            return;
        };
        let now = self.elapsed;
        let samples = |s: f32| (f64::from(s) * sample_rate) as u64;
        piece.each(|part, key, on, off| {
            if samples(on) <= now && now < samples(off) {
                let (c, k) = self.key(part, key);
                emit(0, c, k, false);
            }
        });
    }

    /// The keys that go down or up in the next `frames` samples, each at its
    /// offset in the block.
    pub(crate) fn render(
        &mut self,
        frames: u32,
        sample_rate: f64,
        mut emit: impl FnMut(u32, u8, u8, bool),
    ) {
        let Some(piece) = self.piece else {
            return;
        };
        let from = self.elapsed;
        let to = from + u64::from(frames);
        let samples = |s: f32| (f64::from(s) * sample_rate) as u64;
        // Ups before downs at the same sample, so a repeated key speaks again.
        for down in [false, true] {
            piece.each(|part, key, on, off| {
                let at = samples(if down { on } else { off });
                if (from..to).contains(&at) {
                    let (c, k) = self.key(part, key);
                    emit((at - from) as u32, c, k, down);
                }
            });
        }
        self.elapsed = to;
        if to > samples(piece.seconds() + TAIL_SECONDS) {
            self.piece = None;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_key_that_goes_down_comes_up() {
        for piece in PIECES.iter().chain([&PHRASE]) {
            let mut player = Player::new();
            let mut held = [0u128; 16];
            let mut downs = 0;
            player.start(piece, [0, 1, 2], 0, 48_000.0, |_, _, _, _| {});
            while player.playing().is_some() {
                player.render(256, 48_000.0, |_, c, k, on| {
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
            assert!(downs > 4, "{}", piece.name);
            assert_eq!(held, [0; 16], "{} leaves keys down", piece.name);
        }
    }

    #[test]
    fn stopping_lifts_the_keys_held() {
        let mut player = Player::new();
        let mut held = 0u32;
        player.start(&PIECES[0], [0, 1, 2], 0, 48_000.0, |_, _, _, _| {});
        // Into the last chord.
        for _ in 0..(48_000 * 5 / 256) {
            player.render(256, 48_000.0, |_, _, _, on| {
                if on {
                    held += 1;
                } else {
                    held -= 1;
                }
            });
        }
        assert!(held > 0);
        player.stop(48_000.0, |_, _, _, on| {
            assert!(!on);
            held -= 1;
        });
        assert_eq!(held, 0);
        assert!(player.playing().is_none());
    }
}
