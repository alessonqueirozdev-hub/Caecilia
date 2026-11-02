// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//! What the console's controls do.
//!
//! A registration change goes through the registration controller, so the
//! history knows who made it (a stop drawn by hand, a general recalled),
//! and the controller writes the parameters. Each parameter that changed
//! is then told to the host as a gesture, so a host recording automation
//! records it as it would a knob turned in its own window.

use manguebyte_params::Param;
use manguebyte_plugin::editor::{EditorContext, Gesture};

use caecilia_model::{CouplerId, StopId};
use caecilia_registration::{Command, RegistrationState, Selector};

use super::Shared;
use crate::UiNote;
use crate::console::GENERALS;

/// The registration as the parameters have it.
fn masks(shared: &Shared) -> (u64, u32) {
    (shared.params.stop_mask(), shared.params.coupler_mask())
}

/// Tell the host of every stop and coupler that changed.
fn announce(context: &EditorContext, shared: &Shared, before: (u64, u32)) {
    let (stops, couplers) = masks(shared);
    let value = |on: bool| if on { 1.0 } else { 0.0 };
    for (n, p) in shared.params.stops.iter().enumerate() {
        let bit = 1u64 << n;
        if (before.0 ^ stops) & bit != 0 {
            Gesture::set_once(context, p.param_id(), value(stops & bit != 0));
        }
    }
    for (n, p) in shared.params.couplers.iter().enumerate() {
        let bit = 1u32 << n;
        if (before.1 ^ couplers) & bit != 0 {
            Gesture::set_once(context, p.param_id(), value(couplers & bit != 0));
        }
    }
}

/// Apply `command` through the controller, then tell the host.
fn command(context: &EditorContext, shared: &Shared, command: Command) {
    let before = masks(shared);
    if let Ok(mut controller) = shared.controller.lock() {
        controller.apply(command);
    }
    announce(context, shared, before);
}

/// Draw or retire stop `n`, by hand.
pub(crate) fn toggle_stop(context: &EditorContext, shared: &Shared, n: usize) {
    command(context, shared, Command::ToggleStop(StopId(n as u16)));
}

/// Draw or retire coupler `n`, by hand.
pub(crate) fn toggle_coupler(context: &EditorContext, shared: &Shared, n: usize) {
    command(context, shared, Command::ToggleCoupler(CouplerId(n as u16)));
}

/// Turn the tremulant on or off.
pub(crate) fn toggle_tremulant(context: &EditorContext, shared: &Shared) {
    let p = &shared.params.tremulant;
    let next = if p.value() { 0.0 } else { 1.0 };
    Gesture::set_once(context, p.param_id(), next);
}

/// Recall general `n`.
pub(crate) fn recall(context: &EditorContext, shared: &Shared, n: usize) {
    let before = masks(shared);
    if let Ok(mut controller) = shared.controller.lock() {
        controller.recall_general(n);
    }
    announce(context, shared, before);
}

/// Store what is drawn in general `n`.
pub(crate) fn capture(shared: &Shared, n: usize) {
    if let Ok(mut controller) = shared.controller.lock() {
        controller.capture_general(n);
    }
}

/// The next general in the row, or the previous one.
pub(crate) fn step(context: &EditorContext, shared: &Shared, forward: bool) {
    let next = match (shared.params.console.current_general(), forward) {
        (None, _) => 0,
        (Some(n), true) => (n + 1).min(GENERALS - 1),
        (Some(n), false) => n.saturating_sub(1),
    };
    recall(context, shared, next);
}

/// Retire every stop and coupler.
pub(crate) fn cancel(context: &EditorContext, shared: &Shared) {
    command(
        context,
        shared,
        Command::RecallSnapshot(RegistrationState::default()),
    );
}

/// Draw exactly `state`, as a demonstration asks.
pub(crate) fn recall_state(context: &EditorContext, shared: &Shared, state: RegistrationState) {
    command(context, shared, Command::RecallSnapshot(state));
}

/// Step back through the history.
pub(crate) fn undo(context: &EditorContext, shared: &Shared) {
    let before = masks(shared);
    if let Ok(mut controller) = shared.controller.lock() {
        controller.undo();
    }
    announce(context, shared, before);
}

/// Step forward.
pub(crate) fn redo(context: &EditorContext, shared: &Shared) {
    let before = masks(shared);
    if let Ok(mut controller) = shared.controller.lock() {
        controller.redo();
    }
    announce(context, shared, before);
}

/// Draw every stop `selector` takes, or retire them; returns how many
/// stops changed.
pub(crate) fn selector(
    context: &EditorContext,
    shared: &Shared,
    selector: Selector,
    draw: bool,
) -> u32 {
    let before = masks(shared);
    let c = if draw {
        Command::EngageSelector(selector)
    } else {
        Command::DisengageSelector(selector)
    };
    command(context, shared, c);
    (before.0 ^ masks(shared).0).count_ones()
}

/// Go `distance` steps through the history: back below 0, forward along
/// redo's branch above.
pub(crate) fn travel(context: &EditorContext, shared: &Shared, distance: i32) {
    if distance == 0 {
        return;
    }
    let before = masks(shared);
    if let Ok(mut controller) = shared.controller.lock() {
        for _ in 0..distance.unsigned_abs() {
            if distance < 0 {
                controller.undo();
            } else {
                controller.redo();
            }
        }
    }
    announce(context, shared, before);
}

/// Play the organ `entry` names: its file, or the demonstration organ.
/// The host is told the parameters' names and values changed.
pub(crate) fn load_organ(
    context: &EditorContext,
    shared: &Shared,
    entry: &super::library::Entry,
) -> Result<(), String> {
    let loaded = match &entry.path {
        Some(path) => shared.params.load_organ_file(path).map(|_| ()),
        None => shared.params.use_organ(caecilia_model::caecilia_organ()),
    };
    loaded.map_err(|e| e.to_string())?;
    context.values_changed();
    Ok(())
}

/// Begin moving the swell shoe.
pub(crate) fn swell_gesture(context: &EditorContext, shared: &Shared) -> Gesture {
    let p = &shared.params.swell;
    Gesture::begin(context, p.param_id(), p.normalized())
}

/// A key or pedal played on the console, to the audio thread. A full ring
/// drops it: the host's own notes are never held up for the console's.
pub(crate) fn play(shared: &Shared, channel: usize, key: u8, on: bool) {
    if let Ok(mut notes) = shared.notes.lock() {
        let _ = notes.push(UiNote::Key {
            channel: channel as u8,
            key,
            on,
        });
    }
}
