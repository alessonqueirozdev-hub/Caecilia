//! Caecilia standalone: the organ on the sound card, played from every
//! connected MIDI keyboard.
//!
//!   cargo run --release -p caecilia-plugin --features standalone --bin caecilia
//!
//! The session -- drawstops, pistons, learned controls, level -- is kept in
//! `%APPDATA%\Caecilia\session.txt` (`~/.config/caecilia/session.txt`
//! elsewhere) and restored on the next start. Press Enter to quit.

use std::io::BufRead;
use std::path::PathBuf;

use caecilia_plugin::Caecilia;
use manguebyte_standalone::{Standalone, StandaloneOptions};

fn session_path() -> Option<PathBuf> {
    let base = std::env::var_os("APPDATA")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(
        base.join(if cfg!(windows) {
            "Caecilia"
        } else {
            "caecilia"
        })
        .join("session.txt"),
    )
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let options = StandaloneOptions {
        block_size: 256,
        ..StandaloneOptions::default()
    };
    let app = Standalone::<Caecilia>::start(options)?;

    let session = session_path();
    if let Some(text) = session
        .as_ref()
        .and_then(|p| std::fs::read_to_string(p).ok())
    {
        match app.load_state(&text) {
            Ok(_) => println!("Restored the last session."),
            Err(e) => println!("The last session could not be restored: {e}"),
        }
    }

    println!(
        "Caecilia on {} at {} Hz.",
        app.device_name(),
        app.sample_rate()
    );
    match app.midi_input_names().as_slice() {
        [] => println!("No MIDI keyboard is connected."),
        names => println!("Playing from: {}", names.join(", ")),
    }
    for problem in app.midi_problems() {
        println!("MIDI: {problem}");
    }
    println!(
        "Channel 1 plays the Grand-Orgue, 2 the Récit, 3 the Pédale; program changes 1-16 recall the generals."
    );
    println!("Press Enter to quit.");

    let mut line = String::new();
    let _ = std::io::stdin().lock().read_line(&mut line);

    if let Some(path) = session {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&path, app.save_state())?;
        println!("Session saved to {}.", path.display());
    }
    Ok(())
}
