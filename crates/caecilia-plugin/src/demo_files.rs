// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Demonstrations from MIDI files: every `.mid` in the Demos folder beside
//! the session (`%APPDATA%\Caecilia\Demos`) becomes a piece the instrument
//! plays on itself.
//!
//! Each track goes to a part by its name, or failing that by its range: the
//! lowest line to the pedal, any other to the great (a trio's two upper
//! lines to a manual each). A line that falls off the keyboard moves by
//! octaves. Pieces this file knows have their proper names and their own
//! registrations; any other is named after its file and given one in turn.

use std::path::{Path, PathBuf};

use caecilia_model::TonalFamily::{self, Flute, Mixture, Principal, Reed, String as Strings};
use manguebyte_midi::MidiMessage;
use manguebyte_midi::file::{Division, MetaEvent, MidiFile, TrackEventKind};

use crate::demo::{Change, Demo, Draw, Event, GREAT, PEDAL, Registration, SWELL, score_of};

/// The folder demonstrations are read from: `CAECILIA_DEMOS` if set (empty
/// for none), else beside the session. Nothing under `cargo`, whose tests
/// must not hear what happens to be on this machine.
fn folder() -> Option<PathBuf> {
    if let Some(named) = std::env::var_os("CAECILIA_DEMOS") {
        return (!named.is_empty()).then(|| PathBuf::from(named));
    }
    if std::env::var_os("CARGO_MANIFEST_DIR").is_some() {
        return None;
    }
    let base = std::env::var_os("APPDATA")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(if cfg!(windows) {
        base.join("Caecilia").join("Demos")
    } else {
        base.join("caecilia").join("demos")
    })
}

/// How many pieces the console lists, at most; a piece's number travels in
/// a byte, and the top one means the tour.
const MOST: usize = 200;

/// The pieces in the Demos folder, those this file knows first and in its
/// order; none if there is no folder or nothing in it plays.
pub(crate) fn load() -> Vec<Demo> {
    folder().map_or_else(Vec::new, |f| load_from(&f))
}

fn load_from(folder: &Path) -> Vec<Demo> {
    let Ok(entries) = std::fs::read_dir(folder) else {
        return Vec::new();
    };
    let mut files: Vec<PathBuf> = entries
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| {
            p.extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| e.eq_ignore_ascii_case("mid") || e.eq_ignore_ascii_case("midi"))
        })
        .collect();
    let stem = |p: &Path| {
        p.file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or_default()
            .to_ascii_lowercase()
    };
    let rank = |p: &Path| {
        let s = stem(p);
        KNOWN
            .iter()
            .position(|k| k.file == s)
            .unwrap_or(KNOWN.len())
    };
    files.sort_by_key(|p| (rank(p), stem(p)));
    let mut unknown = 0;
    let mut demos = Vec::new();
    for path in files.into_iter().take(MOST) {
        let Ok(bytes) = std::fs::read(&path) else {
            continue;
        };
        let Ok(file) = MidiFile::from_bytes(&bytes) else {
            continue;
        };
        let s = stem(&path);
        let events = events_of(&file, s.starts_with("trio"));
        if events.is_empty() {
            continue;
        }
        let (name, short, changes) = match KNOWN.iter().find(|k| k.file == s) {
            Some(k) => (k.name.to_string(), k.short.to_string(), k.changes),
            None => {
                let name = path
                    .file_stem()
                    .and_then(|s| s.to_str())
                    .unwrap_or_default()
                    .to_string();
                unknown += 1;
                (name.clone(), name, IN_TURN[unknown % IN_TURN.len()])
            }
        };
        demos.push(Demo {
            name,
            short,
            beat: 1.0,
            changes,
            score: score_of(events),
        });
    }
    demos
}

/// A line of a file: one track on one channel.
struct FileLine {
    name: String,
    /// Each note: its key, and when it goes down and up, in ticks.
    notes: Vec<(u8, u64, u64)>,
}

impl FileLine {
    fn mean(&self) -> f32 {
        self.notes.iter().map(|n| f32::from(n.0)).sum::<f32>() / self.notes.len().max(1) as f32
    }
}

/// The percussion channel, which an organ does not play.
const DRUMS: u8 = 9;

fn lines_of(file: &MidiFile) -> Vec<FileLine> {
    let mut lines = Vec::new();
    for track in &file.tracks {
        let name = track.name().unwrap_or_default().to_ascii_lowercase();
        let end = track.end_tick();
        let mut channels: [Vec<(u8, u64, u64)>; 16] = std::array::from_fn(|_| Vec::new());
        // The notes down, by channel and key, oldest first.
        let mut down: Vec<(u8, u8, u64)> = Vec::new();
        for event in track.events() {
            let TrackEventKind::Midi(message) = &event.kind else {
                continue;
            };
            match *message {
                MidiMessage::NoteOn { channel, note, .. } if channel != DRUMS => {
                    down.push((channel, note, event.tick));
                }
                MidiMessage::NoteOff { channel, note, .. } => {
                    if let Some(i) = down.iter().position(|d| d.0 == channel && d.1 == note) {
                        let (_, _, on) = down.remove(i);
                        channels[usize::from(channel & 0x0F)].push((note, on, event.tick));
                    }
                }
                _ => {}
            }
        }
        for (channel, note, on) in down {
            channels[usize::from(channel & 0x0F)].push((note, on, end));
        }
        for notes in channels {
            if !notes.is_empty() {
                lines.push(FileLine {
                    name: name.clone(),
                    notes,
                });
            }
        }
    }
    lines
}

/// The part each line goes to; for a trio, its two upper lines on a manual
/// each.
fn parts_of(lines: &[FileLine], trio: bool) -> Vec<u8> {
    let named = |name: &str| {
        if name.contains("pedal") || name.contains("p\u{e9}dale") {
            Some(PEDAL)
        } else if name.contains("swell")
            || name.contains("recit")
            || name.contains("r\u{e9}cit")
            || name.contains("oberwerk")
        {
            Some(SWELL)
        } else if name.contains("great")
            || name.contains("grand")
            || name.contains("hauptwerk")
            || name.contains("hand")
        {
            Some(GREAT)
        } else {
            None
        }
    };
    let mut parts: Vec<Option<u8>> = lines.iter().map(|l| named(&l.name)).collect();
    let mut left: Vec<usize> = (0..lines.len()).filter(|i| parts[*i].is_none()).collect();
    left.sort_by(|a, b| lines[*a].mean().total_cmp(&lines[*b].mean()));
    // The lowest line, if low enough and no track is the pedal's by name.
    if !parts.contains(&Some(PEDAL)) {
        if let Some(&lowest) = left.first() {
            if lines[lowest].mean() < 58.0 && lines.len() > 1 {
                parts[lowest] = Some(PEDAL);
                left.remove(0);
            }
        }
    }
    if let ([lower, upper], true) = (left.as_slice(), trio) {
        parts[*lower] = Some(SWELL);
        parts[*upper] = Some(GREAT);
    }
    parts.into_iter().map(|p| p.unwrap_or(GREAT)).collect()
}

/// The ticks' seconds, walking the file's tempo map.
struct Clock {
    /// Each change of tempo: its tick, the seconds up to it, and the
    /// seconds a tick lasts from there.
    spans: Vec<(u64, f64, f64)>,
}

impl Clock {
    fn new(file: &MidiFile) -> Clock {
        let per_quarter = match file.division {
            Division::TicksPerQuarter(t) => f64::from(t).max(1.0),
            Division::Smpte {
                frames_per_second,
                ticks_per_frame,
            } => {
                // Ticks do not depend on the tempo: one fixed rate.
                let rate = f64::from(frames_per_second) * f64::from(ticks_per_frame);
                return Clock {
                    spans: vec![(0, 0.0, 1.0 / rate.max(1.0))],
                };
            }
        };
        let mut tempos: Vec<(u64, u32)> = file
            .tracks
            .iter()
            .flat_map(|t| t.events())
            .filter_map(|e| match e.kind {
                TrackEventKind::Meta(MetaEvent::Tempo(us)) => Some((e.tick, us)),
                _ => None,
            })
            .collect();
        tempos.sort_by_key(|t| t.0);
        let mut spans = vec![(0, 0.0, 500_000.0 / per_quarter / 1e6)];
        for (tick, us) in tempos {
            let (from, seconds, per_tick) = spans[spans.len() - 1];
            let at = seconds + (tick - from) as f64 * per_tick;
            spans.push((tick, at, f64::from(us) / per_quarter / 1e6));
        }
        Clock { spans }
    }

    fn seconds(&self, tick: u64) -> f32 {
        let i = self.spans.partition_point(|s| s.0 <= tick).max(1) - 1;
        let (from, seconds, per_tick) = self.spans[i];
        (seconds + (tick - from) as f64 * per_tick) as f32
    }
}

/// The keyboard a part has on every organ worth the name.
fn compass(part: u8) -> (i32, i32) {
    if part == PEDAL { (36, 67) } else { (36, 96) }
}

/// The file's notes as a score's events: each line on its part, by octaves
/// onto its keyboard, from its first note on.
fn events_of(file: &MidiFile, trio: bool) -> Vec<Event> {
    let lines = lines_of(file);
    let parts = parts_of(&lines, trio);
    let clock = Clock::new(file);
    let first = lines
        .iter()
        .flat_map(|l| l.notes.iter().map(|n| n.1))
        .min()
        .unwrap_or(0);
    let start = clock.seconds(first);
    let mut events = Vec::new();
    for (line, part) in lines.iter().zip(parts) {
        let (low, high) = compass(part);
        // The whole line, as a sixteen-foot pedal written where it sounds.
        let lowest = line
            .notes
            .iter()
            .map(|n| i32::from(n.0))
            .min()
            .unwrap_or(low);
        let up = if lowest < low {
            (low - lowest + 11) / 12 * 12
        } else {
            0
        };
        for &(key, on, off) in &line.notes {
            let mut key = i32::from(key) + up;
            while key > high {
                key -= 12;
            }
            while key < low {
                key += 12;
            }
            let key = key as u8;
            let at = clock.seconds(on) - start;
            let until = clock.seconds(off) - start;
            events.push(Event {
                at,
                part,
                key,
                on: true,
            });
            events.push(Event {
                at: until.max(at + 0.01),
                part,
                key,
                on: false,
            });
        }
    }
    events
}

// ----------------------------------------------------------- registrations

const fn draw(part: u8, names: &'static [&'static str], families: &'static [TonalFamily]) -> Draw {
    Draw {
        part,
        names,
        families,
    }
}

/// A registration for the whole of a piece.
const fn whole(
    label: &'static str,
    draws: &'static [Draw],
    couplers: &'static [&'static str],
) -> Change {
    Change {
        beat: 0.0,
        label,
        registration: Registration {
            draws,
            couplers,
            tremulant: false,
        },
    }
}

const PEDAL_SOFT: Draw = draw(PEDAL, &["Soubasse 16", "Bourdon 8"], &[Flute]);
const PEDAL_FIRM: Draw = draw(
    PEDAL,
    &["Contrebasse 16", "Soubasse 16", "Octavebasse 8"],
    &[Principal],
);
const PEDAL_FULL: Draw = draw(
    PEDAL,
    &[
        "Contrebasse 16",
        "Soubasse 16",
        "Octavebasse 8",
        "Bombarde 16",
        "Trompette 8",
    ],
    &[Principal, Reed],
);
const GREAT_PLENUM: Draw = draw(
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
);
const GREAT_FULL: Draw = draw(
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
);
const GREAT_PRINCIPALS: Draw = draw(GREAT, &["Montre 8", "Prestant 4"], &[Principal]);
const GREAT_FOUNDATIONS: Draw = draw(GREAT, &["Montre 8", "Bourdon 8"], &[Principal, Flute]);
const GREAT_FLUTES: Draw = draw(
    GREAT,
    &["Bourdon 8", "Fl\u{fb}te 4", "Doublette 2"],
    &[Flute],
);
const SWELL_PLENUM: Draw = draw(
    SWELL,
    &["Diapason 8", "Prestant 4", "Plein Jeu III", "Trompette 8"],
    &[Principal, Mixture, Reed],
);
const COUPLED: &[&str] = &["R\u{e9}cit/Grand-Orgue"];

const PLENUM: &[Change] = &[whole("Plenum", &[GREAT_PLENUM, PEDAL_FIRM], &[])];
const FULL: &[Change] = &[whole(
    "Full organ",
    &[GREAT_FULL, SWELL_PLENUM, PEDAL_FULL],
    COUPLED,
)];
const PRINCIPALS: &[Change] = &[whole("Principals", &[GREAT_PRINCIPALS, PEDAL_FIRM], &[])];
const FOUNDATIONS: &[Change] = &[whole("Foundations", &[GREAT_FOUNDATIONS, PEDAL_SOFT], &[])];
const FLUTES: &[Change] = &[whole("Flutes", &[GREAT_FLUTES, PEDAL_SOFT], &[])];
const TOCCATA: &[Change] = &[whole(
    "Organo pleno",
    &[GREAT_FULL, SWELL_PLENUM, PEDAL_FULL],
    &[],
)];
const TRIO_ALLEGRO: &[Change] = &[whole(
    "Trio: principal and cornet",
    &[
        draw(GREAT, &["Montre 8", "Fl\u{fb}te 4"], &[Principal]),
        draw(
            SWELL,
            &["Bourdon 8", "Fl\u{fb}te octaviante 4", "Nasard 2 2/3"],
            &[Flute],
        ),
        PEDAL_SOFT,
    ],
    &[],
)];
const TRIO_ADAGIO: &[Change] = &[whole(
    "Trio: oboe and strings",
    &[
        draw(GREAT, &["Bourdon 8", "Fl\u{fb}te 4"], &[Flute]),
        draw(
            SWELL,
            &["Gambe 8", "Voix C\u{e9}leste 8", "Basson-Hautbois 8"],
            &[Strings, Reed],
        ),
        draw(PEDAL, &["Soubasse 16"], &[Flute]),
    ],
    &[],
)];
const TRIO_VIVACE: &[Change] = &[whole(
    "Trio: bright flutes",
    &[
        draw(
            GREAT,
            &["Bourdon 8", "Prestant 4", "Doublette 2"],
            &[Principal],
        ),
        draw(
            SWELL,
            &["Bourdon 8", "Octavin 2", "Tierce 1 3/5", "Nasard 2 2/3"],
            &[Flute],
        ),
        PEDAL_SOFT,
    ],
    &[],
)];

/// What a file with no entry below draws, in turn.
const IN_TURN: &[&[Change]] = &[FOUNDATIONS, PRINCIPALS, FLUTES];

/// A piece this file knows.
struct Known {
    /// Its file's name, in lower case, without the extension.
    file: &'static str,
    name: &'static str,
    short: &'static str,
    changes: &'static [Change],
}

const fn known(
    file: &'static str,
    name: &'static str,
    short: &'static str,
    changes: &'static [Change],
) -> Known {
    Known {
        file,
        name,
        short,
        changes,
    }
}

const KNOWN: &[Known] = &[
    known(
        "toccata1",
        "Toccata and Fugue in D minor, BWV 565",
        "Toccata and Fugue",
        TOCCATA,
    ),
    known(
        "prefug1",
        "Prelude and Fugue in C major, BWV 553",
        "Prelude, Fugue in C",
        PLENUM,
    ),
    known(
        "prefug2",
        "Prelude and Fugue in D minor, BWV 554",
        "Prelude, Fugue in D minor",
        FOUNDATIONS,
    ),
    known(
        "prefug3",
        "Prelude and Fugue in E minor, BWV 555",
        "Prelude, Fugue in E minor",
        FLUTES,
    ),
    known(
        "prefug4",
        "Prelude and Fugue in F major, BWV 556",
        "Prelude, Fugue in F",
        FULL,
    ),
    known(
        "prefug5",
        "Prelude and Fugue in G major, BWV 557",
        "Prelude, Fugue in G",
        PLENUM,
    ),
    known(
        "prefug6",
        "Prelude and Fugue in G minor, BWV 558",
        "Prelude, Fugue in G minor",
        PRINCIPALS,
    ),
    known(
        "prefug7",
        "Prelude and Fugue in A minor, BWV 559",
        "Prelude, Fugue in A minor",
        FOUNDATIONS,
    ),
    known(
        "prefug8",
        "Prelude and Fugue in B flat major, BWV 560",
        "Prelude, Fugue in B flat",
        FULL,
    ),
    known(
        "trio3a",
        "Trio Sonata no. 3, BWV 527: Andante",
        "Trio Sonata: Andante",
        TRIO_ALLEGRO,
    ),
    known(
        "trio3b",
        "Trio Sonata no. 3, BWV 527: Adagio",
        "Trio Sonata: Adagio",
        TRIO_ADAGIO,
    ),
    known(
        "trio3c",
        "Trio Sonata no. 3, BWV 527: Vivace",
        "Trio Sonata: Vivace",
        TRIO_VIVACE,
    ),
    known(
        "schub5",
        "Ach bleib bei uns, Herr Jesu Christ, BWV 649",
        "Ach bleib bei uns",
        FOUNDATIONS,
    ),
    known(
        "schub6",
        "Kommst du nun, Jesu, BWV 650",
        "Kommst du nun, Jesu",
        PRINCIPALS,
    ),
    known("jesu2", "Jesu, Joy of Man's Desiring", "Jesu, Joy", FLUTES),
    known(
        "jesu1",
        "Jesu, Joy of Man's Desiring (arr. John Renfro Davis)",
        "Jesu, Joy (arr.)",
        FOUNDATIONS,
    ),
    known(
        "01ausmeinesherz",
        "Chorale: Aus meines Herzens Grunde",
        "Aus meines Herzens",
        PRINCIPALS,
    ),
    known(
        "02ichdankdir",
        "Chorale: Ich dank dir, lieber Herre",
        "Ich dank dir",
        FOUNDATIONS,
    ),
    known(
        "03achgott",
        "Chorale: Ach Gott, vom Himmel sieh darein",
        "Ach Gott, vom Himmel",
        FLUTES,
    ),
    known(
        "04esistdasheiluns",
        "Chorale: Es ist das Heil uns kommen her",
        "Es ist das Heil",
        PRINCIPALS,
    ),
    known(
        "05anwasserflussen",
        "Chorale: An Wasserfl\u{fc}ssen Babylon",
        "An Wasserfl\u{fc}ssen",
        FOUNDATIONS,
    ),
    known(
        "06christus",
        "Chorale: Christus, der ist mein Leben",
        "Christus, der ist",
        FLUTES,
    ),
    known(
        "07nunlob",
        "Chorale: Nun lob, mein Seel, den Herren",
        "Nun lob, mein Seel",
        PLENUM,
    ),
    known(
        "08freueteuch",
        "Chorale: Freuet euch, ihr Christen alle",
        "Freuet euch",
        PRINCIPALS,
    ),
    known(
        "09ermuntredich",
        "Chorale: Ermuntre dich, mein schwacher Geist",
        "Ermuntre dich",
        FOUNDATIONS,
    ),
    known(
        "10austiefernot",
        "Chorale: Aus tiefer Not schrei ich zu dir",
        "Aus tiefer Not",
        FOUNDATIONS,
    ),
    known(
        "11jesu",
        "Chorale: Jesu, nun sei gepreiset",
        "Jesu, nun sei gepreiset",
        PLENUM,
    ),
    known(
        "12puernatusinbet",
        "Chorale: Puer natus in Bethlehem",
        "Puer natus",
        FLUTES,
    ),
    known(
        "13alleinzudir",
        "Chorale: Allein zu dir, Herr Jesu Christ",
        "Allein zu dir",
        PRINCIPALS,
    ),
    known(
        "14oherregott",
        "Chorale: O Herre Gott, dein g\u{f6}ttlich Wort",
        "O Herre Gott",
        FOUNDATIONS,
    ),
    known(
        "15christlagintode",
        "Chorale: Christ lag in Todesbanden",
        "Christ lag",
        PRINCIPALS,
    ),
];

#[cfg(test)]
mod tests {
    use super::*;
    use manguebyte_midi::file::Track;

    fn line(name: &str, channel: u8, keys: &[u8]) -> Track {
        let mut track = Track::new();
        track.push_meta(0, MetaEvent::TrackName(name.into()));
        for (i, key) in keys.iter().enumerate() {
            let at = i as u64 * 480;
            track.push_midi(
                at,
                MidiMessage::NoteOn {
                    channel,
                    note: *key,
                    velocity: 90,
                },
            );
            track.push_midi(
                at + 240,
                MidiMessage::NoteOff {
                    channel,
                    note: *key,
                    velocity: 0,
                },
            );
        }
        track
    }

    fn file(tracks: Vec<Track>) -> MidiFile {
        MidiFile::multi_track(480, tracks)
    }

    #[test]
    fn named_tracks_go_to_their_parts_and_a_low_pedal_moves_up() {
        let f = file(vec![
            line("Right Hand", 1, &[72, 74]),
            line("Left Hand", 1, &[60, 62]),
            line("Pedal", 3, &[26, 38]),
        ]);
        let events = events_of(&f, false);
        let keys = |part: u8| -> Vec<u8> {
            events
                .iter()
                .filter(|e| e.part == part && e.on)
                .map(|e| e.key)
                .collect()
        };
        assert_eq!(keys(GREAT), [72, 74, 60, 62]);
        assert_eq!(keys(PEDAL), [38, 50], "an octave up, as a whole");
        assert!(keys(SWELL).is_empty());
    }

    #[test]
    fn a_trio_takes_two_manuals_and_the_pedal_and_three_hands_one() {
        let f = file(vec![
            line("", 0, &[76, 77]),
            line("", 1, &[64, 65]),
            line("", 2, &[40, 41]),
        ]);
        let lines = lines_of(&f);
        assert_eq!(parts_of(&lines, true), [GREAT, SWELL, PEDAL]);
        assert_eq!(parts_of(&lines, false), [GREAT, GREAT, PEDAL]);
    }

    #[test]
    fn four_voices_put_the_bass_on_the_pedal_and_the_rest_on_the_great() {
        let f = file(vec![
            line("", 0, &[72]),
            line("", 1, &[67]),
            line("", 2, &[60]),
            line("", 3, &[48]),
        ]);
        assert_eq!(parts_of(&lines_of(&f), false), [GREAT, GREAT, GREAT, PEDAL]);
    }

    #[test]
    fn time_follows_the_tempo_map_from_the_first_note() {
        let mut tempo = Track::new();
        tempo.push_meta(0, MetaEvent::Tempo(1_000_000));
        tempo.push_meta(960, MetaEvent::Tempo(500_000));
        let mut notes = Track::new();
        notes.push_midi(
            480,
            MidiMessage::NoteOn {
                channel: 0,
                note: 60,
                velocity: 90,
            },
        );
        notes.push_midi(
            1440,
            MidiMessage::NoteOff {
                channel: 0,
                note: 60,
                velocity: 0,
            },
        );
        let events = events_of(&file(vec![tempo, notes]), false);
        assert_eq!(events[0].at, 0.0, "the silence before it is gone");
        // A quarter at 60 bpm, then one at 120.
        assert!((events[1].at - 1.5).abs() < 1e-4, "{}", events[1].at);
    }

    /// The real files, where `CAECILIA_DEMOS_CHECK` names a folder of them:
    /// every one reads, plays and fits the keyboards.
    #[test]
    fn the_folder_named_reads() {
        let Some(folder) = std::env::var_os("CAECILIA_DEMOS_CHECK") else {
            return;
        };
        let demos = load_from(Path::new(&folder));
        assert!(!demos.is_empty());
        for d in &demos {
            println!(
                "{:>7.1} s  {:>5} events  {}",
                d.score.seconds(),
                d.score.event_count(),
                d.name
            );
            assert!(d.score.seconds() > 5.0, "{}", d.name);
        }
    }
}
