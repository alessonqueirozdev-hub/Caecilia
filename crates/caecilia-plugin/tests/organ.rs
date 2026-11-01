// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! The plugin plays the organ a file describes.

use caecilia_model::read_organ;
use caecilia_plugin::{Caecilia, set_organ_for_new_instances, stop_param_id};
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_params::ParamSet;
use manguebyte_plugin::Harness;

/// A two-manual English chamber organ, written by hand.
const CHAMBER: &str = r#"{
  "name": "Chamber organ",
  "windchests": [ { "name": "Main", "pressurePa": 650, "tremulant": true } ],
  "ranks": [
    { "name": "Stopped Diapason 8", "family": "Flute", "footage": 8, "windchest": "Main" },
    { "name": "Principal 4", "family": "Principal", "footage": 4, "windchest": "Main" },
    { "name": "Fifteenth 2", "family": "Principal", "footage": 2, "windchest": "Main" },
    { "name": "Hautboy 8", "family": "Reed", "footage": 8, "windchest": "Main",
      "recipe": { "brightness": 0.7, "formantHz": 1600, "fundamentalDb": -6 } }
  ],
  "divisions": [
    { "name": "Great", "midiChannel": 0, "manual": 1 },
    { "name": "Swell", "enclosed": true, "lowNote": 48, "midiChannel": 1, "manual": 2 }
  ],
  "stops": [
    { "name": "Stopped Diapason 8", "family": "Flute", "division": "Great", "rank": "Stopped Diapason 8" },
    { "name": "Principal 4", "footage": 4, "pitchClass": "Octave", "role": "Chorus", "division": "Great", "rank": "Principal 4" },
    { "name": "Fifteenth 2", "footage": 2, "pitchClass": "Super", "role": "Chorus", "division": "Great", "rank": "Fifteenth 2" },
    { "name": "Hautboy 8", "family": "Reed", "role": "Solo", "division": "Swell", "rank": "Hautboy 8" }
  ],
  "couplers": [ { "name": "Swell to Great", "from": "Swell", "to": "Great" } ]
}"#;

#[test]
fn an_organ_file_is_what_the_plugin_plays() {
    let doc = read_organ(CHAMBER, None).expect("the chamber organ loads");
    set_organ_for_new_instances(doc.organ);

    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    assert_eq!(h.plugin_mut().organ().name, "Chamber organ");

    // The host's parameters carry this organ's names.
    let name = |id: &str| h.params().find(id).unwrap().name().to_string();
    assert_eq!(name(&stop_param_id(3)), "Swell Hautboy 8");
    assert!(name(&stop_param_id(4)).contains("unused"));

    // It opens on its Great's principal chorus and flute, as any organ does.
    let opening: Vec<usize> = (0..64)
        .filter(|n| h.params().stop_mask() & (1 << n) != 0)
        .collect();
    assert_eq!(opening, [0, 1, 2]);

    // Draw the Hautboy and the coupler; a Great key sounds the Great's three
    // ranks and the Swell's reed through the coupler.
    h.automate(0, &stop_param_id(3), 1.0);
    h.automate(0, "coupler-00", 1.0);
    h.schedule(
        0,
        EventKind::NoteOn {
            note: NoteRef::new(0, 60),
            velocity: 0.8,
        },
    );
    let out = h.render(9600);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 4);
    assert!(out.peak() > 0.01);

    // Channel 2 is the Swell, whose compass starts at 48: key 40 is not on it.
    h.schedule(
        9600,
        EventKind::NoteOn {
            note: NoteRef::new(1, 40),
            velocity: 0.8,
        },
    );
    h.render(256);
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), 4);
}
