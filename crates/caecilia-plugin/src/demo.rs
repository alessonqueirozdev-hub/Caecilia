// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The demonstrations: whole pieces the instrument plays on itself, so an
//! organ can be heard without a keyboard, each with registrations that
//! change between its verses.
//!
//! A piece is written for three parts -- the great, the swell and the pedal
//! -- and the console gives each part the MIDI channel of the organ playing.
//! Its lines are written as text: notes (`G4:2 B4:0.5 G4:0.5`), chord
//! symbols the accompaniment voices itself (`G:3 Em D7`), or a bass that
//! follows them. The text is compiled once, off the audio thread, into a
//! score of key events in time; the audio thread only walks it, each key on
//! its own sample, and the keys light as they would under a player's hands.

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

/// A registration from `beat` on; what the history calls it.
pub(crate) struct Change {
    pub(crate) beat: f32,
    pub(crate) label: &'static str,
    pub(crate) registration: Registration,
}

/// How a line's text is read.
enum Kind {
    /// Notes and chords spelled out: `C4`, `F#3+A3+D4`, `r` for a rest.
    Notes,
    /// Chord symbols (`C`, `Am`, `G7`, `Fmaj7`, `D/F#`), each voiced
    /// between `low` and `high` as close to the last as it goes.
    Chords { low: u8, high: u8 },
    /// The bass of chord symbols: the root, or the note after the slash,
    /// between `low` and `high`, as close to the last as it goes.
    Bass { low: u8, high: u8 },
}

/// A line of a piece: one part, from `start` beats on.
struct Line {
    part: u8,
    start: f32,
    kind: Kind,
    text: &'static str,
}

/// A piece: its name, its pace, its registrations, and its lines.
pub(crate) struct Piece {
    pub(crate) name: &'static str,
    /// What the history calls the registration it draws first.
    pub(crate) short: &'static str,
    /// Seconds a beat lasts.
    pub(crate) beat: f32,
    /// The registrations, the first at beat 0.
    pub(crate) changes: &'static [Change],
    lines: &'static [Line],
}

/// The silence left after a piece before it says it is over.
const TAIL_SECONDS: f32 = 0.6;

use TonalFamily::{Flute, Mixture, Principal, Reed, String as Strings};

const fn draw(part: u8, names: &'static [&'static str], families: &'static [TonalFamily]) -> Draw {
    Draw {
        part,
        names,
        families,
    }
}

const fn notes(part: u8, start: f32, text: &'static str) -> Line {
    Line {
        part,
        start,
        kind: Kind::Notes,
        text,
    }
}

const fn chords(part: u8, start: f32, low: u8, high: u8, text: &'static str) -> Line {
    Line {
        part,
        start,
        kind: Kind::Chords { low, high },
        text,
    }
}

const fn bass(start: f32, text: &'static str) -> Line {
    Line {
        part: PEDAL,
        start,
        kind: Kind::Bass { low: 36, high: 55 },
        text,
    }
}

// ------------------------------------------------------------------ pieces

const AMAZING_GRACE: &str = "D4:1 | G4:2 B4:0.5 G4:0.5 | B4:2 A4:1 | G4:2 E4:1 | D4:2 D4:1 | \
     G4:2 B4:0.5 G4:0.5 | B4:2 A4:1 | D5:3 | D5:2 B4:1 | D5:1.5 B4:0.5 D5:0.5 B4:0.5 | \
     G4:2 D4:1 | E4:1.5 G4:0.5 E4:1 | D4:2 D4:1 | G4:2 B4:0.5 G4:0.5 | B4:2 A4:1 | G4:3 | r:2";
const AMAZING_GRACE_CHORDS: &str = "G:3 G C G | G Em D D | G G7 C G | Em D G G:2 D7:1";
const AMAZING_GRACE_LAST: &str = "G:3 G C G | G Em D D | G G7 C G | Em D G G:5";
const SILENT_NIGHT: &str = "G4:3 A4:1 G4:2 | E4:6 | G4:3 A4:1 G4:2 | E4:6 | D5:4 D5:2 | B4:6 | \
     C5:4 C5:2 | G4:6 | A4:4 A4:2 | C5:3 B4:1 A4:2 | G4:3 A4:1 G4:2 | E4:6 | A4:4 A4:2 | \
     C5:3 B4:1 A4:2 | G4:3 A4:1 G4:2 | E4:6 | D5:4 D5:2 | F5:3 D5:1 B4:2 | C5:6 | E5:6 | \
     C5:3 G4:1 E4:2 | G4:3 F4:1 D4:2 | C4:6 | r:6";
const SILENT_NIGHT_CHORDS: &str = "C:6 C C C | G7 G7 C C | F F C C | F F C C | \
     G7 G7 C C | C G7 C C";
const ODE_TO_JOY: &str = "F#5:1 F#5 G5 A5 | A5 G5 F#5 E5 | D5 D5 E5 F#5 | F#5:1.5 E5:0.5 E5:2 | \
     F#5:1 F#5 G5 A5 | A5 G5 F#5 E5 | D5 D5 E5 F#5 | E5:1.5 D5:0.5 D5:2 | \
     E5:1 E5 F#5 D5 | E5 F#5:0.5 G5:0.5 F#5:1 D5 | E5 F#5:0.5 G5:0.5 F#5:1 E5 | D5 E5 A4:2 | \
     F#5:1 F#5 G5 A5 | A5 G5 F#5 E5 | D5 D5 E5 F#5 | E5:1.5 D5:0.5 D5:2";
const ODE_TO_JOY_CHORDS: &str = "D:4 A D A | D A D A:2 D:2 | A:4 A:2 D:2 A:4 D:1 E7:1 A:2 | \
     D:4 A D A:2 D:2";
const OLD_HUNDREDTH: &str = "G4:2 G4:1 F#4 E4 D4 G4 A4 B4:2 | B4:2 B4:1 B4 A4 G4 C5 B4 A4:2 | \
     G4:2 A4:1 B4 A4 G4 E4 F#4 G4:2 | D5:2 B4:1 G4 A4 C5 B4 A4 G4:2 | r:2";
const OLD_HUNDREDTH_CHORDS: &str = "G:2 G:1 D Em G C D G:2 | G:2 Em:1 G D Em Am G D:2 | \
     G:2 D:1 G D Em C D G:2 | G:2 Em:1 Em D Am G D G:2 | r:2";
const GREENSLEEVES: &str = "A4:1 | C5:2 D5:1 | E5:1.5 F5:0.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:0.5 B4:1 | \
     C5:2 A4:1 | A4:1.5 G#4:0.5 A4:1 | B4:2 G#4:1 | E4:2 A4:1 | C5:2 D5:1 | \
     E5:1.5 F5:0.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:0.5 B4:1 | C5:1.5 B4:0.5 A4:1 | \
     G#4:1.5 F#4:0.5 G#4:1 | A4:6 | G5:3 | G5:1.5 F#5:0.5 E5:1 | D5:2 B4:1 | \
     G4:1.5 A4:0.5 B4:1 | C5:2 A4:1 | A4:1.5 G#4:0.5 A4:1 | B4:2 G#4:1 | E4:3 | G5:3 | \
     G5:1.5 F#5:0.5 E5:1 | D5:2 B4:1 | G4:1.5 A4:0.5 B4:1 | C5:1.5 B4:0.5 A4:1 | \
     G#4:1.5 F#4:0.5 G#4:1 | A4:5";
const GREENSLEEVES_CHORDS: &str = "Am:3 C G Em | Am E E Am | Am C G Em | Am E Am Am | \
     C Em G Em | Am E E E | C Em G Em | Am E Am Am";
/// The Westminster quarters, each change a bar: G#4 F#4 E4 B3 and its
/// four sisters, an octave doubled as a carillon would.
const WESTMINSTER: &str = "G#4+G#5:1 F#4+F#5 E4+E5 B3+B4:2 r:1 | \
     E4+E5:1 G#4+G#5 F#4+F#5 B3+B4:2 r:1 | E4+E5:1 F#4+F#5 G#4+G#5 E4+E5:2 r:1 | \
     G#4+G#5:1 E4+E5 F#4+F#5 B3+B4:2 r:1 | B3+B4:1 F#4+F#5 G#4+G#5 E4+E5:2 r:1 | \
     G#4+G#5:1 F#4+F#5 E4+E5 B3+B4:2 r:1 | \
     E4+E5:1 G#4+G#5 F#4+F#5 B3+B4:2 r:1 | E4+E5:1 F#4+F#5 G#4+G#5 E4+E5:2 r:1 | \
     G#4+G#5:1 E4+E5 F#4+F#5 B3+B4:2 r:1 | B3+B4:1 F#4+F#5 G#4+G#5 E4+E5:4";
/// One note of the toccata's opening flourish, an octave doubled.
const TOCCATA: &str = "A5+A4:0.125 G5+G4 A5+A4:1.5 r:0.75 G5+G4:0.125 F5+F4 E5+E4 D5+D4 \
     C#5+C#4:0.75 D5+D4:2 r:1 | \
     A4+A3:0.125 G4+G3 A4+A3:1.5 r:0.75 G4+G3:0.125 F4+F3 E4+E3 D4+D3 \
     C#4+C#3:0.75 D4+D3:2 r:1 | \
     A3+A2:0.125 G3+G2 A3+A2:1.5 r:0.75 G3+G2:0.125 F3+F2 E3+E2 D3+D2 \
     C#3+C#2:0.75 D3+D2:2 r:0.5";

/// The pieces the console offers, in its order.
pub(crate) static PIECES: [Piece; 9] = [
    Piece {
        name: "J. Pachelbel \u{2014} Canon in D",
        short: "Canon",
        beat: 0.6,
        changes: &[
            Change {
                beat: 0.0,
                label: "Canon",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8"], &[Flute]),
                        draw(SWELL, &["Bourdon 8", "Fl\u{fb}te octaviante 4"], &[Flute]),
                        draw(PEDAL, &["Soubasse 16", "Bourdon 8"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
            Change {
                beat: 64.0,
                label: "Canon, strings",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8", "Fl\u{fb}te 4"], &[Flute]),
                        draw(
                            SWELL,
                            &["Gambe 8", "Voix C\u{e9}leste 8", "Fl\u{fb}te octaviante 4"],
                            &[Strings],
                        ),
                        draw(PEDAL, &["Soubasse 16", "Bourdon 8"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
            Change {
                beat: 112.0,
                label: "Canon, principals",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Montre 8", "Bourdon 8"], &[Principal]),
                        draw(
                            SWELL,
                            &["Diapason 8", "Prestant 4", "Fl\u{fb}te octaviante 4"],
                            &[Principal, Flute],
                        ),
                        draw(PEDAL, &["Soubasse 16", "Octavebasse 8"], &[Principal]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
        ],
        lines: &[
            notes(
                PEDAL,
                0.0,
                "D3:2 A2 B2 F#2 G2 D2 G2 A2 | D3:2 A2 B2 F#2 G2 D2 G2 A2 | \
                 D3:2 A2 B2 F#2 G2 D2 G2 A2 | D3:2 A2 B2 F#2 G2 D2 G2 A2 | \
                 D3:2 A2 B2 F#2 G2 D2 G2 A2 | D3:2 A2 B2 F#2 G2 D2 G2 A2 | \
                 D3:2 A2 B2 F#2 G2 D2 G2 A2 | D3:2 A2 B2 F#2 G2 D2 G2 A2 | \
                 D3:2 A2 B2 F#2 G2 D2 G2 A2 | D3:2 A2 B2 F#2 G2 D2 G2 A2 | D2:6",
            ),
            chords(
                GREAT,
                16.0,
                55,
                69,
                "D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | \
                 D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | \
                 D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | D:2 A Bm F#m G D G A | D:6",
            ),
            notes(
                SWELL,
                16.0,
                // The first entry, its answer, the walking quarters, the
                // eighths; the same in thirds; the eighths again, the
                // quarters an octave up, and the first entry to close.
                "F#5:2 E5 D5 C#5 B4 A4 B4 C#5 | D5:2 C#5 B4 A4 G4 F#4 G4 E4 | \
                 D4:1 F#4 A4 G4 F#4 D4 F#4 E4 D4 B3 D4 A4 G4 B4 A4 G4 | \
                 D5:0.5 C#5 D5 D4 C#4 A4 E4 F#4 D4 D5 C#5 B4 C#5 F#5 A5 B5 \
                 G5 F#5 E5 G5 F#5 E5 D5 C#5 B4 A4 G4 F#4 E4 G4 F#4 E4 | \
                 A5+F#5:2 G5+E5 F#5+D5 E5+C#5 D5+B4 C#5+A4 D5+B4 E5+C#5 | \
                 F#5+D5:2 E5+C#5 D5+B4 C#5+A4 B4+G4 A4+F#4 B4+G4 G4+E4 | \
                 D5:0.5 C#5 D5 D4 C#4 A4 E4 F#4 D4 D5 C#5 B4 C#5 F#5 A5 B5 \
                 G5 F#5 E5 G5 F#5 E5 D5 C#5 B4 A4 G4 F#4 E4 G4 F#4 E4 | \
                 D5:1 F#5 A5 G5 F#5 D5 F#5 E5 D5 B4 D5 A5 G5 B5 A5 G5 | \
                 F#5:2 E5 D5 C#5 B4 A4 B4 C#5 | D5+F#5+A5:6",
            ),
        ],
    },
    Piece {
        name: "Amazing Grace \u{2014} the Voix Humaine, then the Cornet",
        short: "Amazing Grace",
        beat: 0.52,
        changes: &[
            Change {
                beat: 0.0,
                label: "Amazing Grace",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8"], &[Flute]),
                        draw(SWELL, &["Voix Humaine 8", "Bourdon 8"], &[Reed]),
                        draw(PEDAL, &["Soubasse 16"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: true,
                },
            },
            Change {
                beat: 48.0,
                label: "Amazing Grace, the Cornet",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8", "Fl\u{fb}te 4"], &[Flute]),
                        draw(
                            SWELL,
                            &[
                                "Bourdon 8",
                                "Fl\u{fb}te octaviante 4",
                                "Nasard 2 2/3",
                                "Octavin 2",
                                "Tierce 1 3/5",
                            ],
                            &[Flute],
                        ),
                        draw(PEDAL, &["Soubasse 16", "Bourdon 8"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
        ],
        lines: &[
            notes(SWELL, 0.0, AMAZING_GRACE),
            notes(SWELL, 48.0, AMAZING_GRACE),
            chords(GREAT, 1.0, 50, 64, AMAZING_GRACE_CHORDS),
            chords(GREAT, 49.0, 50, 64, AMAZING_GRACE_LAST),
            bass(1.0, AMAZING_GRACE_CHORDS),
            bass(49.0, AMAZING_GRACE_LAST),
        ],
    },
    Piece {
        name: "F. X. Gruber \u{2014} Silent Night, on the strings",
        short: "Silent Night",
        beat: 0.27,
        changes: &[
            Change {
                beat: 0.0,
                label: "Silent Night",
                registration: Registration {
                    draws: &[
                        draw(SWELL, &["Gambe 8", "Voix C\u{e9}leste 8"], &[Strings]),
                        draw(PEDAL, &["Soubasse 16"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: true,
                },
            },
            Change {
                beat: 144.0,
                label: "Silent Night, a flute over the strings",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8", "Fl\u{fb}te 4"], &[Flute]),
                        draw(SWELL, &["Gambe 8", "Voix C\u{e9}leste 8"], &[Strings]),
                        draw(PEDAL, &["Soubasse 16"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: true,
                },
            },
        ],
        lines: &[
            notes(SWELL, 0.0, SILENT_NIGHT),
            notes(GREAT, 144.0, SILENT_NIGHT),
            chords(SWELL, 0.0, 52, 64, SILENT_NIGHT_CHORDS),
            chords(SWELL, 144.0, 52, 64, SILENT_NIGHT_CHORDS),
            bass(0.0, SILENT_NIGHT_CHORDS),
            bass(144.0, SILENT_NIGHT_CHORDS),
        ],
    },
    Piece {
        name: "L. van Beethoven \u{2014} Ode to Joy, principals then full organ",
        short: "Ode to Joy",
        beat: 0.5,
        changes: &[
            Change {
                beat: 0.0,
                label: "Ode to Joy",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Montre 8", "Prestant 4"], &[Principal]),
                        draw(PEDAL, &["Soubasse 16", "Octavebasse 8"], &[Principal]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
            Change {
                beat: 64.0,
                label: "Ode to Joy, full organ",
                registration: Registration {
                    draws: &[
                        draw(
                            GREAT,
                            &[
                                "Montre 16",
                                "Montre 8",
                                "Prestant 4",
                                "Doublette 2",
                                "Fourniture IV",
                                "Trompette 8",
                                "Clairon 4",
                            ],
                            &[Principal, Mixture, Reed],
                        ),
                        draw(
                            SWELL,
                            &["Trompette 8", "Clairon 4", "Plein Jeu III"],
                            &[Reed, Mixture],
                        ),
                        draw(
                            PEDAL,
                            &[
                                "Contrebasse 16",
                                "Octavebasse 8",
                                "Bombarde 16",
                                "Trompette 8",
                            ],
                            &[Principal, Reed],
                        ),
                    ],
                    couplers: &["R\u{e9}cit/Grand-Orgue", "Grand-Orgue/P\u{e9}dale"],
                    tremulant: false,
                },
            },
        ],
        lines: &[
            notes(GREAT, 0.0, ODE_TO_JOY),
            notes(GREAT, 64.0, ODE_TO_JOY),
            chords(GREAT, 0.0, 57, 69, ODE_TO_JOY_CHORDS),
            chords(GREAT, 64.0, 57, 69, ODE_TO_JOY_CHORDS),
            bass(0.0, ODE_TO_JOY_CHORDS),
            bass(64.0, ODE_TO_JOY_CHORDS),
            chords(GREAT, 128.0, 57, 74, "D:4"),
            bass(128.0, "D:4"),
        ],
    },
    Piece {
        name: "Old Hundredth \u{2014} a hymn on the plenum, then with the reeds",
        short: "Old Hundredth",
        beat: 0.55,
        changes: &[
            Change {
                beat: 0.0,
                label: "Old Hundredth",
                registration: Registration {
                    draws: &[
                        draw(
                            GREAT,
                            &[
                                "Montre 16",
                                "Montre 8",
                                "Bourdon 8",
                                "Prestant 4",
                                "Doublette 2",
                                "Fourniture IV",
                            ],
                            &[Principal, Mixture],
                        ),
                        draw(
                            PEDAL,
                            &["Contrebasse 16", "Soubasse 16", "Octavebasse 8"],
                            &[Principal],
                        ),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
            Change {
                beat: 42.0,
                label: "Old Hundredth, with the reeds",
                registration: Registration {
                    draws: &[
                        draw(
                            GREAT,
                            &[
                                "Montre 16",
                                "Montre 8",
                                "Bourdon 8",
                                "Prestant 4",
                                "Doublette 2",
                                "Fourniture IV",
                                "Trompette 8",
                                "Clairon 4",
                            ],
                            &[Principal, Mixture, Reed],
                        ),
                        draw(
                            SWELL,
                            &["Plein Jeu III", "Trompette 8", "Clairon 4"],
                            &[Mixture, Reed],
                        ),
                        draw(
                            PEDAL,
                            &[
                                "Contrebasse 16",
                                "Soubasse 16",
                                "Octavebasse 8",
                                "Bombarde 16",
                                "Trompette 8",
                            ],
                            &[Principal, Reed],
                        ),
                    ],
                    couplers: &["R\u{e9}cit/Grand-Orgue"],
                    tremulant: false,
                },
            },
        ],
        lines: &[
            notes(GREAT, 0.0, OLD_HUNDREDTH),
            notes(GREAT, 42.0, OLD_HUNDREDTH),
            chords(GREAT, 0.0, 52, 66, OLD_HUNDREDTH_CHORDS),
            chords(GREAT, 42.0, 52, 66, OLD_HUNDREDTH_CHORDS),
            bass(0.0, OLD_HUNDREDTH_CHORDS),
            bass(42.0, OLD_HUNDREDTH_CHORDS),
            chords(GREAT, 84.0, 52, 71, "C:3 G:5"),
            bass(84.0, "C:3 G:5"),
        ],
    },
    Piece {
        name: "Greensleeves \u{2014} the oboe, then a flute",
        short: "Greensleeves",
        beat: 0.42,
        changes: &[
            Change {
                beat: 0.0,
                label: "Greensleeves",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8"], &[Flute]),
                        draw(SWELL, &["Basson-Hautbois 8", "Bourdon 8"], &[Reed]),
                        draw(PEDAL, &["Soubasse 16"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: false,
                },
            },
            Change {
                beat: 96.0,
                label: "Greensleeves, a flute",
                registration: Registration {
                    draws: &[
                        draw(GREAT, &["Bourdon 8"], &[Flute]),
                        draw(
                            SWELL,
                            &["Bourdon 8", "Fl\u{fb}te octaviante 4", "Nasard 2 2/3"],
                            &[Flute],
                        ),
                        draw(PEDAL, &["Soubasse 16"], &[Flute]),
                    ],
                    couplers: &[],
                    tremulant: true,
                },
            },
        ],
        lines: &[
            notes(SWELL, 0.0, GREENSLEEVES),
            notes(SWELL, 96.0, GREENSLEEVES),
            chords(GREAT, 1.0, 52, 64, GREENSLEEVES_CHORDS),
            chords(GREAT, 97.0, 52, 64, GREENSLEEVES_CHORDS),
            bass(1.0, GREENSLEEVES_CHORDS),
            bass(97.0, GREENSLEEVES_CHORDS),
        ],
    },
    Piece {
        name: "The Westminster quarters, and the hour",
        short: "Westminster",
        beat: 0.5,
        changes: &[Change {
            beat: 0.0,
            label: "Westminster",
            registration: Registration {
                draws: &[
                    draw(GREAT, &["Montre 8", "Bourdon 8"], &[Principal]),
                    draw(
                        SWELL,
                        &[
                            "Bourdon 8",
                            "Fl\u{fb}te octaviante 4",
                            "Octavin 2",
                            "Prestant 4",
                        ],
                        &[Flute],
                    ),
                    draw(
                        PEDAL,
                        &["Contrebasse 16", "Soubasse 16", "Bourdon 8"],
                        &[Principal, Flute],
                    ),
                ],
                couplers: &[],
                tremulant: false,
            },
        }],
        lines: &[
            notes(SWELL, 0.0, WESTMINSTER),
            // The hour, struck four times: the great bell and the pedal.
            notes(
                GREAT,
                62.0,
                "E3+B3+E4:2 r:1 E3+B3+E4:2 r:1 E3+B3+E4:2 r:1 E3+B3+E4:4",
            ),
            notes(PEDAL, 62.0, "E2:2 r:1 E2:2 r:1 E2:2 r:1 E2:4"),
        ],
    },
    Piece {
        name: "J. S. Bach \u{2014} Toccata in D minor, BWV 565 (opening)",
        short: "Toccata",
        beat: 0.55,
        changes: &[Change {
            beat: 0.0,
            label: "Toccata",
            registration: Registration {
                draws: &[
                    draw(
                        GREAT,
                        &[
                            "Montre 16",
                            "Montre 8",
                            "Prestant 4",
                            "Doublette 2",
                            "Fourniture IV",
                            "Trompette 8",
                        ],
                        &[Principal, Mixture, Reed],
                    ),
                    draw(
                        PEDAL,
                        &["Contrebasse 16", "Octavebasse 8", "Bombarde 16"],
                        &[Principal, Reed, Flute],
                    ),
                ],
                couplers: &[],
                tremulant: false,
            },
        }],
        lines: &[
            notes(GREAT, 0.0, TOCCATA),
            // Over the low D, a diminished seventh built note by note, and
            // its resolution.
            notes(PEDAL, 19.75, "D2:10"),
            notes(GREAT, 20.25, "C#3:5"),
            notes(GREAT, 20.5, "E3:4.75"),
            notes(GREAT, 20.75, "G3:4.5"),
            notes(GREAT, 21.0, "A#3:4.25"),
            notes(GREAT, 21.25, "C#4:4"),
            notes(GREAT, 21.5, "E4:3.75"),
            notes(GREAT, 25.25, "D3+F#3+A3+D4+F#4:4.5"),
        ],
    },
    Piece {
        name: "A cadence in C, on the full organ",
        short: "Cadence",
        beat: 0.8,
        changes: &[Change {
            beat: 0.0,
            label: "Cadence",
            registration: Registration {
                draws: &[
                    draw(GREAT, &[], &[Principal, Mixture, Reed, Flute]),
                    draw(PEDAL, &[], &[Principal, Reed, Flute]),
                ],
                couplers: &[],
                tremulant: false,
            },
        }],
        lines: &[
            notes(
                GREAT,
                0.0,
                "C4+E4+G4+C5:2 C4+F4+A4+C5:2 C4+E4+G4+C5:1 D4+F4+G4+B4:1 C4+E4+G4+C5:4",
            ),
            notes(PEDAL, 0.0, "C2:2 F2:2 G2:1 G2:1 C2:4"),
        ],
    },
];

/// The phrase each stop plays alone in the tour of the stops: up and
/// down a chord, on whatever part it is sent to.
pub(crate) static PHRASE: Piece = Piece {
    name: "Every stop, one by one",
    short: "Tour",
    beat: 0.2,
    changes: &[],
    lines: &[notes(GREAT, 0.0, "C4:1 E4 G4 C5 G4 E4 C4:3")],
};

/// The index that means [`PHRASE`].
pub(crate) const PHRASE_INDEX: u8 = u8::MAX;

/// Which piece: one of [`PIECES`], or the tour's [`PHRASE`].
#[cfg(test)]
fn piece(index: u8) -> &'static Piece {
    PIECES.get(usize::from(index)).unwrap_or(&PHRASE)
}

// ------------------------------------------------------------------ scores

/// A key going down or up, `at` seconds into the piece.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Event {
    at: f32,
    part: u8,
    key: u8,
    on: bool,
}

/// A piece compiled: its key events in time, ups before downs at the same
/// moment, and how long it lasts.
pub(crate) struct Score {
    events: Vec<Event>,
    seconds: f32,
}

#[cfg(test)]
impl Score {
    /// How long it lasts, in seconds, its last key lifted.
    fn seconds(&self) -> f32 {
        self.seconds
    }
}

static SCORES: OnceLock<Vec<Score>> = OnceLock::new();

/// Compile every piece; done when the instrument is made, so the audio
/// thread only ever reads the scores.
pub(crate) fn prepare() {
    scores();
}

fn scores() -> &'static [Score] {
    SCORES.get_or_init(|| {
        PIECES
            .iter()
            .chain([&PHRASE])
            .map(|p| compile(p).unwrap_or_else(|e| panic!("{}: {e}", p.name)))
            .collect()
    })
}

/// The score of piece `index` (see [`piece`]).
pub(crate) fn score(index: u8) -> &'static Score {
    let scores = scores();
    scores
        .get(usize::from(index))
        .unwrap_or(&scores[scores.len() - 1])
}

/// The key a note name spells: `C4` is middle C, 60.
fn pitch(name: &str) -> Result<u8, String> {
    let mut chars = name.chars();
    let letter = chars.next().ok_or("an empty note")?;
    let class: i32 = match letter {
        'C' => 0,
        'D' => 2,
        'E' => 4,
        'F' => 5,
        'G' => 7,
        'A' => 9,
        'B' => 11,
        _ => return Err(format!("'{name}' is not a note")),
    };
    let rest = chars.as_str();
    let (accidental, octave) = match rest.chars().next() {
        Some('#') => (1, &rest[1..]),
        Some('b') => (-1, &rest[1..]),
        _ => (0, rest),
    };
    let octave: i32 = octave
        .parse()
        .map_err(|_| format!("'{name}' has no octave"))?;
    let key = 12 * (octave + 1) + class + accidental;
    u8::try_from(key)
        .ok()
        .filter(|k| *k < 128)
        .ok_or(format!("'{name}' is off the keyboard"))
}

/// A pitch class and the rest of the symbol: `F#m7` is 6 and `m7`.
fn root(symbol: &str) -> Result<(i32, &str), String> {
    let mut chars = symbol.chars();
    let class: i32 = match chars.next() {
        Some('C') => 0,
        Some('D') => 2,
        Some('E') => 4,
        Some('F') => 5,
        Some('G') => 7,
        Some('A') => 9,
        Some('B') => 11,
        _ => return Err(format!("'{symbol}' is not a chord")),
    };
    let rest = chars.as_str();
    Ok(match rest.chars().next() {
        Some('#') => ((class + 1) % 12, &rest[1..]),
        Some('b') => ((class + 11) % 12, &rest[1..]),
        _ => (class, rest),
    })
}

/// A chord symbol's pitch classes, the bass's first.
fn chord(symbol: &str) -> Result<(Vec<i32>, i32), String> {
    let (body, slash) = match symbol.split_once('/') {
        Some((b, s)) => (b, Some(s)),
        None => (symbol, None),
    };
    let (r, quality) = root(body)?;
    let intervals: &[i32] = match quality {
        "" => &[0, 4, 7],
        "m" => &[0, 3, 7],
        "7" => &[0, 4, 7, 10],
        "maj7" => &[0, 4, 7, 11],
        "m7" => &[0, 3, 7, 10],
        "dim" => &[0, 3, 6],
        "dim7" => &[0, 3, 6, 9],
        "sus4" => &[0, 5, 7],
        "sus2" => &[0, 2, 7],
        "6" => &[0, 4, 7, 9],
        other => return Err(format!("'{other}' in '{symbol}' is not a chord quality")),
    };
    let bass = match slash {
        Some(s) => root(s)?.0,
        None => r,
    };
    Ok((intervals.iter().map(|i| (r + i) % 12).collect(), bass))
}

/// A duration in beats: `2`, `0.5`, `1/3`.
fn beats(text: &str) -> Result<f32, String> {
    let bad = || format!("'{text}' is not a duration");
    match text.split_once('/') {
        Some((a, b)) => {
            let (a, b): (f32, f32) = (a.parse().map_err(|_| bad())?, b.parse().map_err(|_| bad())?);
            Ok(a / b)
        }
        None => text.parse().map_err(|_| bad()),
    }
}

/// The keys of `classes` between `low` and `high` closest to `last` (or to
/// the middle of the range, for the first chord).
fn voice(classes: &[i32], low: u8, high: u8, last: &[u8]) -> Vec<u8> {
    let options: Vec<Vec<u8>> = classes
        .iter()
        .map(|c| (low..=high).filter(|k| i32::from(*k) % 12 == *c).collect())
        .collect();
    let middle = (f32::from(low) + f32::from(high)) / 2.0;
    let mut best: Option<(f32, Vec<u8>)> = None;
    let mut pick = vec![0usize; options.len()];
    loop {
        let mut keys: Vec<u8> = pick
            .iter()
            .zip(&options)
            .filter_map(|(i, o)| o.get(*i).copied())
            .collect();
        keys.sort_unstable();
        keys.dedup();
        let span = keys.last().zip(keys.first()).map_or(0, |(h, l)| h - l);
        if keys.len() == options.len() && span <= 12 {
            let cost = if last.len() == keys.len() {
                keys.iter()
                    .zip(last)
                    .map(|(a, b)| (f32::from(*a) - f32::from(*b)).abs())
                    .sum()
            } else {
                keys.iter()
                    .map(|k| (f32::from(*k) - middle).abs())
                    .sum::<f32>()
            };
            if best.as_ref().is_none_or(|(c, _)| cost < *c) {
                best = Some((cost, keys));
            }
        }
        // The next combination, as an odometer.
        let mut i = 0;
        loop {
            if i == pick.len() {
                return best.map(|b| b.1).unwrap_or_default();
            }
            pick[i] += 1;
            if pick[i] < options[i].len() {
                break;
            }
            pick[i] = 0;
            i += 1;
        }
    }
}

/// The notes of a line: each key, when it goes down, and how long it is
/// held, in beats from the piece's start.
fn line_notes(line: &Line) -> Result<Vec<(u8, f32, f32)>, String> {
    let mut out = Vec::new();
    let mut t = line.start;
    let mut length = 1.0;
    let mut last: Vec<u8> = Vec::new();
    for token in line.text.split_whitespace().filter(|t| *t != "|") {
        let (what, len) = match token.split_once(':') {
            Some((w, l)) => (w, Some(l)),
            None => (token, None),
        };
        if let Some(len) = len {
            length = beats(len)?;
        }
        if what != "r" {
            let keys = match line.kind {
                Kind::Notes => what.split('+').map(pitch).collect::<Result<Vec<_>, _>>()?,
                Kind::Chords { low, high } => {
                    let keys = voice(&chord(what)?.0, low, high, &last);
                    if keys.is_empty() {
                        return Err(format!("'{what}' has no voicing in {low}..{high}"));
                    }
                    keys
                }
                Kind::Bass { low, high } => {
                    let class = chord(what)?.1;
                    let near = last.first().map_or(f32::from(low) + 5.0, |k| f32::from(*k));
                    let key = (low..=high)
                        .filter(|k| i32::from(*k) % 12 == class)
                        .min_by(|a, b| {
                            (f32::from(*a) - near)
                                .abs()
                                .total_cmp(&(f32::from(*b) - near).abs())
                        })
                        .ok_or(format!("'{what}' has no bass in {low}..{high}"))?;
                    vec![key]
                }
            };
            for key in &keys {
                out.push((*key, t, length));
            }
            last = keys;
        }
        t += length;
    }
    Ok(out)
}

/// A piece's lines as one score.
fn compile(piece: &Piece) -> Result<Score, String> {
    let mut raw = Vec::new();
    for line in piece.lines {
        for (key, start, len) in line_notes(line)? {
            let on = start * piece.beat;
            let held = len * piece.beat;
            // Organists lift between notes: a little of each note, never
            // more than a short breath.
            let off = on + held - (held * 0.06).min(0.08);
            raw.push(Event {
                at: on,
                part: line.part,
                key,
                on: true,
            });
            raw.push(Event {
                at: off,
                part: line.part,
                key,
                on: false,
            });
        }
    }
    raw.sort_by(|a, b| a.at.total_cmp(&b.at).then(a.on.cmp(&b.on)));
    // Two lines on one key of one part: it goes down with the first and up
    // with the last.
    let mut down = [[0u8; 128]; 3];
    let mut events = Vec::with_capacity(raw.len());
    let mut seconds: f32 = 0.0;
    for e in raw {
        let count = &mut down[usize::from(e.part.min(2))][usize::from(e.key)];
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
    Ok(Score { events, seconds })
}

// ------------------------------------------------------------------ player

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

    /// The piece playing, if any (see [`piece`]).
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
            let bit = 1u128 << e.key;
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
    fn notes_and_chords_are_read() {
        assert_eq!(pitch("C4"), Ok(60));
        assert_eq!(pitch("F#2"), Ok(42));
        assert_eq!(pitch("Bb3"), Ok(58));
        assert_eq!(chord("D/F#").unwrap(), (vec![2, 6, 9], 6));
        assert_eq!(chord("G7").unwrap().0, vec![7, 11, 2, 5]);
        assert_eq!(beats("1/3").unwrap(), 1.0 / 3.0);
        // Close to the last chord: C to F keeps C and moves the others up.
        assert_eq!(voice(&[5, 9, 0], 55, 69, &[60, 64, 67]), vec![60, 65, 69]);
    }

    #[test]
    fn every_piece_compiles_and_lasts() {
        for (n, p) in PIECES.iter().enumerate() {
            let s = compile(p).unwrap_or_else(|e| panic!("{}: {e}", p.name));
            assert!(!s.events.is_empty(), "{}", p.name);
            // Whole pieces, not snippets, but the cadence and the toccata's
            // opening.
            if !matches!(p.short, "Cadence" | "Toccata") {
                assert!(s.seconds > 30.0, "{}: {} s", p.name, s.seconds);
            }
            assert!(
                p.changes.first().is_some_and(|c| c.beat == 0.0),
                "{}: a registration from the start",
                p.name
            );
            assert!(score(n as u8).seconds() == s.seconds);
        }
    }

    #[test]
    fn every_key_that_goes_down_comes_up() {
        for index in (0..PIECES.len() as u8).chain([PHRASE_INDEX]) {
            let mut player = Player::new();
            let mut held = [0u128; 16];
            let mut downs = 0;
            player.start(index, [0, 1, 2], 0, |_, _, _, _| {});
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
            let name = piece(index).name;
            assert!(downs > 4, "{name}");
            assert_eq!(held, [0; 16], "{name} leaves keys down");
        }
    }

    #[test]
    fn stopping_lifts_the_keys_held() {
        let mut player = Player::new();
        let mut held = 0u32;
        player.start(0, [0, 1, 2], 0, |_, _, _, _| {});
        // Into the canon, its ground, chords and tune all sounding.
        for _ in 0..(48_000 * 20 / 256) {
            player.render(256, 48_000.0, |_, _, _, on| {
                if on {
                    held += 1;
                } else {
                    held -= 1;
                }
            });
        }
        assert!(held > 3, "{held}");
        player.stop(|_, _, _, on| {
            assert!(!on);
            held -= 1;
        });
        assert_eq!(held, 0);
        assert!(player.playing().is_none());
    }
}
