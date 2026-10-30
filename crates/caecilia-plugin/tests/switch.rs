//! The organ changes while the instrument plays.

use caecilia_model::{Organ, read_organ};
use caecilia_plugin::{
    Action, Binding, Caecilia, FADE_SECONDS, OrganLoadError, Source, stop_param_id,
};
use caecilia_registration::{Piston, factory_generals};
use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{EventKind, NoteRef};
use manguebyte_dsp::tuning::{Scale, TuningSettings};
use manguebyte_plugin::{Harness, Plugin};

/// A two-manual English chamber organ: three Great stops open it.
const CHAMBER: &str = r#"{
  "name": "Chamber organ",
  "windchests": [ { "name": "Main", "pressurePa": 650, "tremulant": true } ],
  "ranks": [
    { "name": "Stopped Diapason 8", "family": "Flute", "footage": 8, "windchest": "Main" },
    { "name": "Principal 4", "family": "Principal", "footage": 4, "windchest": "Main" },
    { "name": "Fifteenth 2", "family": "Principal", "footage": 2, "windchest": "Main" },
    { "name": "Hautboy 8", "family": "Reed", "footage": 8, "windchest": "Main" }
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

fn chamber() -> Organ {
    read_organ(CHAMBER, None).expect("the chamber organ").organ
}

/// A file of the chamber organ, its own per test.
fn chamber_file(name: &str) -> std::path::PathBuf {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("organ-switch");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::write(&path, CHAMBER).unwrap();
    path
}

/// The demonstration organ, dry: the reverb would blur a click.
fn harness() -> Harness<Caecilia> {
    let mut h = Harness::<Caecilia>::new(48000.0, 256);
    h.automate(0, "reverb", 0.0);
    h
}

fn press(h: &mut Harness<Caecilia>, frame: u64, keys: &[u8]) {
    for &key in keys {
        h.schedule(
            frame,
            EventKind::NoteOn {
                note: NoteRef::new(0, key),
                velocity: 0.8,
            },
        );
    }
}

fn drawn(h: &Harness<Caecilia>) -> Vec<u16> {
    let mask = h.params().stop_mask();
    (0..64).filter(|n| mask & (1 << n) != 0).collect()
}

/// Channel `c` of two renders, one after the other.
fn joined(a: &AudioBuffer<f32>, b: &AudioBuffer<f32>, c: usize) -> Vec<f32> {
    a.channel(c).iter().chain(b.channel(c)).copied().collect()
}

/// The largest step from one sample to the next, either channel.
fn largest_step(x: &[Vec<f32>; 2], from: usize, to: usize) -> f32 {
    x.iter()
        .flat_map(|x| (from.max(1)..to).map(move |i| (x[i] - x[i - 1]).abs()))
        .fold(0.0, f32::max)
}

fn rms(x: &[f32]) -> f32 {
    (x.iter().map(|v| v * v).sum::<f32>() / x.len() as f32).sqrt()
}

#[test]
fn a_held_chord_goes_on_sounding_on_the_new_organ_without_a_click() {
    let mut h = harness();
    press(&mut h, 0, &[60, 64, 67]);
    let before = h.render(9600);

    let ctl = h.plugin_mut().controller();
    ctl.lock().unwrap().use_organ(chamber()).unwrap();
    let after = h.render(9600);

    assert_eq!(h.plugin_mut().organ().name, "Chamber organ");
    assert_eq!(drawn(&h), [0, 1, 2], "the new organ opens as it opens");
    assert_eq!(
        h.plugin_mut().engine().unwrap().active_voices(),
        9,
        "the three held keys on its three Great stops"
    );

    // No click: the change moves the waveform no faster than the chord did.
    let x = [joined(&before, &after, 0), joined(&before, &after, 1)];
    let change = 9600;
    let window = (2.0 * FADE_SECONDS * 48000.0) as usize;
    let steady = largest_step(&x, change - window, change);
    let across = largest_step(&x, change, change + window);
    assert!(
        across < 1.5 * steady,
        "a step of {across} across the change, {steady} while it held"
    );
    // No hole: every few milliseconds of it sound.
    let level = rms(&x[0][change - window..change]);
    for start in (change..change + window).step_by(128) {
        let r = rms(&x[0][start..start + 128]);
        assert!(r > 0.2 * level, "{r} at frame {start}, {level} before");
    }
}

#[test]
fn the_new_organ_brings_its_own_pistons_and_a_fresh_history() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    let params = h.plugin_mut().params();
    // On the demonstration organ: a stop drawn, a general of the organist's,
    // a stop and a piston learned.
    h.automate(0, &stop_param_id(20), 1.0);
    h.render(256);
    ctl.lock().unwrap().capture_general(12);
    for (number, action) in [(80, Action::ToggleStop(20)), (81, Action::General(3))] {
        params.console.bind(Binding {
            source: Source::Controller { channel: 0, number },
            action,
        });
    }

    // What the demonstration organ played last, not yet in the history when
    // the change comes, does not go into the new organ's.
    h.automate(256, &stop_param_id(21), 1.0);
    h.render(256);
    ctl.lock().unwrap().use_organ(chamber()).unwrap();
    h.render(512);

    let mut ctl = ctl.lock().unwrap();
    assert_eq!(
        ctl.state().stops.to_mask(),
        0b111,
        "the history starts from the opening"
    );
    assert!(ctl.undo().is_empty(), "nothing before the opening to undo");
    let organ = chamber();
    for (n, (_, piston)) in factory_generals(&organ).into_iter().enumerate() {
        if let Piston::Snapshot(state) = piston {
            assert_eq!(params.console.general(n), Some((state.stops.to_mask(), 0)));
        }
    }
    assert_eq!(
        params.console.general(12),
        Some((0, 0)),
        "the organist's are empty"
    );
    let learned: Vec<Action> = params.console.bindings().iter().map(|b| b.action).collect();
    assert_eq!(
        learned,
        [Action::General(3)],
        "a piston keeps its control, a stop of the old organ does not"
    );
}

#[test]
fn the_session_remembers_its_organ() {
    let path = chamber_file("session.organ.json");
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    let warnings = ctl.lock().unwrap().load_organ(&path).unwrap();
    assert!(warnings.is_empty(), "{warnings:?}");
    h.render(256);
    h.automate(256, &stop_param_id(3), 1.0);
    h.render(256);
    ctl.lock().unwrap().capture_general(9);
    let state = h.save_state();
    assert!(state.contains("session.organ.json"), "{state}");

    // A new instance, on the demonstration organ, opens the session.
    let mut again = harness();
    again.load_state(&state).unwrap();
    again.render(512);
    assert_eq!(again.plugin_mut().organ().name, "Chamber organ");
    assert_eq!(drawn(&again), [0, 1, 2, 3], "the session's registration");
    let params = again.plugin_mut().params();
    assert_eq!(params.console.general(9), Some((0b1111, 0)));
    let ctl = again.plugin_mut().controller();
    assert_eq!(ctl.lock().unwrap().state().stops.to_mask(), 0b1111);

    // And a session of the demonstration organ takes it back, once the
    // first change has faded.
    let demo = harness().save_state();
    again.load_state(&demo).unwrap();
    again.render(4800);
    assert_eq!(
        again.plugin_mut().organ().name,
        caecilia_model::demo_organ().name
    );
}

#[test]
fn a_file_that_does_not_load_leaves_the_organ_playing() {
    let mut h = harness();
    press(&mut h, 0, &[60]);
    h.render(2048);
    let voices = h.plugin_mut().engine().unwrap().active_voices();
    let ctl = h.plugin_mut().controller();
    let missing = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("no-such.organ.json");
    let result = ctl.lock().unwrap().load_organ(&missing);
    assert!(matches!(result, Err(OrganLoadError::File(_))), "{result:?}");
    h.render(2048);
    assert_eq!(
        h.plugin_mut().organ().name,
        caecilia_model::demo_organ().name
    );
    assert_eq!(h.plugin_mut().engine().unwrap().active_voices(), voices);
    assert_eq!(h.plugin_mut().params().organ.generation(), 0);
}

#[test]
fn a_tuning_of_the_controllers_outlives_the_change() {
    let mut h = harness();
    let ctl = h.plugin_mut().controller();
    let nineteen = TuningSettings {
        scale: Scale::equal(19),
        ..TuningSettings::default()
    };
    assert!(ctl.lock().unwrap().use_tuning(&nineteen).unwrap());
    h.render(256);
    ctl.lock().unwrap().use_organ(chamber()).unwrap();
    h.render(256);
    let hz = h.plugin_mut().engine().unwrap().key_frequency(69 + 19);
    assert!((hz - 880.0).abs() < 1e-9, "{hz}");
}

#[test]
fn an_organ_chosen_before_activation_is_the_one_prepared() {
    use caecilia_plugin::CaeciliaParams;
    use manguebyte_dsp::ProcessSpec;
    use manguebyte_plugin::Plugin;

    let mut plugin = Caecilia::new();
    let params: std::sync::Arc<CaeciliaParams> = plugin.params();
    params.use_organ(chamber()).unwrap();
    plugin.prepare(&ProcessSpec {
        sample_rate: 44100.0,
        max_block_size: 128,
        num_channels: 2,
    });
    assert_eq!(plugin.organ().name, "Chamber organ");
    let opening: Vec<u16> = (0..64)
        .filter(|n| params.stop_mask() & (1 << n) != 0)
        .collect();
    assert_eq!(opening, [0, 1, 2]);
}
