// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! Caecilia standalone: the organ on the sound card, played from every
//! connected MIDI keyboard and from its console, in a window of its own.
//!
//!   cargo run --release -p caecilia-plugin --features standalone --bin caecilia
//!
//! The session -- the organ, drawstops, pistons, learned controls, level --
//! is kept in `%APPDATA%\Caecilia\session.txt`
//! (`~/.config/caecilia/session.txt` elsewhere) and restored on the next
//! start. Closing the window quits. With `--no-window`, or where there are
//! no application windows, it plays from the terminal: `organ <file>`
//! changes the organ; an empty line quits.

use std::io::BufRead;
use std::path::{Path, PathBuf};

use caecilia_plugin::Caecilia;
use manguebyte_params::ParamSet;
use manguebyte_standalone::{Standalone, StandaloneOptions};
use manguebyte_window::App;

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
    let mut window = true;
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
            "--no-window" => window = false,
            "--help" | "-h" => {
                println!("caecilia [--organ <file.organ.json>] [--no-window]");
                println!("Plays the organ with its console in a window; closing it quits.");
                println!("With --no-window, `organ <file.organ.json>` typed while it plays");
                println!("changes the organ, and an empty line quits.");
                return Ok(());
            }
            other => return Err(format!("unknown argument '{other}' (try --help)").into()),
        }
    }

    // The console's window, when there can be one.
    let app = if window {
        match App::new() {
            Ok(app) => {
                caecilia_plugin::keep_console_of_new_instances();
                Some(app)
            }
            Err(e) => {
                println!("No window here ({e}); playing from the terminal.");
                None
            }
        }
    } else {
        None
    };

    let options = StandaloneOptions {
        block_size: 256,
        ..StandaloneOptions::default()
    };
    let player = Standalone::<Caecilia>::start(options)?;

    let session = session_path();
    if let Some(text) = session
        .as_ref()
        .and_then(|p| std::fs::read_to_string(p).ok())
    {
        match player.load_state(&text) {
            Ok(_) => println!("Restored the last session."),
            Err(e) => println!("The last session could not be restored: {e}"),
        }
    }
    // The organ asked for wins over the session's.
    if let Some(path) = &organ_arg {
        if player.params().organ.path() != std::path::absolute(path).ok() {
            change_organ(&player, path);
        }
    }
    println!("Organ: {}", player.params().organ.organ().name);

    println!(
        "Caecilia on {} at {} Hz.",
        player.device_name(),
        player.sample_rate()
    );
    match player.midi_input_names().as_slice() {
        [] => println!("No MIDI keyboard is connected."),
        names => println!("Playing from: {}", names.join(", ")),
    }
    for problem in player.midi_problems() {
        println!("MIDI: {problem}");
    }
    println!(
        "MIDI channels play the divisions the organ file assigns; program changes 1-16 recall the generals."
    );
    match app.as_ref().map(open_console) {
        Some(Ok(console)) => {
            println!("The console is open; closing it quits.");
            if let Some(app) = &app {
                app.run()?;
            }
            drop(console);
        }
        failed => {
            if let Some(Err(e)) = failed {
                println!("The console did not open ({e}); playing from the terminal.");
            }
            terminal(&player)?;
        }
    }

    if let Some(path) = session {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&path, player.save_state())?;
        println!("Session saved to {}.", path.display());
    }
    Ok(())
}

/// The console of the instance playing, in a window of `app`.
fn open_console(app: &App) -> Result<manguebyte_gui::AppEditor, String> {
    let (console, params) =
        caecilia_plugin::console_of_last_instance().ok_or("the instance left no console")?;
    let params: std::sync::Arc<dyn ParamSet> = params;
    console
        .open_in_app(app, "Caecilia", Some(params))
        .map_err(|e| e.to_string())
}

/// Plays from the terminal: `organ <file>` changes the organ; an empty line
/// quits.
fn terminal(player: &Standalone<Caecilia>) -> std::io::Result<()> {
    println!("Type `organ <file>` to change the organ; press Enter to quit.");
    for line in std::io::stdin().lock().lines() {
        let line = line?;
        let line = line.trim();
        match line.split_once(char::is_whitespace) {
            _ if line.is_empty() => break,
            Some(("organ", file)) => change_organ(player, Path::new(file.trim().trim_matches('"'))),
            _ => println!("`organ <file>` changes the organ; an empty line quits."),
        }
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
