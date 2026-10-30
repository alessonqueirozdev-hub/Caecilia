//! Loads built Caecilia bundles the way a host does and plays a chord.
//!
//!   cargo run --profile caecilia-release -p caecilia-plugin --example host_check -- \
//!       <target>/bundled/Caecilia.clap <target>/bundled/Caecilia.vst3
//!
//! For each bundle: the plugin's entry, its parameter count, a C major chord
//! for one second through the format's own processor, and the level and
//! peak that came out. A bundle that fails to load, or plays silence, fails.

#![allow(unsafe_code)]

use manguebyte_core::AudioBuffer;
use manguebyte_dsp::event::{Event, EventBuffer, EventKind, NoteRef};
use manguebyte_dsp::{ProcessContext, Transport};
use manguebyte_host::any::Library;

fn main() {
    let paths: Vec<String> = std::env::args().skip(1).collect();
    assert!(!paths.is_empty(), "give the bundles to check");
    let mut failed = false;
    for path in &paths {
        match check(path) {
            Ok(line) => println!("ok    {path}: {line}"),
            Err(e) => {
                println!("FAIL  {path}: {e}");
                failed = true;
            }
        }
    }
    if failed {
        std::process::exit(1);
    }
}

fn check(path: &str) -> Result<String, String> {
    // SAFETY: loading a plugin runs its code; these are our own bundles.
    let library = unsafe { Library::load(path) }.map_err(|e| format!("load: {e}"))?;
    let entry = library
        .plugins()
        .first()
        .ok_or("no plugin in the bundle")?
        .clone();
    let mut plugin = library
        .create(&entry.id)
        .map_err(|e| format!("create: {e}"))?;
    let params = plugin.params().len();
    let takes_notes = plugin.takes_notes();
    let mut processor = plugin
        .activate(48000.0, 256)
        .map_err(|e| format!("activate: {e}"))?;

    let transport = Transport::default();
    let mut chord = EventBuffer::with_capacity(8);
    for key in [60u8, 64, 67] {
        let _ = chord.push(Event {
            offset: 0,
            kind: EventKind::NoteOn {
                note: NoteRef::new(0, key),
                velocity: 0.8,
            },
        });
    }
    let quiet = EventBuffer::with_capacity(1);
    let mut buffer = AudioBuffer::<f32>::new(2, 256);
    let (mut sum, mut n, mut peak) = (0.0f64, 0usize, 0.0f32);
    for block in 0..(48000 / 256) {
        buffer.clear();
        let events = if block == 0 { &chord } else { &quiet };
        processor.process_with(&mut buffer, &ProcessContext::new(events, &transport));
        if block >= 94 {
            for c in 0..2 {
                for &x in buffer.channel(c) {
                    sum += f64::from(x) * f64::from(x);
                    n += 1;
                    peak = peak.max(x.abs());
                }
            }
        }
    }
    let rms_db = 10.0 * (sum / n.max(1) as f64).max(1e-24).log10();
    if rms_db < -60.0 {
        return Err(format!("silent ({rms_db:.1} dBFS)"));
    }
    Ok(format!(
        "{} \"{}\", {params} parameters, takes notes: {takes_notes}, chord {rms_db:.1} dBFS rms, peak {:.1} dBFS",
        entry.id,
        entry.name,
        20.0 * f64::from(peak).max(1e-12).log10()
    ))
}
