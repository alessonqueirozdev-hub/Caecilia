<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# caecilia::midi

MIDI interpretation and the console control surface for Caecilia. This module
turns **raw, core-native MIDI into typed engine intent**: keyboard notes routed
to divisions, the combination **sequencer** (si5/do6 = Previous/Next), the
**generals + divisionals** piston memory, **program-change → generals**, CC
mapping, and **MIDI-learn** bindings that carry the *same* selector-grammar
identity the OSC and JSON-RPC surfaces carry. (The console omnibar is not part of
that: it matches with its own, simpler grammar written in JavaScript.)

It is part of the pure, JUCE-free `caecilia_core` static library and is
headless-unit-testable, though no test suite covers it yet. No `juce::` MIDI type
ever reaches it: the design is for the `plugin` module to translate
`juce::MidiMessage` into [`MidiEvent`](MidiEvent.h) at the seam.

> **Status: not wired into the plugin.** Only
> [`ChannelToDivisionMap`](ChannelToDivisionMap.h) is used by the shipping
> VST3/Standalone: `plugin::CommandBridge` reads it while walking
> `juce::MidiMessage`s directly and builds no `MidiEvent`. `MidiRouter`,
> `MidiMap`, `MidiLearn`, `CombinationStore`, `Sequencer`, `VelocityCurve` and
> `ProgramChangeMap` are never instantiated outside this directory. What the
> plugin handles today is note on/off with channel→division routing,
> all-notes-off and sustain (CC 64); expression (CC 11), program change,
> aftertouch and pitch bend are not handled at all. The console's si5/do6
> page-turn is a separate implementation on raw juce MIDI inside
> `plugin::CaeciliaAudioProcessor`, not this module's `Sequencer`.

## Design in one paragraph

The [`MidiRouter`](MidiRouter.h) is the audio-thread front door. It reads a
published, immutable [`MidiMap`](MidiMap.h) and classifies each
[`MidiEvent`](MidiEvent.h) into a [`MidiRouteResult`](MidiRouteResult.h): a
keyboard **note** (division + transpose + shaped velocity), a **sustain** change,
a **registration** action, or a **panic**. The router owns *no* registration or
voice-allocation logic — that keeps the strict layering intact. Notes still need
per-division pipe expansion (engine-side, against the live registration), and
registration actions are to be resolved **off the audio thread** into a
`StateDelta` that becomes an `ApplyStateDelta` engine command — that bridge is
unwritten, and `AudioEngine`'s `ApplyStateDelta` case is still an empty body.
Every registration action the
router emits is a [`RegistrationCommandTemplate`](RegistrationCommandTemplate.h)
whose selector text is the shared grammar (`family:reed & div:swell`), so a
MIDI-learned drawstop is indistinguishable from one toggled anywhere else.

## Real-time contract

- **Off the audio thread:** every binding edit — [`MidiMap`](MidiMap.h) mutators,
  [`MidiLearn`](MidiLearn.h) capture, [`Sequencer`](Sequencer.h) program editing,
  [`CombinationStore`](CombinationStore.h) capture. Bindings are published as an
  immutable table and swapped in wholesale, never mutated in place under the
  audio thread.
- **On the audio thread:** [`MidiRouter::route`](MidiRouter.h) is `noexcept`,
  allocation-free and lock-free; it only *reads* the connected map. The
  sequencer's pointer-moving operations (`next`/`previous`/`goTo`) are likewise
  `noexcept` and fixed-capacity, so the registration bridge may call them when a
  page-turn intent arrives.

Nothing here allocates: the learn table, sequencer program, and combination
memory are all fixed-capacity arrays.

## Resolution order (per event)

1. **Note on/off** — a bound sequencer nav key (si5/do6) or a note learned as a
   registration control is consumed as a registration action on its *on* edge,
   and the paired note-off is swallowed; otherwise the note routes to the
   channel's division.
2. **Control change** — CC 120/123 → panic; a learned CC → registration (when it
   crosses its threshold); CC 64 → sustain on the channel's division; else
   ignored.
3. **Program change** — a learned PC binding, else the
   [`ProgramChangeMap`](ProgramChangeMap.h) (PC → generals).
4. Pitch bend / aftertouch are ignored in the scaffold (`TODO(phase8)`).

## Public API

| Type | Header | Role |
| --- | --- | --- |
| `MidiEvent` | `MidiEvent.h` | Core-native MIDI message crossing the seam; `MidiSource`, well-known CC numbers. |
| `MidiRouter` | `MidiRouter.h` | RT-safe raw-MIDI → typed intent; the audio-thread front door. |
| `MidiRouteResult` | `MidiRouteResult.h` | Discriminated routing outcome (Note / Sustain / Registration / Panic / Ignored). |
| `MidiMap` | `MidiMap.h` | The complete resolved binding table the router reads. |
| `ChannelToDivisionMap` | `ChannelToDivisionMap.h` | Channel → division, with transpose and key range. |
| `VelocityCurve` | `VelocityCurve.h` | Velocity shaping (Fixed is the organ-classic default). |
| `ProgramChangeMap` | `ProgramChangeMap.h` | PC → general / divisional recall. |
| `SequencerNavMap` | `SequencerNavMap.h` | si5/do6 → Previous/Next page-turn keys. |
| `RegistrationCommandTemplate` | `RegistrationCommandTemplate.h` | Portable semantic action + shared selector text. |
| `MidiLearnBinding` | `MidiLearnBinding.h` | One learned controller → registration action. |
| `MidiLearn` | `MidiLearn.h` | The learn capture state machine (Idle/Armed/Captured). |
| `Sequencer` | `Sequencer.h` | Ordered combination stepper walked with Prev/Next. |
| `CombinationStore` | `CombinationStore.h` | Generals + per-division divisionals (Snapshot or portable Intent). |

## Usage sketch

```cpp
using namespace caecilia;

// --- Off-thread setup ---------------------------------------------------
midi::MidiMap map;                             // si5/do6 nav preloaded
map.channels().mapChannel(0, core::DivisionId{0});   // ch 1 -> Great
map.channels().mapChannel(1, core::DivisionId{1});   // ch 2 -> Swell
map.programChange().setDefaultMode(midi::ProgramChangeMap::Mode::RecallGeneral);

// Learn a CC to "draw the Swell reeds":
midi::MidiLearn learn;
learn.arm(midi::RegistrationCommandTemplate::engage("family:reed & div:swell"));
if (learn.observe(midi::MidiEvent::controlChange(1, /*cc*/ 80, /*value*/ 127)))
    map.installBinding(learn.takeCaptured());

// --- On the audio thread ------------------------------------------------
midi::MidiRouter router;
router.connect(map);                           // store the pointer once

const auto r = router.route(midi::MidiEvent::noteOn(0, 60, 100));
// r.kind == Note, r.note.division == {0}, r.note.velocity == 100 (Fixed curve)
```

## Boundary with `registration`

The MIDI module intentionally stops at *intent*. It never parses selectors,
computes a `StateDelta`, or resolves a `Sequencer` step into audible stops — that
is the `registration` module's off-thread work. The
[`RegistrationCommandTemplate`](RegistrationCommandTemplate.h) is the contract
between them: MIDI produces it; registration is meant to consume it, parsing the
embedded selector text with the one shared `SelectorParser` grammar. Nothing
consumes one today, so that control fan-in (MIDI, OSC, JSON-RPC, UI) speaks a
single semantic language on paper only.

## Status

Scaffold (roadmap **v0.8** — *Control API + MIDI-learn*). Interfaces and the
routing model are final; the registration/engine bridge that turns a
`RegistrationCommandTemplate` or a `SequencerStep` into a published `StateDelta`
and an `ApplyStateDelta` command lands with the `registration` module
(`TODO(phase8)`).
