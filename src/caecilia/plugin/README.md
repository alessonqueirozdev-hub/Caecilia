<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# caecilia::plugin

The JUCE wrapper — the VST3 / AU / Standalone `AudioProcessor` and its editor shell.

The Audio Unit is named unconditionally in `FORMATS`, because JUCE already drops
formats a platform cannot build and a hand-written `if(APPLE)` would be wrong under
an iOS toolchain. What makes that safe rather than merely believed is the
configure-time assertion beside it: an AU target appearing on a non-Apple host is a
`FATAL_ERROR`, so the Windows and Linux CI jobs stand as the proof it does not
happen. The macOS job builds the component, ad-hoc signs it — JUCE only signs AU
through `COPY_PLUGIN_AFTER_BUILD`, which is off here — and runs `auval` over it.
This is **one of only two modules allowed to include JUCE** (the other is `ui`);
it is pure glue between the host and the pure `caecilia_core` engine and owns **no
DSP, synthesis or registration logic** of its own.

The Apache-2.0 / JUCE license boundary is enforced at the **target** level: this
module contributes to the `Caecilia` JUCE target, while all audio logic lives in
the JUCE-free `caecilia_core` static library. JUCE is a separate third-party
dependency — see [`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md).

## The seam

The processor holds a `core::engine::AudioEngine` and talks to it through exactly
two lock-free channels — never by mutating engine state directly:

```
  host MIDI + host parameters                        host output buffer
            │                                                 ▲
            ▼   (audio thread, single producer)               │
   CommandBridge ──push──▶ SPSC EngineCommand ring ──▶ AudioEngine::processBlock
                                                              │  publishes
                                                              ▼
   editor / UI  ◀──poll── MeterBridge ◀── double-buffered MeterSnapshot
```

- **Mutation** (note on/off, sustain, panic, reverb, tremulant, temperament) is
  encoded as `EngineCommand` values by `CommandBridge` and pushed onto the
  engine's SPSC ring. `CommandBridge` is the ring's single upstream producer,
  running inside `processBlock`.
- **Except the console's own edits.** Registration, reverb space, master EQ and
  the trims are applied on the message thread under the processor's callback
  lock, not over the ring (`setUiRegistration`, `setUiReverb`, `setUiEqGain`).
  Drawn stops never become `StopEngage` / `StopDisengage` commands.
- **Reads** (levels, voice count) reach the editor through the `ui::StateMirror`
  frame the audio thread publishes each block, plus two output-peak atomics for
  the VU. `MeterBridge` wraps the same wait-free `MeterSnapshot` but nothing polls
  it yet. The audio thread never touches a `juce::Component`. The snapshot's wind
  fields are carried but never filled in.

## Real-time contract

- `prepareToPlay` is the **only** place allocation happens — it forwards the host
  sample rate / block size to `AudioEngine::prepare`.
- `processBlock` sets `juce::ScopedNoDenormals` (per-thread FTZ/DAZ), encodes host
  intent onto the ring, wraps the host buffer in a JUCE-free `core::AudioBlock`
  (the only audio type crossing the seam), and calls `AudioEngine::processBlock`.
- A full command ring **drops** the command rather than blocking; parameter state
  is idempotently re-sent on the next block.

## Contents

| Type | Header | Role |
| --- | --- | --- |
| `CaeciliaAudioProcessor` (`CaeciliaProcessor`) | `PluginProcessor.h` | The VST3/AU/Standalone `juce::AudioProcessor`; owns the engine + bridges; state save/load; PDC/tail reporting. |
| `CaeciliaEditor` | `PluginEditor.h` | Serves the embedded `docs/mockups/console.html` to a `juce::WebBrowserComponent`, registers the JS→C++ native functions, and pushes `ui::StateMirror` frames to the page at 30 Hz. |
| `ParameterLayout` | `ParameterLayout.h` | Stable host-parameter IDs + the APVTS layout factory (global controls + a reserved boolean stop pool). |
| `CaeciliaParameterMirror` | `ParameterMirror.h` | Owns the APVTS and the saved-state document. Also holds a semantic registration `ValueTree` (still empty — nothing writes it) and a `bindOrgan` that would map the stop pool onto a loaded organ, which nothing calls. |
| `CommandBridge` | `CommandBridge.h` | Single-producer encoder: host MIDI + changed parameters → `EngineCommand` ring, with per-block change detection. |
| `MeterBridge` | `MeterBridge.h` | Read-only view over the engine's lock-free `MeterSnapshot`, plus dB helpers. |

## Why a reserved stop-parameter pool

A drawstop is naturally a boolean host parameter, but the concrete stop list is
only known once an organ loads — and JUCE forbids adding parameters after the
processor is constructed. `ParameterLayout` therefore reserves a fixed pool of
`kMaxStopParameters` generic boolean parameters, which
`CaeciliaParameterMirror::bindOrgan` is meant to map onto concrete `StopId`s at
load time. **Nothing calls `bindOrgan` yet**, so the whole pool is inert and the
sounding registration comes from the console instead
(`CaeciliaAudioProcessor::setUiRegistration`).

## Status / roadmap notes

The scaffold wires the whole host↔engine seam with coherent, compilable bodies.
The `// TODO(phaseN)` markers in the sources are the remaining depth work:

- `phase0.1` — the voice arena, tuning table and master reverb are bound from
  `prepareToPlay`; still open: the wind supply (`AudioEngine::stepWind()` is
  empty) and a windchest count taken from the organ instead of the hard-coded 1.
- `phase0.2` — `StateMirror` is mounted and polled by the editor. `ui::ConsoleView`
  is gone (see `src/caecilia/ui/README.md`); the console's wind gauges remain a JS
  simulation until the engine steps the wind model.
- `phase0.3` — report real PDC latency from the bound reverb / oversampler.
- `phase0.6` — sample-accurate note scheduling (add a sub-block offset to the note
  command payload).
- `phase0.7` — fan a key press out to every engaged stop's rank pipe; mirror
  registration provenance into the parallel `ValueTree`.
- `phase0.8` — route learned CC / program-change and si5/do6 sequencer navigation
  through the shared MIDI router instead of only note on/off + all-notes-off.

These are internal upgrades behind stable public signatures.
