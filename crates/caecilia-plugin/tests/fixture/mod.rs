// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! A Demos folder for tests: small MIDI files written here, under the file
//! names the instrument knows, played by whichever test asks first.

use std::path::PathBuf;
use std::sync::Once;

use manguebyte_midi::MidiMessage;
use manguebyte_midi::file::{MidiFile, Track};

/// A line of quarter notes at 120 bpm.
fn line(channel: u8, keys: &[u8]) -> Track {
    let mut track = Track::new();
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
            at + 450,
            MidiMessage::NoteOff {
                channel,
                note: *key,
                velocity: 0,
            },
        );
    }
    track
}

/// Make the folder and have new instances read it.
pub fn demos() {
    static MADE: Once = Once::new();
    MADE.call_once(|| {
        let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR"))
            .join(concat!("demos-", env!("CARGO_CRATE_NAME")));
        std::fs::create_dir_all(&dir).unwrap();
        let tune = [69, 67, 69, 65, 64, 62, 61, 62];
        // Enough files for the list to come in pages.
        for name in [
            "toccata1",
            "prefug1",
            "prefug2",
            "prefug3",
            "prefug4",
            "prefug5",
            "prefug6",
            "prefug7",
            "prefug8",
            "trio3a",
            "trio3b",
            "trio3c",
            "schub5",
            "schub6",
            "jesu1",
            "jesu2",
            "02ichdankdir",
            "03achgott",
            "06christus",
            "07nunlob",
        ] {
            let file = MidiFile::multi_track(
                480,
                vec![line(0, &tune), line(1, &[62, 60]), line(2, &[38, 45])],
            );
            std::fs::write(dir.join(format!("{name}.mid")), file.to_bytes()).unwrap();
        }
        caecilia_plugin::set_demo_folder_for_new_instances(&dir);
    });
}
