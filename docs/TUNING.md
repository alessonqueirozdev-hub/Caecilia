<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia — Tuning Design

Historical temperaments and micro-tuning, computed **off the audio thread** into
per-pipe sounding frequencies the voices read. This document specifies the
temperament model, absolute pitch anchoring, octave-stretch, deterministic
per-pipe detune scatter, and how exact-rational `Footage` keeps mutations on their
true harmonic.

> Scope: implements the core contract `core::ITuning`; complements
> [`SYNTHESIS.md`](SYNTHESIS.md) (`PerPipeVoicer` shares the same `PipeId` seed as
> the detune scatter) and [`REGISTRATION.md`](REGISTRATION.md) (the exact `Footage`
> reasoning).

---

## 1. The contract (`core::ITuning`)

The tuning module answers one question — *what frequency does this pipe sound?* —
and answers it as a cheap, RT-safe lookup:

```cpp
TemperamentId temperament() const noexcept;
double referenceA4Hz() const noexcept;
double frequencyForNote(MidiNote note) const noexcept;              // note as an 8' unison
double frequencyForPipe(PipeId pipe, Footage footage) const noexcept; // scaled EXACTLY by Footage
```

Everything is precomputed off the audio thread (in `prepare()` and on
temperament-change commands delivered over the ring); the query methods never
mutate live state and are safe to call while rendering.

---

## 2. Four composable layers (`TuningModel`)

`TuningModel` (the concrete `ITuning`) composes four independent layers into one
query surface:

```
  1. Temperament    interval character (12 cent deviations from equal)
  2. reference A4   absolute anchoring (440, 415, ...)          ─┐ baked once into a
  3. StretchTuning  per-key Railsback octave stretch            ─┤ 128-entry unison table
  ────────────────────────────────────────────────────────────  ┘ (unisonHz_) by rebuild()
  4. DetuneCurve    deterministic per-pipe scatter (PipeId hash) — evaluated on the fly
```

Layers 1–3 bake once into `unisonHz_[128]` via `configure(...)` / `rebuild()`
(NOT RT-safe). Layer 4 is a pure per-pipe hash. So `frequencyForNote` is an array
lookup and `frequencyForPipe` is that lookup × footage ratio × a cheap detune —
both `noexcept`, allocation-free, lock-free.

```cpp
model.configure(TemperamentLibrary::get(TemperamentId::Werckmeister3),
                /*A4*/ 415.0,
                StretchTuning{ .bassCentsPerOctave = 2.0, .trebleCentsPerOctave = 3.0 },
                DetuneCurve{ .amplitudeCents = 1.5f });
```

The `set*` mutators (`setTemperament`, `setReferenceA4Hz`, `setStretch`,
`setDetune`) rebuild off-thread; the synthesis layer reads a stable `TuningModel`
in `prepare()` and on change commands, never mid-render.

---

## 3. Temperaments (`Temperament` + `TemperamentLibrary`)

A temperament is fully described, up to a global transposition, by the **twelve
cent deviations** of its scale degrees from equal temperament, referenced to
C (pitch class 0), octave-periodic:

```cpp
struct Temperament {
  TemperamentId id;
  std::array<double, 12> centsFromEqual;   // index 0 = C ... 9 = A ... 11 = B
  const char* name;
  double centsForPitchClass(int pc) const noexcept;   // reduced mod 12, RT-safe/constexpr
  static constexpr Temperament custom(const std::array<double,12>&, const char* name);
};
```

`TemperamentLibrary` supplies the built-in cent tables
(`kBuiltInCount = 6`), selectable via `core::TemperamentId`:

| `TemperamentId`   | Character                                  |
|-------------------|--------------------------------------------|
| `Equal`           | 12-tone equal temperament                  |
| `QuarterMeantone` | quarter-comma meantone                     |
| `Werckmeister3`   | Werckmeister III (well temperament)        |
| `Kirnberger3`     | Kirnberger III                             |
| `Pythagorean`     | Pythagorean                                |
| `Young`           | Thomas Young (1799)                        |
| `Custom`          | *not* a built-in — supplied as data        |

`Custom` is intentionally **data-driven, not fabricated in code**: any well
temperament, meantone variant or measured historical tuning is expressed as a
twelve-entry cent table through `Temperament::custom(table, name)` without
touching engine code (`isBuiltIn`, `displayName`, `builtInIds`).

---

## 4. Absolute anchoring (`TuningTable`)

The temperament is transposition-free *character*; `TuningTable::build` pins it to
an absolute pitch by anchoring A4 to `referenceA4Hz` (e.g. 440 Hz modern, 415 Hz
baroque, 466 Hz chorton), producing the 128-entry unison Hz table the model reads.

---

## 5. Octave stretch (`StretchTuning`)

A Railsback-style curve bends the compass extremes outward — bass slightly flat,
treble slightly sharp — so octaves beat with the partials the ear expects rather
than with theoretical equal-tempered ratios. It is a piecewise-linear function of
distance in octaves from a pivot (default A4 = 69):

```cpp
struct StretchTuning {
  double bassCentsPerOctave   = 0.0;   // positive → flatter bass
  double trebleCentsPerOctave = 0.0;   // positive → sharper treble
  MidiNote pivotNote          = 69;    // zero-deviation pivot
  static constexpr StretchTuning none() noexcept;      // identity (no stretch)
  double centsForNote(MidiNote note) const noexcept;   // RT-safe/constexpr
};
```

The default (both slopes 0) is a no-op, so an unconfigured curve leaves the
temperament untouched.

---

## 6. Deterministic per-pipe detune (`DetuneCurve`)

Real pipes are never in perfect tune with one another; that tiny, fixed
mistuning is much of why a rank sounds like many pipes, not one cloned
oscillator. `DetuneCurve` derives a small, **repeatable** cent offset from the
pipe's stable `PipeId`:

```cpp
struct DetuneCurve {
  float amplitudeCents = 0.0f;         // peak scatter (0 disables → perfectly tuned rank)
  uint32_t seed = 0x9E3779B9u;         // salt so ranks/instruments scatter independently
  static constexpr DetuneCurve none() noexcept;
  double centsForPipe(PipeId pipe) const noexcept;   // splitmix32-style hash, RT-safe
};
```

The same pipe always drifts the same way across runs and sessions. Crucially, it
shares the **same `PipeId` seed** as the synthesis-side `PerPipeVoicer` (see
[`SYNTHESIS.md`](SYNTHESIS.md) §5), so tuning scatter and voicing scatter are
coherent — together they keep unisons and mixtures **shimmering** instead of
phase-locking.

---

## 7. Exact `Footage` scaling — why mutations stay true

`frequencyForPipe(pipe, footage)` scales the note's unison frequency by the
rank's **exact-rational** `core::Footage`, so a mutation never drifts off its
true harmonic:

| Footage      | `num/den` | Sounds                    |
|--------------|-----------|---------------------------|
| 8' unison    | 8/1       | at the key's unison pitch |
| 4' octave    | 4/1       | one octave above          |
| 16' sub      | 16/1      | one octave below          |
| 2 2/3' quint | 8/3       | a perfect twelfth above   |
| 1 3/5' tierce| 8/5       | a seventeenth above       |

Because the ratio is exact (not a rounded float), a 2 2/3' Nazard sits on the true
3rd harmonic and a 1 3/5' Tierce on the true 5th — essential for a coherent
Cornet or Sesquialtera, and the same exactness that lets build-plenum keep
mutations out of a diapason chorus (see [`REGISTRATION.md`](REGISTRATION.md)).

---

## 8. Change flow (temperament switch while playing)

```
  UI / control → temperament-change command → SPSC ring → engine
        │ (OFF audio thread) TuningModel::setTemperament(...) → rebuild() 128-entry table
        ▼
  synthesis reads the stable TuningModel on the next block; no live-table mutation,
  no click (pitch change is applied at note boundaries / via smoothed pipe frequency)
```

Temperament and A-reference are **never mutated mid-render**; the rebuild happens
off-thread and the new table is swapped in at a block boundary.

---

## 9. Real-time safety summary

| Concern      | Guarantee                                                                   |
|--------------|-----------------------------------------------------------------------------|
| Allocation   | Only in `configure(...)` / `rebuild()` (off the audio thread)               |
| Queries      | `frequencyForNote` / `frequencyForPipe` are `noexcept` array-lookup + hash  |
| Determinism  | `DetuneCurve` keyed by `PipeId` → repeatable across runs/sessions           |
| Correctness  | Exact-rational `Footage` scaling → mutations land on their true harmonic     |
| Live changes | Temperament/A4 rebuilt off-thread, swapped at a block boundary              |

---

## 10. Roadmap touchpoints

- **v0.9 — tuning module.** Historical temperaments, stretch and per-pipe detune,
  delivered alongside the UI polish phase so the console can expose temperament
  and A-reference selection.
