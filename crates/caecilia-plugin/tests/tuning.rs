//! Temperaments, pitch and Scala, through a host and the controller.

use caecilia_plugin::{Caecilia, TEMPERAMENT_NAMES, stop_param_id};
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_dsp::tuning::{Scale, TuningSettings};
use manguebyte_plugin::Harness;

fn harness() -> Harness<Caecilia> {
    Harness::<Caecilia>::new(48000.0, 256)
}

fn cents(a: f64, b: f64) -> f64 {
    1200.0 * (a / b).log2()
}

fn key_hz(h: &mut Harness<Caecilia>, key: u8) -> f64 {
    h.plugin_mut().engine().unwrap().key_frequency(key)
}

#[test]
fn the_host_chooses_temperament_key_and_pitch() {
    let mut h = harness();
    h.render(256);
    assert!((key_hz(&mut h, 69) - 440.0).abs() < 1e-9);
    assert!(
        cents(key_hz(&mut h, 64), 440.0 * (-5.0f64 / 12.0).exp2()).abs() < 1e-9,
        "equal by default"
    );

    let meantone = TEMPERAMENT_NAMES
        .iter()
        .position(|n| n.contains("meantone"))
        .unwrap();
    h.automate(256, "temperament", meantone as f64);
    h.automate(256, "pitch", 415.0);
    h.render(256);
    assert!(
        (key_hz(&mut h, 69) - 415.0).abs() < 1e-9,
        "A4 is the pitch asked for"
    );
    // Quarter-comma meantone on C: a pure major third C-E, 386.3 cents.
    let third = cents(key_hz(&mut h, 64), key_hz(&mut h, 60));
    assert!((third - 386.313_7).abs() < 1e-3, "C-E is {third} cents");

    // On G instead: now G-B is the pure third.
    h.automate(512, "temperament-key", 7.0);
    h.render(256);
    let third = cents(key_hz(&mut h, 71), key_hz(&mut h, 67));
    assert!((third - 386.313_7).abs() < 1e-3, "G-B is {third} cents");
}

#[test]
fn the_controller_plays_any_tuning_and_a_parameter_takes_it_back() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    let settings = TuningSettings {
        scale: Scale::equal(19),
        ..TuningSettings::default()
    };
    assert!(ctl.lock().unwrap().use_tuning(&settings).unwrap());
    h.render(256);
    assert!(
        (key_hz(&mut h, 69 + 19) - 880.0).abs() < 1e-9,
        "19 keys to the octave"
    );

    h.automate(256, "temperament", 0.0);
    h.automate(256, "pitch", 442.0);
    h.render(256);
    assert!(
        (key_hz(&mut h, 81) - 884.0).abs() < 1e-9,
        "back to equal temperament at 442"
    );
}

#[test]
fn a_key_a_scala_mapping_leaves_out_sounds_nothing() {
    let mut h = harness();
    // Only the Grand-Orgue's Montre 8 is drawn.
    for n in 0..64 {
        h.automate(0, &stop_param_id(n), 0.0);
    }
    h.automate(0, &stop_param_id(7), 1.0);
    let scl = "! pentatonic.scl\nA pentatonic scale\n5\n200.0\n400.0\n700.0\n900.0\n2/1\n";
    // Twelve keys to the octave; the black keys play nothing.
    let kbm = "! white.kbm\n12\n0\n127\n60\n69\n440.0\n5\n0\nx\n1\nx\n2\nx\nx\n3\nx\n4\nx\nx\n";
    assert!(
        h.plugin_mut()
            .controller()
            .lock()
            .unwrap()
            .use_scala(scl, Some(kbm))
            .unwrap()
    );
    h.render(256);
    for (key, sounds) in [(60u8, true), (61, false), (62, true), (66, false)] {
        h.schedule(
            h.position(),
            EventKind::NoteOn {
                note: NoteRef::new(0, key),
                velocity: 0.8,
            },
        );
        let before = h.plugin_mut().engine().unwrap().active_voices();
        h.render(256);
        let after = h.plugin_mut().engine().unwrap().active_voices();
        assert_eq!(after > before, sounds, "key {key}");
    }
    assert!((cents(key_hz(&mut h, 62), key_hz(&mut h, 60)) - 200.0).abs() < 1e-6);
}
