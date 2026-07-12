<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia

**A proprietary hybrid pipe-organ virtual instrument (VST3 / AU / Standalone).**

> Status: **playable alpha (v0.1).** The VST3 / AU / Standalone plugin builds
> green on Windows (MSVC), macOS and Linux and **makes sound**: a 3-division,
> 26-stop demo organ with per-tonal-family additive voicing, an algorithmic FDN
> reverb, and equal-tempered A=440 tuning. The live console UI is mounted — a
> photoreal skin auto-laid-out from the organ, with a radiating pedalboard, keys
> that light in real time as you play, and drawstops that re-voice the sound when
> drawn. A modern, versioned Windows installer (Inno Setup) ships the VST3 and
> Standalone.
>
> **Working now:** cross-platform plugin • live console + on-screen/MIDI keyboard
> • real-time key lighting • registration that changes timbre • FDN reverb •
> equal temperament • Windows installer.
>
> **Planned next (wired, not yet audible):** wind-model breathing/sag &
> tremulant as wind modulation • live historical-temperament switching •
> coupler / combination-piston / expression-shoe action • the hybrid recorded-
> attack + modeled-sustain voice layer. See [`docs/ROADMAP.md`](docs/ROADMAP.md).
>
> The sections below describe the **full design target**; not every capability
> is wired yet — see the roadmap for the current line.

Caecilia aims to beat GrandOrgue and Aeolus **on sound simultaneously**, match
Organteq **on polish**, and beat everyone on **programmable, portable,
explainable registration**.

---

## Why Caecilia wins

GrandOrgue ships dead, looped samples with no wind response. Aeolus ships
uniform synthetic additive tone with a single global tremulant. Each does in
isolation what a real instrument does together. Caecilia unifies them and
exposes the result through a registration brain no mainstream organ VST has.

### 1. Hybrid, layered voice engine

A voice is **not** "a rank picks one engine." It is a composition:

- an **attack layer** (recorded chiff / speech), spliced phase- and
  spectrum-continuously into
- a **loop-free modeled sustain** (seeded from an offline-analysed
  `SpectralModel`), plus
- a **release layer** (multi-stage, hold-time-aware, with a modeled
  pressure-collapse chiff),

all sharing one spectral seed, with **per-pipe hand-voicing variance** keyed to
a stable `PipeId` on top. Pure-sample and pure-additive voices remain fully
shippable **degenerate configurations of the same pipeline** — the safe
fallback, never abandoned.

### 2. Wind as an audio-rate control signal (the realism moat)

A shared wind model integrates a reservoir/bellows ODE under **real polyphony
demand** and publishes a per-block immutable `WindState` with sample-accurate
pressure taps. Pressure **sags** under load and drives per-partial modulation
(pitch, level, brightness, harmonic development, speech) through **per-tonal-
family response curves** — reeds, strings, and flues breathe differently.
Tremulant is **wind modulation** (coupled AM + FM + timbral), because it
modulates the driver, not the output gain. Neither reference instrument has this.

### 3. Superior dual-mode UI (benchmark: Organteq)

One data-driven `ConsoleLayoutModel` (auto-laid-out from the loaded organ), one
lock-free `StateMirror` (audio-thread truth, polled at frame rate), and
**pluggable Skins** — a photoreal wood skin (vector geometry + texture brushes +
procedural shading, crisp at 4K) and a clean flat vector skin. Both console
modes are Skins over **one model and one live-state feed**, so live feedback,
MIDI-learn, zoom/pan, theming, and accessibility are built once. Animated
reservoir gauges make the wind physics **visible**, sagging under polyphony.

### 4. Semantic registration + control API

Stops carry an **exact-rational** `Footage` plus a rich taxonomy
(`TonalFamily`, `ChorusRole`, `PitchClass`). A first-class **Selector algebra**
(`family:reed & pitch:8 & div:swell`) resolves lazily against the live state; a
terminating, off-thread rules engine derives state, records **why** each stop is
on (`explain()`), supports **branching undo/redo**, and — the marquee win —
**ports** a semantic registration intent across different organ definitions with
a confidence score. One `SelectorParser` grammar is shared verbatim by OSC,
JSON-RPC, MIDI-learn, and the UI omnibar, so the whole engine is programmable
with zero API drift.

---

## Repository layout

```
Caecilia/
├─ CMakeLists.txt            Top-level build (core lib + optional JUCE plugin + tests)
├─ LICENSE                   Proprietary, All Rights Reserved
├─ THIRD_PARTY_NOTICES.md    JUCE / Catch2 — their own separate licenses
├─ CONTRIBUTING.md           Conventional Commits, branch/PR flow, formatting
├─ docs/
│  └─ ARCHITECTURE.md        Master architecture: layering, voice/wind/registration, data flow
├─ src/caecilia/
│  ├─ core/                  Pure, JUCE-free contract: enums, Footage, IDs, IVoice, IWindSupply, ...
│  ├─ engine/               (planned) RT scheduler + voice pool + AudioEngine seam
│  ├─ synthesis/            (planned) layered voices, sample/additive/waveguide/modal engines
│  ├─ wind/                 (planned) reservoir ODE, WindState, tremulant
│  ├─ model/                (planned) OrganSpec, per-pipe spatial data, sample descriptors
│  ├─ dsp/                  (planned) FDN + convolution reverb, sinc interpolator, filters
│  ├─ tuning/              (planned) historical temperaments, stretch, detune
│  ├─ midi/                 (planned) core-native MIDI routing + MIDI-learn
│  ├─ registration/         (planned) Selector algebra, rules engine, undo/redo, port()
│  ├─ control/              (planned) OSC + JSON-RPC over one command sink
│  ├─ plugin/               (planned, JUCE) VST3/AU/Standalone AudioProcessor
│  └─ ui/                   (planned, JUCE) dual-mode console, skins, wind gauges
└─ tests/                    (planned) headless Catch2 unit tests linking caecilia_core
```

### Strict two-tier layering

- **`caecilia_core`** is a pure, **JUCE-free**, allocation/lock/exception-free
  **static library** holding *all* DSP / engine / synth / wind / model / tuning
  / midi / registration / control logic. It links only the C++ standard library
  and is fully unit-testable headless.
- Only the **`plugin`** and **`ui`** modules may include JUCE. They mutate engine
  state **only** through SPSC command rings and read truth **only** through
  lock-free snapshots. A no-JUCE lint guards the core target.

---

## Building

Requirements: **CMake >= 3.22**, a **C++20** compiler (MSVC 2022, Clang 15+, or
GCC 12+). JUCE 8 and Catch2 are pulled automatically via `FetchContent` **only
when needed**.

```bash
# Full build: pure core + JUCE plugin + tests
cmake -S . -B build
cmake --build build

# Pure-core only (no JUCE fetched at all) — ideal for headless CI of the DSP
cmake -S . -B build-core -DCAECILIA_CORE_ONLY=ON
cmake --build build-core

# Tests
ctest --test-dir build --output-on-failure
```

### Build options

| Option                  | Default | Effect                                                         |
|-------------------------|---------|----------------------------------------------------------------|
| `CAECILIA_BUILD_PLUGIN` | `ON`    | Build the JUCE VST3/AU/Standalone `Caecilia` target            |
| `CAECILIA_BUILD_TESTS`  | `ON`    | Build headless Catch2 unit tests                               |
| `CAECILIA_CORE_ONLY`    | `OFF`   | Build only `caecilia_core`; **no JUCE fetched**, plugin forced off |

---

## License

**PROPRIETARY / CLOSED-SOURCE. All Rights Reserved.**
Copyright (c) 2026 Alesson Queiroz.

Caecilia is proprietary and confidential; no permission is granted to use, copy,
modify, distribute, sublicense, or sell any part except by the copyright owner.
See [LICENSE](LICENSE).

JUCE is a **third-party dependency under its own separate license**, documented
in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and never conflated with
Caecilia's license. All Caecilia DSP is implemented independently from public
literature and contains **no GPL code**.
