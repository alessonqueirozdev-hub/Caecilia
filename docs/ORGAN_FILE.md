<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# The organ file

Caecilia plays whatever organ it is given. The instrument compiled into the binary
is one organ, not *the* organ: an organ file describes another, and the console's
⌂ button opens it.

This is the reference for that file. Every default below is the one in
`src/caecilia/model/OrganDefinition.h`; every key is the one
`src/caecilia/model/OrganLoader.cpp` reads.

## Getting one to start from

`examples/caecilia-demo.organ.json` is the instrument's own organ, written out
through this format. It is a real three-manual specification — 26 ranks, 26 stops,
3 windchests, 3 couplers — and it is the intended starting point: copy it, edit it,
open it.

The same file is what `caecilia-organ-file --export` prints, so you can always
regenerate it:

```bash
caecilia-organ-file --export > my-organ.organ.json
```

And check one before loading it, which reports the same diagnostics the plugin
would:

```bash
caecilia-organ-file --check my-organ.organ.json
```

## The shape of it

One JSON object, with five collections under it.

```json
{
  "name": "St Aubin, Grand Orgue",
  "builder": "Aristide Cavaillé-Coll",
  "year": 1868,
  "windchests": [ ... ],
  "ranks":      [ ... ],
  "divisions":  [ ... ],
  "stops":      [ ... ],
  "couplers":   [ ... ]
}
```

Order does not matter; nothing is positional. References between collections are
**by name**, so a stop names its division and its rank rather than indexing them.

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | What the console calls this instrument. A missing name is a warning, not an error. |
| `builder` | string | — | Free text. |
| `year` | number | `0` | Free text as a number; `0` means unstated. |

## Windchests

A windchest is a reservoir with a pressure and, optionally, a tremulant. Ranks
stand on chests, and a chest's wind sags under everything drawn on it — which is
why putting the Pédale on its own chest is audible and not bookkeeping.

```json
{ "name": "Récit chest", "pressurePa": 735, "tremulant": true }
```

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | Must be present and unique. Ranks reference it. |
| `pressurePa` | number | `812` | Wind pressure in pascals. 812 Pa ≈ 83 mm water column. |
| `tremulant` | bool | `false` | Whether this chest has a tremulant. |

## Ranks

A rank is a row of pipes: one pipe per note, one timbre, one footage. It is the
thing that makes sound. A stop is the *control* that admits wind to one.

```json
{
  "name": "Montre 8",
  "family": "Principal",
  "footage": 8,
  "windchest": "Grand-Orgue chest",
  "lowNote": 36,
  "highNote": 96,
  "voicing": { "brightness": 0.62 }
}
```

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | Not required to be unique — see **Naming the same rank twice** below. |
| `family` | string | `"Principal"` | `Principal`, `Flute`, `String`, `Reed`, `Mixture`, `Hybrid`, `Percussion`, `Undefined`. |
| `engine` | string | `"Additive"` | `Additive`, `Sample`, `Waveguide`, `Modal`. Only `Additive` sounds today; the others fall back to it. |
| `footage` | number or `[num, den]` | `8` | Feet. `4`, `16`, or `[2, 3]` for a 2⅔. |
| `windchest` | string | — | Name of the chest this rank stands on. Empty means chest 0. |
| `lowNote` | number | `36` | MIDI note of the lowest pipe. |
| `highNote` | number | `96` | MIDI note of the highest pipe. |
| `pan` | number | `0.0` | −1 left, +1 right. |
| `distanceM` | number | `8.0` | Metres from the listener; feeds the spatial model. |
| `spectrum` | string | — | Path to a **measured** spectrum, relative to this document. See below. |
| `voicing` | object | — | See below. Omitted keys keep their defaults. |
| `sampleSet` | object | — | Only meaningful with `"engine": "Sample"`. |

### A measured spectrum

A rank can sound from a **recording of a real pipe** instead of from the
procedural recipe. `caecilia-partial-extractor` turns a steady recording into a
partial bank:

```bash
caecilia-partial-extractor --input montre-c4.wav --output spectra/montre-8.json
```

and the rank names it:

```json
{ "name": "Montre 8", "family": "Principal", "footage": 8,
  "spectrum": "spectra/montre-8.json" }
```

What the measurement supplies is the **shape** of the spectrum, not its level. The
absolute level of a recording is whatever the person with the microphone chose;
the partials are normalised so the loudest sits at 0 dB, and the organ's own
balance decides loudness from there. Everything else still applies on top — the
footage fold, the level calibration, and the pipe-to-pipe scatter that makes a
rank of sixty-one pipes out of one measured one.

The path is **relative to the organ document, and can only be relative**. An organ
file is something that travels — you download one, or are sent one — and a
reference that could name an absolute path, or climb out with `..`, would be a
document that reads its recipient's disk. Both are refused.

A spectrum that cannot be opened is a **warning**, not an error: the rank still
speaks, from the recipe, and the reference is kept so a document that travelled
without its spectra does not lose them the next time it is saved.

### Voicing

How this rank speaks. Every value is a unit fraction unless the name says
otherwise, and every one may be omitted.

| key | default | meaning |
|---|---|---|
| `chiff` | `0.0` | Attack transient, the consonant at the start of a pipe's speech. |
| `harmonicDevelopment` | `0.5` | How much upper harmonic content the steady tone carries. |
| `brightness` | `0.5` | Spectral tilt: higher is brighter. |
| `windSensitivity` | `0.5` | How much this rank's pitch and level follow the chest's wind. |
| `detuneScatterCents` | `1.0` | Pipe-to-pipe tuning scatter, in cents. Zero is a synthesiser; a real rank is never zero. |
| `levelScatterDb` | `0.5` | Pipe-to-pipe level scatter, in decibels. |
| `brightnessScatter` | `0.05` | Pipe-to-pipe timbre scatter. |
| `attackScatterMs` | `2.0` | Pipe-to-pipe speech-onset scatter, in milliseconds. |

### Sample set

| key | default | meaning |
|---|---|---|
| `resource` | — | Token naming the sample set. |
| `sampleRate` | `48000` | Source rate of the recordings. |
| `channels` | `1` | 1 or 2. |
| `baseNote` | `60` | The MIDI note the recording was made at. |
| `streaming` | `true` | Stream from disk rather than hold in memory. |
| `loopStart`, `loopEnd` | `0` | Loop points, in frames. |

## Divisions

A division is a keyboard and what it commands: the Grand-Orgue, the Récit, the
Pédale. It also carries the swell box, if there is one.

```json
{
  "name": "Récit",
  "kind": "Manual",
  "lowNote": 36,
  "highNote": 96,
  "enclosed": true,
  "manual": 2,
  "midiChannel": 2
}
```

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | Must be present and unique. Stops and couplers reference it. |
| `kind` | string | `"Manual"` | `Manual`, `Pedal`, `Floating`. |
| `lowNote` | number | `36` | Bottom of the keyboard. A note below it is dropped rather than sounded. |
| `highNote` | number | `96` | Top of the keyboard. A 61-note manual is 36–96; a 32-note pedalboard is 36–67. |
| `enclosed` | bool | `false` | Whether this division stands in a swell box. |
| `tremulant` | bool | `false` | Whether the division has its own tremulant. |
| `manual` | number | `0` | Stacking order on the console. 1 is the lowest manual. |
| `midiChannel` | number | `-1` | 0-based MIDI channel that plays this division; `-1` leaves it unassigned. |

A division with `kind: "Pedal"` still needs its compass: the pedalboard is a
keyboard, and a division with no playable range is a division nobody can play.

## Stops

A stop is a drawknob. It names the division it belongs to and the rank it admits.

```json
{
  "name": "Trompette 8",
  "family": "Reed",
  "footage": 8,
  "pitchClass": "Unison",
  "role": "Chorus",
  "division": "Récit",
  "rank": "Récit chest/Trompette 8"
}
```

A compound stop names its constituent ranks through `mixture` rather than through
a rank per row:

```json
{
  "name": "Fourniture IV",
  "family": "Mixture",
  "footage": 2,
  "pitchClass": "Compound",
  "role": "MixtureCrown",
  "division": "Grand-Orgue",
  "rank": "Fourniture IV",
  "mixture": [2, [4, 3], 1, [2, 3]]
}
```

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | What is engraved on the drawknob. |
| `family` | string | `"Principal"` | Same vocabulary as a rank's. Used by the console's registration helpers. |
| `footage` | number or `[num, den]` | `8` | What is engraved after the name. |
| `pitchClass` | string | `"Unison"` | `Sub`, `Unison`, `Octave`, `Super`, `Mutation`, `Compound`. |
| `role` | string | `"Foundation"` | `Foundation`, `Chorus`, `MixtureCrown`, `Solo`, `Color`, `Effect`, `Undefined`. |
| `division` | string | — | Name of the division. Must exist. |
| `rank` | string | — | Name of the rank, or `"windchest/rank"` — see below. |
| `mixture` | array | — | Constituent footages of a compound stop. Each entry takes the same two forms as `footage`: `[2, [4,3], 1, [2,3]]` is a IV-rank mixture of 2′, 1⅓′, 1′, ⅔′. |

### Naming the same rank twice

Rank names are **not** required to be unique, because on a real organ they are
not: the same *Trompette 8* appears on two manuals as two different rows of pipes,
and this instrument's own organ has five such pairs.

What must be unambiguous is a **reference**. A stop naming `"Trompette 8"` when
two ranks are called that is an error, and the loader says so and tells you the
fix: qualify it with the chest.

```json
"rank": "Récit chest/Trompette 8"
```

Only the ambiguous references need qualifying; most of a document uses bare names.

## Couplers

A coupler makes one keyboard play another's stops.

```json
{ "name": "Récit → Grand-Orgue", "from": "Récit", "to": "Grand-Orgue" }
```

| key | type | default | meaning |
|---|---|---|---|
| `name` | string | — | What is engraved on the tab. |
| `from` | string | — | Division whose stops are borrowed. |
| `to` | string | — | Division whose keys do the borrowing. |
| `octaveShift` | number | `0` | Semitones. `+12` is a super-octave coupler, `-12` a sub. |
| `kind` | string | `"InterManual"` | `InterManual`, `IntraManual`, `Melody`, `Bass`. |

## What this instrument can carry

An organ larger than the tables that hold it does not fail quietly — the loader
refuses it and says which stop was the first one over the line.

| | limit | why |
|---|---|---|
| stops | **64** | The registration the audio thread reads is a single 64-bit word. A stop past it could not be drawn, automated or sounded. |
| couplers | **16** | Past the sixteenth there is no surface — host or console — that can engage one. |
| windchests | **64** | A warning, not an error: those ranks still speak, but they draw their wind from the first chest. |

## When it will not open

Nothing is loaded until the whole document has parsed and validated, so a file
that will not open leaves the organ you were playing exactly where it was.

Errors carry a line and a column and the path that failed, e.g.
`stop[14] 'Bombarde 16': Stop references unknown division 'Pédal'.` Warnings do
not stop the load: an unknown `family` becomes `Undefined`, an inverted compass is
normalised, and both are reported.

The console shows the first error on the strip beside the load meter.
`caecilia-organ-file --check` prints all of them.

## What the file does not carry

A registration, a combination, a swell position or a tuning. Those belong to a
session, not to an instrument: the organ is what you are playing, and the session
is how you are playing it. A project stores which organ file it used — the path,
not the document, because the file is yours and you may still be editing it.
