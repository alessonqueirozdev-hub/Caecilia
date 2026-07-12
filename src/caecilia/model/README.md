<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# `caecilia::model` — the instrument definition

The `model` module is Caecilia's **instrument description with first-class
semantic metadata**. It answers "what is this organ?" — its ranks, stops,
divisions, couplers, windchests and per-pipe physical placement — and compiles a
human-readable organ file into the single immutable object the whole engine
reads: the **`Organ`** (aliased `OrganSpec`).

It is part of the pure `caecilia_core` static library: it depends only on
`core` and the C++ standard library, includes no JUCE, and performs **no OS or
filesystem I/O**. Loading and compiling happen off the audio thread; the
compiled `Organ` is thereafter read-only, so its query methods are RT-safe.

---

## Two layers

```
   organ file text  ──parse──▶  OrganDefinition  ──compile──▶  Organ (== OrganSpec)
   (JSON / YAML)                (mutable mirror)               (immutable, id-indexed)
        owned by the shell      names, human-edited            engine/registration/UI read this
```

1. **Definition layer** (`OrganDefinition.h`) — a mutable, 1:1 mirror of the
   on-disk document (`WindchestDef`, `RankDef`, `StopDef`, `DivisionDef`,
   `CouplerDef`, `VoicingDef`, `SampleSetDef`). Cross-references are **by name**
   so the file stays human-editable. `validate()` does cheap structural checks.

2. **Compiled spec layer** — the immutable domain model. Every element's id
   equals its index in the `Organ`'s arrays, so id lookups are O(1) and
   allocation-free.

`OrganLoader` bridges them: `parse()` (text → definition), `compile()`
(definition → organ, resolving names to dense ids and materialising pipes), and
`load()` (both). The shell reads the file bytes and hands the **text** to the
loader, so `caecilia_core` never touches the filesystem.

---

## Type map

| Type | Role | Spec alias |
|------|------|------------|
| `Pipe` | One physical pipe: frequency (footage-scaled nominal + `frequencyUnder(ITuning)`) and per-pipe voicing seed/overrides | — |
| `Rank` | Homogeneous pipe set: `EngineKind`, exact `Footage`, `TonalFamily`, voicing, samples, compass; owns its `Pipe`s | `RankSpec` |
| `Stop` | Drawstop semantic identity: `TonalFamily` + `Footage` + `PitchClass` + `ChorusRole` + its `DivisionId` + the `RankId` it controls | `StopSpec` |
| `Division` | Stops under one keyboard section; enclosure + tremulant + compass | `DivisionSpec` |
| `Manual` | Physical keyboard binding (MIDI channel, stacking order) for a manual division | — |
| `Coupler` | Routes keys between divisions with an octave shift; first-class in registration | `CouplerSpec` |
| `Windchest` | Wind-supply grouping; nominal pressure + tremulant flag | `WindchestSpec` |
| `Organ` | The compiled, immutable instrument; id-indexed arrays + activation mapping | `OrganSpec` |
| `PipeSpatial` | Per-pipe placement feeding dsp early reflections | — |
| `RankVoicingSpec` | Voicing character + per-pipe scatter amounts (source for `synthesis::PerPipeVoicer`) | — |
| `SampleSetDescriptor` | Caecilia's **own** proprietary sample-format descriptor (never a GPL'd GO/HW set) | — |
| `OrganDefinition` | Mutable document mirror | — |
| `OrganLoader` | Parse / compile / load / serialize | — |

The `*Spec` aliases exist so the registration, wind and UI modules can speak in
"spec" terms while sharing the exact same immutable types — there is one
definition of each, no parallel hierarchy.

---

## The Rank → PipeId activation mapping

`Organ` exposes the concrete mapping a keypress resolves to:

- `Organ::pipeForKey(StopId, MidiNote)` — the pipe a single drawn stop sounds.
- `Organ::collectPipesForKey(DivisionId, MidiNote, engaged, out)` — the full set
  of pipes a key sounds given the engaged stops, written into a **caller-owned
  buffer** (allocation-free, `noexcept`) so it can run on/near the audio thread.

Couplers are **pre-expanded by the registration engine** into the `engaged` set
before this is queried; `model` resolves only the direct division.

---

## Organ file schema (Caecilia's own format)

A minimal example (JSON; YAML is the same shape). References are by name.

```jsonc
{
  "name": "St. Cecilia — House Organ",
  "builder": "Caecilia",
  "year": 2026,

  "windchests": [
    { "name": "Great",  "pressurePa": 812, "tremulant": false },
    { "name": "Swell",  "pressurePa": 762, "tremulant": true  }
  ],

  "ranks": [
    { "name": "Principal 8",  "family": "Principal", "engine": "Waveguide",
      "footageNum": 8, "footageDen": 1, "windchest": "Great",
      "lowNote": 36, "highNote": 96,
      "voicing": { "chiff": 0.15, "brightness": 0.6, "detuneScatterCents": 1.5 } },

    { "name": "Trumpet 8",    "family": "Reed",      "engine": "Modal",
      "footageNum": 8, "footageDen": 1, "windchest": "Swell" }
  ],

  "divisions": [
    { "name": "Great", "kind": "Manual", "lowNote": 36, "highNote": 96,
      "manualIndex": 0, "midiChannel": 0 },
    { "name": "Swell", "kind": "Manual", "enclosed": true, "tremulant": true,
      "manualIndex": 1, "midiChannel": 1 }
  ],

  "stops": [
    { "name": "Principal", "family": "Principal", "footageNum": 8, "footageDen": 1,
      "pitchClass": "Unison", "role": "Foundation",
      "division": "Great", "rank": "Principal 8" },

    { "name": "Trumpet",   "family": "Reed", "footageNum": 8, "footageDen": 1,
      "pitchClass": "Unison", "role": "Solo",
      "division": "Swell", "rank": "Trumpet 8" }
  ],

  "couplers": [
    { "name": "Swell to Great", "from": "Swell", "to": "Great",
      "octaveShift": 0, "kind": "InterManual" }
  ]
}
```

Enum-valued fields accept the canonical spellings in `EngineTypes.h`
(`TonalFamily`, `ChorusRole`, `PitchClass`, `EngineKind`) plus `DivisionKind` /
`CouplerKind`; parsing is case-insensitive and an unknown token becomes a
diagnostic, not a silent default. `Footage` is stored as an exact
`num`/`den` fraction (`8'` = 8/1, `2 2/3'` = 8/3, `1 3/5'` = 8/5).

> Caecilia never ingests a GPL'd GrandOrgue / Hauptwerk organ definition or
> sample set. This schema, the offline analysis data and the `SpectralModel` it
> produces are owned IP.

---

## Real-time safety

- **Off the audio thread** (may allocate): `OrganLoader::parse/compile/load`,
  `OrganDefinition::validate`, all `set*` builders, `Rank::generatePipes`,
  `Stop::displayName`, the enum ↔ string helpers.
- **RT-safe** (`noexcept`, allocation-free): every read accessor on the compiled
  spec — `Organ::rank/stop/division/coupler/windchest`, `Organ::pipeForKey`,
  `Organ::collectPipesForKey`, `Rank::pipeFor/contains`, `Pipe::frequencyUnder`,
  `Coupler::mapNote`, `Division::contains`.

---

## Status

Scaffold (roadmap `v0`). `compile()`, structural validation, the pipe-activation
mapping and the semantic taxonomy are real. The concrete JSON/YAML reader/writer
(`parse` / `serialize`) is stubbed — it reports "not yet implemented" — pending
the proprietary document reader; `compile()` already works on hand-built
`OrganDefinition`s so the rest of the engine can be developed against real specs.
