//! Caecilia standalone: the organ on the sound card, played from every
//! connected MIDI keyboard.
//!
//!   cargo run --release -p caecilia-plugin --features standalone --bin caecilia
//!
//! The session -- the organ, drawstops, pistons, learned controls, level --
//! is kept in `%APPDATA%\Caecilia\session.txt`
//! (`~/.config/caecilia/session.txt` elsewhere) and restored on the next
//! start. While it plays, `organ <file>` changes the organ; an empty line
//! quits.

use std::io::BufRead;
use std::path::{Path, PathBuf};

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
    // `--organ <file>`: play that organ, whatever the last session played.
    let mut args = std::env::args().skip(1);
    let mut organ_arg = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--organ" => {
                let path = PathBuf::from(args.next().ok_or("--organ needs a file")?);
                match caecilia_plugin::set_organ_file_for_new_instances(&path) {
                    Ok(warnings) => {
                        for w in &warnings {
                            println!("{}: warning: {w}", path.display());
                        }
                    }
                    Err(e) => return Err(format!("{}: {e}", path.display()).into()),
                }
                organ_arg = Some(path);
            }
            "--help" | "-h" => {
                println!("caecilia [--organ <file.organ.json>]");
                println!("While it plays, `organ <file.organ.json>` changes the organ.");
                return Ok(());
            }
            other => return Err(format!("unknown argument '{other}' (try --help)").into()),
        }
    }

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
    // The organ asked for wins over the session's.
    if let Some(path) = &organ_arg {
        if app.params().organ.path() != std::path::absolute(path).ok() {
            change_organ(&app, path);
        }
    }
    println!("Organ: {}", app.params().organ.organ().name);

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
        "MIDI channels play the divisions the organ file assigns; program changes 1-16 recall the generals."
    );
    println!("Type `organ <file>` to change the organ; press Enter to quit.");

    for line in std::io::stdin().lock().lines() {
        let line = line?;
        let line = line.trim();
        match line.split_once(char::is_whitespace) {
            _ if line.is_empty() => break,
            Some(("organ", file)) => change_organ(&app, Path::new(file.trim().trim_matches('"'))),
            _ => println!("`organ <file>` changes the organ; an empty line quits."),
        }
    }

    if let Some(path) = session {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&path, app.save_state())?;
        println!("Session saved to {}.", path.display());
    }
    Ok(())
}

/// Play the organ in `path` from now on, saying how it went.
fn change_organ(app: &Standalone<Caecilia>, path: &Path) {
    match app.params().load_organ_file(path) {
        Ok(warnings) => {
            for w in &warnings {
                println!("{}: warning: {w}", path.display());
            }
            println!("Organ: {}", app.params().organ.organ().name);
        }
        Err(e) => println!("{}: {e}; the organ playing stays.", path.display()),
    }
}
