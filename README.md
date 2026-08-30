<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia

**An open-source hybrid pipe-organ virtual instrument (VST3 / AU / Standalone).**

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

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

*Where this stands:* only `StateMirror` was built. The console that ships is a
single embedded web page (`docs/mockups/console.html`) in a
`WebBrowserComponent`; the layout model and the two skins were written, never
instantiated, and removed (see [`src/caecilia/ui/README.md`](src/caecilia/ui/README.md)).
Its reservoir gauges are animated in JavaScript from the live voice count,
because the engine does not step the wind model yet.

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
├─ LICENSE                   Apache License 2.0
├─ NOTICE                    Attribution + build-dependency licences
├─ THIRD_PARTY_NOTICES.md    JUCE / Catch2 — their own separate licenses
├─ CONTRIBUTING.md           Conventional Commits, branch/PR flow, formatting
├─ docs/
│  ├─ ARCHITECTURE.md        Master architecture: layering, voice/wind/registration, data flow
│  ├─ ORGAN_FILE.md          The organ-file format: every key, every default, the limits
│  └─ …                      DSP_DESIGN, SYNTHESIS, WIND_MODEL, TUNING, REGISTRATION, UI_DESIGN
├─ examples/
│  └─ caecilia-demo.organ.json   The instrument's own organ, to copy and edit
├─ src/caecilia/
│  ├─ core/                  Pure, JUCE-free contract: enums, Footage, IDs, IVoice, IWindSupply, ...
│  ├─ engine/                RT scheduler, voice pool, command ring, meters
│  ├─ synthesis/             additive voice + partial bank (the other engines are unused)
│  ├─ wind/                  reservoir ODE, WindState, tremulant (per chest, wired)
│  ├─ model/                 Organ/Stop/Rank, the demo organ, registration composites
│  ├─ dsp/                   FDN reverb, master EQ, limiter, sinc interpolator, filters
│  ├─ tuning/                historical temperaments, stretch, detune
│  ├─ midi/                  MIDI-learn + learned controls (wired); MidiRouter is not
│  ├─ registration/          StopSet + factory generals (wired); selector algebra, rules
│  │                        engine and undo/redo are built and tested but unreached
│  ├─ control/               OSC + JSON-RPC over one command sink (not yet wired)
│  ├─ plugin/                (JUCE) VST3/AU/Standalone AudioProcessor + bridges
│  └─ ui/                    (JUCE) StateMirror; the console is a WebView page
└─ tests/                    headless Catch2 unit tests linking caecilia_core
```

### Strict two-tier layering

- **`caecilia_core`** is a pure, **JUCE-free**, allocation/lock/exception-free
  **static library** holding *all* DSP / engine / synth / wind / model / tuning
  / midi / registration / control logic. It links only the C++ standard library
  and is fully unit-testable headless.
- Only the **`plugin`** and **`ui`** modules may include JUCE. Note events and
  automatable parameters reach the engine **only** through the SPSC command ring,
  and truth comes back **only** through lock-free snapshots. The console's own
  edits are the standing exception: registration, reverb space and master EQ are
  applied on the message thread under the processor's callback lock. A no-JUCE
  lint guards the core target.

---

## Playing your own organ

The instrument compiled into the binary is *an* organ, not *the* organ. The
console's ⌂ button opens an organ file; ↺ goes back to the built-in one. A project
remembers which file it was written on — the path, not a copy, because the file is
yours and you may still be editing it.

Start from the instrument's own organ, which ships as a real three-manual
specification:

```bash
cp examples/caecilia-demo.organ.json my-organ.organ.json
```

Every key, every default and every limit is in
[`docs/ORGAN_FILE.md`](docs/ORGAN_FILE.md). To check a file before loading it —
same diagnostics the plugin reports, with line and column:

```bash
caecilia-organ-file --check my-organ.organ.json
```

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

Caecilia is licensed under the **Apache License, Version 2.0**.
See [LICENSE](LICENSE) and [NOTICE](NOTICE).

    SPDX-License-Identifier: Apache-2.0
    Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

Apache-2.0 was chosen over MIT/BSD for its **explicit patent grant** — audio DSP
is a field where third-party patents exist, and contributors and users both
benefit from the clarity.

### JUCE: Caecilia qualifies for the free route

[JUCE](https://juce.com/get-juce/) is dual-licensed: **AGPLv3 (free)** for open-source
projects that ship under the same free terms, or a **paid commercial licence** for
closed-source products. Caecilia is open source, so it takes the free route — no JUCE
licence needs to be bought, by us or by you.

Concretely:

| What | Licence |
|------|---------|
| Caecilia's source code | **Apache-2.0** |
| `caecilia_core` built alone (`-DCAECILIA_CORE_ONLY=ON`, no JUCE) | **Apache-2.0** |
| A compiled VST3 / AU / Standalone binary (links JUCE) | **AGPLv3** |

Apache-2.0 is one-way compatible with AGPLv3, which is exactly why this works: our
code stays permissive, and the combined binary — ours plus JUCE — is distributed
under AGPLv3, as JUCE's free licence requires. In practice that means shipping the
binary alongside its source, which we do anyway.

You only need to buy a commercial JUCE licence if you want to ship a **closed-source**
product built on Caecilia. Building it for yourself, modifying it, forking it, and
redistributing it as open source are all free.

**Microsoft WebView2** (Windows only, used by the embedded console) ships under
Microsoft's own SDK terms.

Full details in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Provenance of the DSP

Every DSP algorithm here is written from public literature and standard
mathematics — RBJ biquad cookbook, Jot-style unitary FDN, Dattorro input
diffusion, Kaiser-windowed sinc interpolation. **No GPL DSP source (GrandOrgue,
Aeolus, or otherwise) was referenced or copied.** If you believe any part
infringes, please open an issue and it will be addressed.

---

## Contributing

Contributions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md) for the
commit convention, branch flow, the strict core/JUCE layering rule, and the
real-time-safety constraints that apply to any code on an audio path.

Everyone participating is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

By contributing, you agree that your contributions are licensed under
Apache-2.0, per section 5 of the licence.
