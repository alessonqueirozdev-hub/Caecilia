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
//! start; the sound card and the MIDI ports, chosen on the console's
//! Sound > Audio page, in `audio.txt` beside it. Closing the window quits. With `--no-window`, or where there are
//! no application windows, it plays from the terminal: `organ <file>`
//! changes the organ; an empty line quits.

use std::cell::RefCell;
use std::io::BufRead;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use caecilia_plugin::{Caecilia, DeviceEntry, DeviceRequest, DevicesView};
use manguebyte_params::ParamSet;
use manguebyte_standalone::{SettingsChoices, Standalone, StandaloneOptions};
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

    // The sound card and the MIDI ports as the player last chose them.
    let session = session_path();
    let audio = session.as_ref().map(|p| p.with_file_name("audio.txt"));
    let mut options = StandaloneOptions {
        block_size: 256,
        ..StandaloneOptions::default()
    };
    if let Some(saved) = audio
        .as_ref()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|text| text.parse().ok())
    {
        options.settings = saved;
    }
    let player = Rc::new(RefCell::new(Standalone::<Caecilia>::start(options)?));

    if let Some(text) = session
        .as_ref()
        .and_then(|p| std::fs::read_to_string(p).ok())
    {
        match player.borrow().load_state(&text) {
            Ok(_) => println!("Restored the last session."),
            Err(e) => println!("The last session could not be restored: {e}"),
        }
    }
    // The organ asked for wins over the session's.
    if let Some(path) = &organ_arg {
        let p = player.borrow();
        if p.params().organ.path() != std::path::absolute(path).ok() {
            change_organ(&p, path);
        }
    }
    {
        let p = player.borrow();
        println!("Organ: {}", p.params().organ.organ().name);
        println!("{}", p.report());
        match p.midi_input_names().as_slice() {
            [] => println!("No MIDI keyboard is connected."),
            names => println!("Playing from: {}", names.join(", ")),
        }
    }
    println!(
        "MIDI channels play the divisions the organ file assigns; program changes 1-16 recall the generals."
    );
    // Served before the console opens, so its Audio page is there at once.
    serve_devices(&player, audio.clone());
    match app.as_ref().map(open_console) {
        Some(Ok(console)) => {
            println!("The console is open; closing it quits.");
            println!("Settings chooses the sound card and the MIDI ports.");
            if let Some(app) = &app {
                app.run()?;
            }
            drop(console);
            caecilia_plugin::stop_serving_devices();
        }
        failed => {
            caecilia_plugin::stop_serving_devices();
            if let Some(Err(e)) = failed {
                println!("The console did not open ({e}); playing from the terminal.");
            }
            terminal(&player.borrow())?;
        }
    }

    let p = player.borrow();
    if let Some(path) = &audio {
        save_audio(&p, path);
    }
    if let Some(path) = session {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&path, p.save_state())?;
        println!("Session saved to {}.", path.display());
    }
    Ok(())
}

/// Keep the player's choice of devices for the next start.
fn save_audio(player: &Standalone<Caecilia>, path: &Path) {
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    if let Err(e) = std::fs::write(path, player.settings().to_string()) {
        println!("The audio settings could not be saved: {e}");
    }
}

/// Serve the console's Audio page from `player`: carry out what the player
/// asks, look for devices plugged in or pulled out, and say what there is.
/// A change that took is saved to `audio` at once.
fn serve_devices(player: &Rc<RefCell<Standalone<Caecilia>>>, audio: Option<PathBuf>) {
    let player = Rc::clone(player);
    let mut listed: Option<SettingsChoices> = None;
    caecilia_plugin::serve_devices(move |requests, describe| {
        let mut p = player.borrow_mut();
        for request in requests {
            let report = match request {
                DeviceRequest::Output(id) => p.set_output_device(id),
                DeviceRequest::SampleRate(rate) => p.set_sample_rate(*rate),
                DeviceRequest::BufferSize(frames) => p.set_buffer_size(*frames),
                DeviceRequest::MidiInput(id, open) => p.set_midi_input(id, *open),
            };
            println!("{report}");
        }
        if !requests.is_empty() {
            if let Some(path) = &audio {
                save_audio(&p, path);
            }
        }
        if let Some(report) = p.poll_devices() {
            println!("{report}");
            listed = None;
        }
        if describe || listed.is_none() || !requests.is_empty() {
            listed = p.choices().ok();
        }
        view_of(&p, listed.as_ref())
    });
}

/// What the console's Audio page shows of `player`, its devices as `listed`.
fn view_of(player: &Standalone<Caecilia>, listed: Option<&SettingsChoices>) -> DevicesView {
    let report = player.report();
    let settings = player.settings();
    let playing = report.output.as_ref().map(|o| o.id.as_str());
    let Some(listed) = listed else {
        return DevicesView {
            report: report.to_string(),
            failed: report.error.is_some(),
            ..DevicesView::default()
        };
    };
    let output = listed
        .outputs
        .iter()
        .find(|o| Some(o.device.id.as_str()) == playing);
    DevicesView {
        outputs: listed
            .outputs
            .iter()
            .map(|o| DeviceEntry {
                id: o.device.id.clone(),
                name: o.device.name.clone(),
                chosen: Some(o.device.id.as_str()) == playing,
                is_default: o.is_default,
            })
            .collect(),
        sample_rates: output.map(|o| o.sample_rates.clone()).unwrap_or_default(),
        sample_rate: settings.sample_rate,
        buffer_sizes: output.map(|o| o.buffer_sizes.clone()).unwrap_or_default(),
        buffer_size: settings.buffer_size,
        midi_inputs: listed
            .midi_inputs
            .iter()
            .map(|m| DeviceEntry {
                id: m.port.id.clone(),
                name: m.port.name.clone(),
                chosen: m.open,
                is_default: false,
            })
            .collect(),
        report: report.to_string(),
        failed: report.error.is_some(),
    }
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
