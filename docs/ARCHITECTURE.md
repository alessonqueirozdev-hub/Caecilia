<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Ceciliae — Architecture

This is the master architecture document: the layering model, module
responsibilities, the four load-bearing design decisions (hybrid voice, wind,
RT contract, semantic registration), the end-to-end data flow, and the
real-time safety rules every module must obey.

> Status: early scaffold. The contract types in `src/ceciliae/core` are real;
> the downstream modules described here are planned per the roadmap.

---

## 1. The two-tier layering

Ceciliae is split into a **pure core** and a thin **JUCE shell**.

```
                        ┌───────────────────────────────────────────┐
   JUCE SHELL           │  plugin  (VST3/AU/Standalone AudioProcessor)│
   (may include JUCE)   │  ui      (dual-mode console, skins, gauges) │
                        └───────────────┬───────────────────────────┘
                                        │  SPSC command rings (write)
                                        │  lock-free snapshots (read)
                                        ▼
   ┌───────────────────────────────────────────────────────────────────┐
   │  ceciliae_core  (pure STATIC lib — links ONLY the std library)     │
   │                                                                     │
   │  core ─ shared vocabulary + RT contract (this is the seam)         │
   │   ├─ engine        RT scheduler, voice pool, AudioEngine seam      │
   │   ├─ synthesis     layered voices + sample/additive/wg/modal       │
   │   ├─ wind          reservoir ODE → immutable WindState             │
   │   ├─ model         OrganSpec, per-pipe spatial, sample descriptors │
   │   ├─ dsp           FDN + convolution reverb, sinc interp, filters  │
   │   ├─ tuning        historical temperaments, stretch, detune        │
   │   ├─ midi          core-native MIDI routing + MIDI-learn           │
   │   ├─ registration  Selector algebra, rules engine, undo/redo, port │
   │   └─ control       OSC + JSON-RPC over one command sink            │
   └───────────────────────────────────────────────────────────────────┘
```

**Rules of the layering**

- `core` depends on **nothing** but the C++ standard library and must not depend
  on any other Ceciliae module. Every other module depends on `core`.
- Only `plugin` and `ui` may include JUCE. A no-JUCE lint guards the core target
  so no `juce::File/String/ValueTree/AudioBuffer` leaks in.
- The JUCE shell **never** mutates engine state directly. It writes to
  single-producer/single-consumer (SPSC) command rings and reads truth from
  lock-free snapshots (`WindState`, `MeterSnapshot`, generation-tagged handles).

**Namespaces.** Each module maps to a single sibling sub-namespace under
`ceciliae` (`dsp` → `ceciliae::dsp`, `wind` → `ceciliae::wind`, …). The one
recorded exception is **`engine`**, which nests under `core` as
`ceciliae::core::engine`: it is the real-time seam of the pure core library, not
a peer domain module. The nesting is intentional and does **not** imply that
`core` depends on `engine` (the dependency runs engine → core).

---

## 2. The core contract (`src/ceciliae/core`)

`core` defines the shared vocabulary and the RT-contract types every module
compiles against. Present today:

| Header            | Provides                                                                        |
|-------------------|---------------------------------------------------------------------------------|
| `EngineTypes.h`   | `SampleRate`, `MidiNote`, `Velocity`; `PipeId/RankId/StopId/DivisionId`; `TonalFamily`, `ChorusRole`, `PitchClass`, `EngineKind`, `VoiceTier`; exact-rational `Footage` + named-rank constants |
| `AudioBlock.h`    | Non-owning, JUCE-free multi-channel float view with sample offset + `subBlock`   |
| `IVoice.h`        | Voice lifecycle: `prepare / noteOn / noteOff / renderAdd / isActive / kind / tier` |
| `IWindSupply.h`   | Wind query surface: `pressureAt`, `pressureDeviation`, `registerDemand`           |
| `ITuning.h`       | `frequencyForNote`, `frequencyForPipe(PipeId, Footage)`, temperament selection    |
| `IReverb.h`       | `prepare / setParams / process / reset / latencySamples` for both reverbs         |

`Footage` is an **exact reduced rational** (8' = 8/1, 2 2/3' = 8/3, 1 3/5' =
8/5) with `feet()`, `isMutation()`, and `octaveClassFrom8()`. Keeping it exact
is a **correctness requirement**: pitch-based selectors and build-plenum must
never pollute a principal chorus with quints or tierces.

---

## 3. The hybrid voice model

Realism lives in two places a monolithic one-engine-per-rank model cannot
express:

- **Intra-note**: a recorded chiff/speech attack spliced — phase- and
  spectrum-continuously — into a **loop-free modeled sustain**.
- **Per-pipe**: deterministic hand-voicing variance keyed to a stable `PipeId`.

So an `IVoice` is a **composition** of layers:

```
   AttackLayer  ──▶  SustainLayer  ──▶  ReleaseLayer
   (ISampleSource)   (IModeledSustain)  (multi-stage, pressure-collapse chiff)
        │                 │
        └── share one SpectralModel seed ──┘
                          │
                  VoicingProfile (per PipeId)
```

Two synthesis **tiers** share the same voicing params and wind port:

- an expensive **nonlinear source–resonator** model (NonlinearJet + Waveguide
  for flues; BeatingReed + ModalResonator for reeds) for exposed/solo stops;
- a CPU-cheap **wind-modulated partial bank** (additive/modal) for plenum
  polyphony.

Under CPU pressure the engine **demotes** a voice's tier with a phase-aligned,
inaudible crossfade. Chiff and speech **emerge** from jet startup and staggered
per-partial onsets — never a canned identical transient.

Pure-sample and pure-additive voices are simply **degenerate configurations** of
this pipeline — the safe, always-shippable fallback.

---

## 4. The wind model (`wind`)

Wind is an **audio-rate control signal, not an LFO** — the single realism moat
both references lack.

- `WindModel::step()` integrates a reservoir/bellows pressure ODE **once per
  block** (no alloc, no lock) under the summed instantaneous `WindDemand`
  published by sounding voices, producing **pressure sag** under polyphony and
  per-windchest distribution.
- Output is an **immutable per-block `WindState`** snapshot with sample-accurate
  pressure taps (`pressureAt`). Because it is computed once then read-only, every
  voice reads it **race-free** by construction.
- Pressure feeds per-partial modulation (pitch, level, brightness, harmonic
  development, speech) via per-`TonalFamily` `WindResponseCurve` — reeds,
  strings, and flues respond on **different curves**.
- **Tremulant** is modeled as modulation injected into the pressure signal,
  yielding coupled **AM + FM + timbral** effects downstream — not an output-gain
  wobble.

---

## 5. Registration intelligence (`registration`)

Three layers the baseline conflated:

1. **Specification** — immutable `StopSpec` metadata in `OrganSpec`.
2. **State** — `RegistrationState`: engaged `StopSet` + couplers (couplers are
   first-class in resolution).
3. **Intent** — a first-class **Selector algebra**
   (`family` / `pitch` / `role` / `division` / `named` / `engaged` / `where`,
   with `&  |  -  !`) resolving **lazily** against `OrganSpec` + live state.

A terminating, deterministic, RT-budgeted **rules engine** (NOT full Prolog)
applies `RegistrationCommand`s **off the audio thread**, records provenance so
`explain(StopId)` answers "why is this on", supports **command-sourced branching
undo/redo** (state = fold of the command log), domain-intelligent **build-plenum**
(principal-family, unison-pitched 16-8-4-2 + mixtures, mutations excluded by
default), pistons capturing either a literal snapshot or a portable intent, and
crescendo/Walze as an ordered Selector sequence.

The marquee deliverable is **`port()`**: a semantic `RegistrationIntent`
transfers across different `OrganSpec`s with a **confidence score** and graceful
degradation. Every `apply()` emits an atomic `StateDelta` pushed to the engine
over the ring for a **click-free, wind-modeled stop crossfade**.

One `SelectorParser` grammar is the **single source of truth**, shared verbatim
by OSC, JSON-RPC, MIDI-learn, and the UI omnibar.

---

## 6. The control API (`control`)

Thin transport codecs over **one** registration command bus:

- an **OSC** adapter and a **JSON-RPC** adapter, both parsing the **same**
  `SelectorParser` grammar and submitting through the **same** `ICommandSink`;
- one `IStatePublisher` fans state changes out to all observers (UI, OSC
  feedback, MIDI LEDs).

The adapters hold **no registration logic** — they only encode/decode. The
selector grammar is a **versioned public contract** (explicit version field from
day one), because external scripts depend on it.

---

## 7. End-to-end data flow

```
  MIDI bytes (host)
        │  plugin: juce::MidiBuffer → core-native MidiEvent
        ▼
  midi: MidiRouter
        │  NoteOn/NoteOff            RegistrationCommand (learned CC/PC, si5/do6 nav)
        │        │                            │
        │        │                            ▼
        │        │                 registration: rules engine (OFF audio thread)
        │        │                            │  atomic StateDelta
        ▼        ▼                            ▼
  ─────────────── SPSC command ring ────────────────────────
                          │
                          ▼
  engine: AudioEngine::processBlock  (ON audio thread)
        1. drain command ring (note on/off, StateDelta, tremulant, temperament)
        2. wind: WindModel::step() → immutable WindState for this block
        3. render active voices  → per-windchest accumulation buses
             synthesis voices read: WindState (wind), ITuning (pitch),
             VoicingProfile (per-pipe variance)
        4. dsp: master chain (spatialization, FDN/convolution reverb)
        5. publish MeterSnapshot to the UI double-buffer
                          │
                          ▼
              host output buffers  →  speakers
                          │
        ui: StateMirror polls MeterSnapshot at frame rate (audio thread
            NEVER touches a Component); skins paint model + mirror.
```

---

## 8. Real-time safety rules (mandatory)

Documented on every hot function; enforced by a CI allocator/lock trap.

- **No** heap allocation, locks, logging, or exceptions on any
  `process()` / `render()` / `step()` / apply-on-audio path.
- **All** allocation and precompute happens in `prepare()` / `setup()` /
  `configure()`, off the audio thread.
- Hot voice/engine functions are marked `noexcept`.
- Cross-thread **mutation** of engine state happens **exclusively** via SPSC
  lock-free command rings (single writer, single reader).
- Cross-thread **reads** of engine truth happen **exclusively** via lock-free
  snapshots: immutable per-block `WindState`, double-buffered `MeterSnapshot`,
  generation-tagged handles (`VoiceHandle`) that safely reject stale references
  after a steal.
- Registration algebra, rules evaluation, undo/redo, and sample loading **never**
  run on the audio thread — they produce an atomic `StateDelta` handed over the
  ring and applied as a click-free, wind-modeled crossfade.
- A per-block **`DeadlineBudget`** sheds additive partials and steals the
  quietest/oldest voices before an xrun, so worst-case tutti degrades to subtle
  thinning, never a dropout.
- Denormals are handled by per-audio-thread FTZ/DAZ plus explicit flushing in
  feedback paths (FDN, wind).

---

## 9. Roadmap (abridged)

`v0` scaffold & contract · `v0.1` audible sine ranks end-to-end · `v0.2` wind +
tremulant · `v0.3` DSP core (sinc, filters, FDN) · `v0.4` sample layer +
per-pipe voicing · `v0.5` hybrid attack-splice + modeled sustain · `v0.6`
nonlinear physical tier + tiering · `v0.7` registration brain · `v0.8` control
API + MIDI-learn + `port()` · `v0.9` UI polish + second skin + tuning · `v0.95`
convolution reverb + hardening · `v1` release.
