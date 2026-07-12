<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# caecilia::synth — Synthesis module

Voice implementations behind the pure `caecilia::core::IVoice` contract. This
module turns a keypress on a rank into audio, and it is where Caecilia's core
thesis becomes sound: **a voice is a layered pipeline, not one engine per rank.**

Every voice is a composition of an **attack layer**, a **loop-free modeled
sustain layer**, and a **release layer**, sharing one spectral seed and a
deterministic per-pipe voicing. Pure-sample and pure-additive ranks are simply
degenerate configurations of that same pipeline — the safe, fully-shippable
fallbacks, never abandoned.

## What it depends on

- **`caecilia_core`** (plus the C++ standard library) and the **`wind`** module,
  whose `wind::WindResponseCurve` the partial bank consumes for family-specific
  breathing. No JUCE.
- Where the design still calls for the `dsp` module (the 16-point Kaiser sinc
  interpolator, the oversampled nonlinear island), this scaffold uses
  **self-contained placeholders marked `// TODO(phaseN)`** so the module compiles
  today. The seams (`ISampleSource`, `IWindSupply` from core) are already in place
  for that module to plug into.

## Public API

### Voices (`core::IVoice` implementations)

| Type | Engine | Role |
|------|--------|------|
| `SampleVoice` | `EngineKind::Sample` | Multi-sample playback (linear-interp resampler → `dsp` sinc later). The safe fallback tier. |
| `AdditiveVoice` | `EngineKind::Additive` | Wind-modulated `PartialBank`. The CPU-cheap plenum tier and the demotion target. |
| `PhysicalPipeVoice` | `EngineKind::Waveguide` / `Modal` | Nonlinear excitation + resonator, wind-coupled. The expensive solo/exposed tier. |

### Selection

- `VoiceFactory` — picks the engine/tier per rank from a `RankVoiceRequest`
  (`selectEngine`, `selectTier`) and builds a prepared voice (`create`, off the
  audio thread). Exposed stops earn the physical tier; ranks with a
  `SpectralModel` get modeled-additive; ranks with only a sample set fall back to
  `SampleVoice`.

### Layer & component interfaces

- `IVoiceLayer` — common contract for attack/sustain/release stages.
- `ISampleSource` — read-only recorded-sample provider (loaded by `model`).
- `IModeledSustain` — loop-free sustain seeded from a `SpectralModel`
  (`seedFrom`); implemented by `PartialBank`.
- `IExcitation` — `NonlinearJet` (flues), `BeatingReed` (reeds).
- `IResonator` — `Waveguide` (flues), `ModalResonator` (reeds).

### Building blocks & value types

- `PartialBank` / `Partial` — the wind-modulated additive/modal partial bank.
- `PerPipeVoicer` → `VoicingProfile` — deterministic per-pipe hand-voicing
  variance from a stable `core::PipeId` seed (a mixture shimmers, never cloned).
- `VoicingParams` — a rank's voicing recipe (the scatter ranges).
- `SpectralModel` / `PartialTrack` / `FormantEnvelope` — the analysis bridge that
  lets a sampled attack and a modeled sustain share one timbre.
- `AttackSpliceConfig`, `ReleaseSpec`, `VoiceContext`, `ArEnvelope` — the
  splice/release/wind-coupling configuration types (the per-family response curve
  itself is `wind::WindResponseCurve`, owned by the `wind` module).

## Real-time contract

- `prepare()` (and the off-thread `VoiceFactory::create`, `setComponents`,
  `set*` configurators) are the **only** places that allocate or precompute.
- `noteOn`, `noteOff`, `renderAdd`, `isActive`, `kind`, `tier`,
  `cpuCostEstimate` are `noexcept`, allocation-free and lock-free.
- `renderAdd` **accumulates** (`+=`) into the block so many voices sum into one
  per-windchest bus — it never overwrites.
- Wind is read through the immutable per-block `core::IWindSupply` snapshot; the
  voice never mutates shared state on the audio thread.

## Status (roadmap phase)

- **Working now:** the full layered structure, `VoiceFactory` tiering,
  deterministic `PerPipeVoicer`, an audible wind-aware `PartialBank`/
  `AdditiveVoice`, and functional `SampleVoice` resampling.
- **Placeholder (later phases):** the nonlinear jet/reed excitations return
  silence while advancing their settle timers (phase 6, oversampled island); the
  attack-splice crossfade and pressure-collapse release chiff are stubbed
  (phases 5–6); the `dsp` sinc interpolator replaces the linear tap (phase 4).
