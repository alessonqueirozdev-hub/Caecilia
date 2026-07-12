<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# ceciliae::plugin

The JUCE wrapper — the VST3 / Standalone `AudioProcessor` and its editor shell.
This is **one of only two modules allowed to include JUCE** (the other is `ui`);
it is pure glue between the host and the pure `ceciliae_core` engine and owns **no
DSP, synthesis or registration logic** of its own.

The proprietary / JUCE license boundary is enforced at the **target** level: this
module contributes to the `Ceciliae` JUCE target, while all audio logic lives in
the JUCE-free `ceciliae_core` static library. JUCE is a separate third-party
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

- **Mutation** (note on/off, reverb, tremulant, temperament, stops) is encoded as
  `EngineCommand` values by `CommandBridge` and pushed onto the engine's SPSC
  ring. `CommandBridge` is the ring's single upstream producer, running inside
  `processBlock`.
- **Reads** (levels, wind sag, voice count) come only through `MeterBridge`, a
  thin accessor over the engine's wait-free `MeterSnapshot`. The audio thread
  never touches a `juce::Component`.

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
| `CeciliaeAudioProcessor` (`CeciliaeProcessor`) | `PluginProcessor.h` | The VST3/Standalone `juce::AudioProcessor`; owns the engine + bridges; state save/load; PDC/tail reporting. |
| `CeciliaeEditor` | `PluginEditor.h` | Thin `AudioProcessorEditor` shell; frame-rate polls `MeterBridge`. Hosts `ui::ConsoleView` in a later phase. |
| `ParameterLayout` | `ParameterLayout.h` | Stable host-parameter IDs + the APVTS layout factory (global controls + a reserved boolean stop pool). |
| `CeciliaeParameterMirror` | `ParameterMirror.h` | Owns the APVTS **and** a parallel semantic registration `ValueTree` with `UndoManager`; binds the stop pool to a loaded `OrganSpec`; state serialisation. |
| `CommandBridge` | `CommandBridge.h` | Single-producer encoder: host MIDI + changed parameters → `EngineCommand` ring, with per-block change detection. |
| `MeterBridge` | `MeterBridge.h` | Read-only view over the engine's lock-free `MeterSnapshot`, plus dB helpers. |

## Why a reserved stop-parameter pool

A drawstop is naturally a boolean host parameter, but the concrete stop list is
only known once an organ loads — and JUCE forbids adding parameters after the
processor is constructed. `ParameterLayout` therefore reserves a fixed pool of
`kMaxStopParameters` generic boolean parameters, and
`CeciliaeParameterMirror::bindOrgan` maps pool slots onto concrete `StopId`s at
load time. Unused slots stay inert.

## Status / roadmap notes

The scaffold wires the whole host↔engine seam with coherent, compilable bodies.
The `// TODO(phaseN)` markers in the sources are the remaining depth work:

- `phase0.1` — bind the synthesis voice arena, wind supply, tuning table and
  master reverb into the engine from `prepareToPlay`; windchest count from the
  loaded `OrganSpec`.
- `phase0.2` — mount `ui::ConsoleView` + `StateMirror` in the editor; live wind
  gauges.
- `phase0.3` — report real PDC latency from the bound reverb / oversampler.
- `phase0.6` — sample-accurate note scheduling (add a sub-block offset to the note
  command payload).
- `phase0.7` — fan a key press out to every engaged stop's rank pipe; mirror
  registration provenance into the parallel `ValueTree`.
- `phase0.8` — route learned CC / program-change and si5/do6 sequencer navigation
  through the shared MIDI router instead of only note on/off + all-notes-off.

These are internal upgrades behind stable public signatures.
