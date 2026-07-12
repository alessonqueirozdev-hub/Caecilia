<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# caecilia::tuning

Historical temperaments and micro-tuning for Caecilia. This module turns a chosen
temperament, a reference pitch, and optional stretch/detune into the **per-pipe
sounding frequencies** the synthesis layer reads. It is part of the pure,
JUCE-free `caecilia_core` static library and is fully unit-testable headless.

It is the concrete implementation of the core contract
[`core::ITuning`](../core/ITuning.h).

## Design in one paragraph

A `TuningModel` composes four layers into one `ITuning` query surface:

1. **`Temperament`** — the interval *character*, expressed as twelve cent
   deviations from equal temperament (C = 0). Octave-periodic and
   transposition-free.
2. **Reference A4** — absolute anchoring. `TuningTable::build` pins MIDI note 69
   to `referenceA4Hz` *exactly* by zeroing the temperament's own A deviation, so
   the `ITuning` promise ("A4 sounds at `referenceA4Hz`") always holds while the
   temperament's shape is preserved.
3. **`StretchTuning`** — a Railsback-style per-key octave stretch (flat bass,
   sharp treble), zero at a pivot note.
4. **`DetuneCurve`** — deterministic per-pipe cent scatter hashed from `PipeId`,
   so a rank sounds like many pipes instead of one cloned oscillator. Seeded from
   the same `PipeId` the synthesis-side PerPipeVoicer uses.

Layers 1–3 are baked once into a 128-entry unison table off the audio thread;
layer 4 is a pure per-pipe hash evaluated on the fly.

## Real-time contract

- **Off the audio thread:** `TuningModel::configure` and the `set*` mutators
  rebuild the table (128 `exp2` evaluations). Call them in `prepare()` or when
  handling a temperament-change command delivered over the engine's SPSC ring —
  never mid-render.
- **On the audio thread:** `frequencyForNote` and `frequencyForPipe` are
  `noexcept`, allocation-free, lock-free lookups that never mutate live state.

The engine hands the synthesis layer a stable `TuningModel` in `prepare()`; a
temperament change is a command that swaps/reconfigures the model off-thread, not
a live mutation of a model being read mid-block.

## Footage-exact sounding pitch

`frequencyForPipe(pipe, footage)` scales the key's unison frequency by the rank's
**exact rational** [`core::Footage`](../core/EngineTypes.h):

```
ratio = 8 / feet = (8 * footage.den) / footage.num
```

so a 4′ rank sounds an octave up (ratio 2), a 2 2⁄3′ quint a twelfth up
(ratio 3), and a 16′ an octave down (ratio 0.5). Because the ratio is exact, a
mutation lands precisely on its true harmonic and never pollutes a principal
chorus.

## Public API

| Type | Header | Role |
| --- | --- | --- |
| `Temperament` | `Temperament.h` | 12-entry cent-deviation table (+ `custom()` for data-driven tables). |
| `TemperamentLibrary` | `TemperamentLibrary.h` | Built-in catalogue: equal, quarter-comma meantone, Werckmeister III, Kirnberger III, Pythagorean, Young. |
| `TuningTable` | `TuningTable.h` | 128-note absolute frequency table built from a temperament + A4. |
| `StretchTuning` | `StretchTuning.h` | Per-key octave-stretch curve (header-only). |
| `DetuneCurve` | `DetuneCurve.h` | Deterministic per-pipe cent scatter from `PipeId` (header-only). |
| `TuningModel` | `TuningModel.h` | Composes the above; implements `core::ITuning`. |

## Usage sketch

```cpp
using namespace caecilia;

tuning::TuningModel tuning;
// Off-thread (prepare / temperament-change handler):
tuning.configure(tuning::TemperamentLibrary::get(core::TemperamentId::Werckmeister3),
                 /*referenceA4Hz=*/415.0,
                 tuning::StretchTuning{ /*bass*/ 2.0, /*treble*/ 3.0, /*pivot*/ 69 },
                 tuning::DetuneCurve{ /*amplitudeCents*/ 1.5f });

// On the audio thread (RT-safe):
const double f8  = tuning.frequencyForPipe({ /*rankId*/ 3, /*note*/ 69 }, core::footage::kEight);
const double f22 = tuning.frequencyForPipe({ /*rankId*/ 4, /*note*/ 69 }, core::footage::kTwoAndTwoThird);
```

## Status

Scaffold (roadmap **v0.9**). Interfaces and composition are final; the built-in
cent tables are the nominal published deviations and are marked for
high-precision recalibration once the tuning validation harness lands
(`TODO(phase9)` in `TemperamentLibrary.cpp`).
