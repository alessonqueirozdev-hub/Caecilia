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

It is part of the pure, JUCE-free `caecilia_core` static library. The learn table,
the binding predicates and the event packing are covered by
`tests/midi/MidiLearnTest.cpp`; the audio thread's swallow decision by
`tests/midi/LearnedControlsTest.cpp`; the router by
`tests/midi/MidiRouterTest.cpp`. The sequencer and the combination store are not
covered, and nothing instantiates them either. No `juce::` MIDI type
ever reaches it: the design is for the `plugin` module to translate
`juce::MidiMessage` into [`MidiEvent`](MidiEvent.h) at the seam.

> **Status: partly wired.** The shipping VST3/Standalone uses
> [`ChannelToDivisionMap`](ChannelToDivisionMap.h) for note routing, and
> [`MidiMap`](MidiMap.h) + [`MidiLearn`](MidiLearn.h) +
> [`MidiLearnBinding`](MidiLearnBinding.h) for MIDI learn — a drawstop or a
> general piston bound to a physical control, right-clicked on the console,
> saved with the document. That path is also the first thing to cross the seam
> this module was designed around: `plugin::CaeciliaAudioProcessor` translates a
> `juce::MidiMessage` into a [`MidiEvent`](MidiEvent.h) for it.
>
> Still never instantiated outside this directory: `MidiRouter`,
> `CombinationStore`, `Sequencer`, `VelocityCurve`, `ProgramChangeMap`. The note
> path continues to be decoded on `juce::MidiMessage`s inside
> `plugin::CommandBridge`, so the duplication below is reduced, not ended.
>
> [`MidiRouter`](MidiRouter.h) is now covered by `tests/midi/MidiRouterTest.cpp`,
> which is the groundwork for ending it. One difference has to be settled first:
> `CommandBridge` falls back to a **default division** for a channel no manual
> claims, so a single-keyboard controller plays without being configured, and the
> router ignores such a channel outright. Switching without that would silence
> every controller that has not been mapped.
>
> What the plugin does handle, through that second implementation: note on/off
> with channel→division routing and per-division compass limits, all-notes-off,
> sustain (CC 64), **expression (CC 11)** onto the division's swell shoe, and
> **program change** onto the general pistons. Aftertouch and pitch bend are
> ignored, which is right for an organ — a pipe is on or off, and its pitch is
> the instrument's. The console's si5/do6 page-turn is likewise a separate
> implementation on raw juce MIDI inside `plugin::CaeciliaAudioProcessor`, not
> this module's `Sequencer`.
>
> So what is left is not a missing feature so much as one implementation too
> many: notes, channels, expression, program change and the page-turn are all
> decoded in the plugin against `juce::MidiMessage`, and described here against
> `MidiEvent`. MIDI learn is the piece that crossed.

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
unwritten, and it is now unlikely to be written as described: registration
changes reach the audio thread as a published `EngagedRankTable`, read once per
block and reconciled against the keys already down, and that is the path a
drawstop, a piston, a coupler and a host automation move all travel by.
`AudioEngine`'s `ApplyStateDelta` case is an empty body because nothing
constructs one, not because something is missing from the working path.
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
  allocation-free and lock-free; it only *reads* the connected map. It is not
  `const`, and deliberately: it remembers the note-ons it swallowed
  ([`SwallowedNotes`](SwallowedNotes.h)) so their note-offs go the same way.
  Deciding that on the note-off instead — by asking whether the note is mapped
  *now* — eats the release of a key whose press already sounded. The
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
