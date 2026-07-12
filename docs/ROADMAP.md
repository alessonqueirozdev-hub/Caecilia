<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Ceciliae — Roadmap

The phased plan from scaffold to v1. Each phase is a **shippable, testable
increment** that keeps the real-time contract green and the license boundary
clean. Phases build strictly on their predecessors; the degenerate
pure-sample / pure-additive voice remains shippable from v0.4 onward, so there is
always a working instrument.

> Scope: the master plan behind [`ARCHITECTURE.md`](ARCHITECTURE.md). Per-module
> deep dives: [`DSP_DESIGN.md`](DSP_DESIGN.md), [`SYNTHESIS.md`](SYNTHESIS.md),
> [`WIND_MODEL.md`](WIND_MODEL.md), [`REGISTRATION.md`](REGISTRATION.md),
> [`CONTROL_API.md`](CONTROL_API.md), [`UI_DESIGN.md`](UI_DESIGN.md),
> [`TUNING.md`](TUNING.md).

---

## Guiding invariants (every phase)

- **RT contract stays green.** No allocation / lock / logging / exception on any
  `process()` / `render()` / `step()` path; a CI allocator/lock trap enforces it.
- **License boundary stays clean.** Only `plugin` / `ui` include JUCE (no-JUCE
  lint on `core`); no GPL source ingested; `THIRD_PARTY_NOTICES.md` documents
  JUCE/Catch2 separately from Ceciliae's proprietary license.
- **Numerical parity.** Every SIMD backend is bit/ULP-compared against a scalar
  reference in CI.

---

## Phase table

| Version | Theme                              | Marquee deliverable                                    |
|---------|------------------------------------|--------------------------------------------------------|
| v0      | Scaffold & contract                | pure-core headers + tests, no audio                    |
| v0.1    | Audible sine ranks end-to-end      | plugin ↔ core seam proven; RT trap in CI               |
| v0.2    | Wind model + tremulant realism     | ranks that breathe and sag; first console skin         |
| v0.3    | DSP core                           | sinc interp, filters, FDN reverb; SIMD ULP harness     |
| v0.4    | Sample layer + per-pipe voicing    | shippable pure-sample tier (safe fallback)             |
| v0.5    | Hybrid attack-splice + modeled sustain | loop-free sustain from offline `SpectralModel`      |
| v0.6    | Nonlinear physical tier + tiering  | emergent chiff; phase-aligned demotion; SoA/SIMD        |
| v0.7    | Registration brain                 | Selector algebra, branching undo/redo, `explain()`      |
| v0.8    | Control API + MIDI-learn           | OSC/JSON-RPC/MIDI over one grammar; `port()`            |
| v0.9    | UI polish & second skin + tuning   | photoreal skin, temperaments, animated wind gauges      |
| v0.95   | Convolution reverb + hardening     | second reverb; worst-case degradation proofs           |
| v1      | Release                            | full hybrid voice + all pillars, gates green            |

---

## v0 — Scaffold & contract  *(current)*

Create the `C:\Ceciliae` CMake tree: `ceciliae_core` STATIC lib (std-lib only) +
`Ceciliae` JUCE target via FetchContent + Catch2. Write the core vocabulary and
interfaces as headers with coherent stub bodies — `Footage` (exact rational),
`TonalFamily`/`ChorusRole`/`PitchClass`, `PipeId`/`StopId`, `IVoice`/`IVoiceLayer`,
`RenderContext`, `WindState`/`IWindSupply`, `SpscRing`, `VoiceHandle`,
`DeadlineBudget`, the `AudioEngine` seam. Proprietary header on every file;
no-JUCE lint on `core`.

**Exit criteria:** Catch2 tests for `Footage` math and `SpscRing` pass; core
compiles with no JUCE; no audio yet.

---

## v0.1 — Audible sine ranks end-to-end

Minimal `AudioEngine` + `VoicePool` renders band-limited additive/sine voices
through the plugin into a host; MIDI note on/off via the command ring; one flat
`OrganSpec` from a hand-written definition.

**Proves:** the RT contract (allocation-free `processBlock`, generation-tagged
stealing) and the `plugin`/`core` seam. **Exit:** RT-safety allocator/lock trap
wired into CI.

---

## v0.2 — Wind model + tremulant realism

`WindModel` reservoir ODE with sag under polyphony, per-windchest `WindState`
snapshot, `WindResponseCurve` per family, tremulant as wind modulation (see
[`WIND_MODEL.md`](WIND_MODEL.md)). Sine ranks now audibly **breathe and sag**.
UI `StateMirror` + a first `FlatVectorSkin` `ConsoleView` showing keys lighting
and a live wind-pressure gauge, data-driven from the `OrganSpec`
([`UI_DESIGN.md`](UI_DESIGN.md)).

**Exit:** demand→sag feedback audible; UI polls meters lock-free.

---

## v0.3 — DSP core

Fresh 16-point Kaiser-windowed-sinc interpolator + polyphase resampling;
biquad/one-pole/formant filters; the Jot-style **FDN reverb** with
denormal-flushed feedback; per-pipe stereo spatialization. Scalar-vs-SIMD
ULP-bounded CI harness. Master chain in `AudioEngine`. See
[`DSP_DESIGN.md`](DSP_DESIGN.md).

**Exit:** SIMD backends within ULP/RMS budget; reverb tail denormal-safe.

---

## v0.4 — Sample layer + per-pipe voicing

`ISampleSource` streaming from the proprietary sample format (off-thread loading
in `model`), multi-stage release-by-hold-time (`ReleaseSpec`), `PerPipeVoicer`
deterministic variance keyed to `PipeId`.

**Milestone:** pure-sample ranks are **fully shippable — the safe fallback
tier.** From here, there is always a working instrument.

---

## v0.5 — Hybrid attack-splice + modeled sustain

The offline analysis toolchain produces the `SpectralModel`; `IModeledSustain`
(the additive/partial-bank tier) is seeded from it; `AttackSpliceConfig`
phase-aligned crossfade splices a recorded chiff into a **loop-free** modeled
sustain (see [`SYNTHESIS.md`](SYNTHESIS.md) §7).

**Exit:** ABX / spectral-difference validation against reference recordings.

---

## v0.6 — Nonlinear physical tier + tiering

`NonlinearJet` + `Waveguide` (flues) and `BeatingReed` + `ModalResonator` (reeds)
in an oversampled polyBLEP island; **emergent** chiff; `VoiceTier` demotion with a
phase-aligned inaudible crossfade under `DeadlineBudget` pressure; per-`EngineKind`
SoA arenas + SIMD batch for unisons. Numerical-stability guards (soft-limit, DC
block, FTZ/DAZ).

**Exit:** exposed solo stops run the physical tier; tutti degrades to thinning,
never an xrun.

---

## v0.7 — Registration brain

Selector algebra + `SelectorParser` grammar, `RegistrationState` with first-class
couplers, command-sourced **branching** undo/redo, `explain()`,
domain-intelligent build-plenum, pistons (Snapshot|Intent), crescendo. Atomic
`StateDelta` over the ring → click-free wind-modeled stop crossfade. The
`RegistrationPaletteComponent` (family × footage by-intent) in the UI. See
[`REGISTRATION.md`](REGISTRATION.md).

**Exit:** every draw is a click-free crossfade; history is a navigable tree.

---

## v0.8 — Control API + MIDI-learn

OSC + JSON-RPC adapters over the one `ICommandSink`/`SelectorParser` (versioned
grammar); MIDI-learn bindings carrying the shared semantic identity; PC→Generals
and si5/do6 sequencer nav honoured. `RegistrationIntent::port()` across organ
definitions with a confidence score. See [`CONTROL_API.md`](CONTROL_API.md).

**Exit:** a script and a learned MIDI control produce identical results; an intent
recalls sensibly on a *different* organ.

---

## v0.9 — UI polish & second skin + tuning

`PhotorealSkin` (vector geometry + texture brushes + procedural shading, crisp at
4K), cached chrome layers + VBlank dirty-region repaint on Direct2D/Metal with
software fallback, `ViewportController` zoom/pan, `ThemeManager` hot-swap,
`ConsoleAccessibilityHandler`, animated wind gauges. Tuning module: historical
temperaments, stretch, per-pipe detune ([`TUNING.md`](TUNING.md),
[`UI_DESIGN.md`](UI_DESIGN.md)).

**Exit:** both skins over one model/feed; console matches the Organteq polish bar.

---

## v0.95 — Convolution reverb + hardening

Second (convolution) reverb path; worst-case tutti/coupled-load tests proving
graceful degradation; cross-platform SIMD (AVX2/NEON) CI matrix; denormal/NaN
guards across FDN + wind feedback; latency budget verified.

**Exit:** measured worst-case block within deadline; PDC correct.

---

## v1 — Release

Full hybrid voice engine (sample + additive + waveguide as composed layers),
shared wind model, dual DSP reverb, two polished console skins with live feedback,
semantic registration with portability, OSC/JSON-RPC/MIDI-learn control surface,
historical tunings.

**Ship gates (all green):** RT-safety trap, no-JUCE-in-core lint, SIMD ULP
parity, license-boundary check, `THIRD_PARTY_NOTICES.md` current. Proprietary,
closed-source.

---

## Dependency graph (abridged)

```
  v0 contract
    ├─▶ v0.1 engine/plugin seam ─▶ v0.2 wind ─▶ v0.6 nonlinear tier
    │                              └─▶ v0.9 UI (gauges)
    ├─▶ v0.3 dsp ──────────────▶ v0.6 (oversampled island) ─▶ v0.95 convolution
    ├─▶ v0.4 sample ─▶ v0.5 hybrid splice ─▶ v0.6
    ├─▶ v0.7 registration ─▶ v0.8 control/MIDI-learn/port
    └─▶ v0.9 tuning
                                     └────────────────────▶ v1 release
```
