# `caecilia::registration` — The queryable registration brain

Registration is Caecilia's clearest structural win. Where GrandOrgue and Aeolus
treat drawstops as opaque on/off switches, this module turns registration into a
**programmable, queryable, explainable relational system**: you select stops by
what they *are* (`family:reed & pitch:8 & div:swell`), combine those selections
with set algebra, fold declarative commands into state through a branching
undo/redo history, ask *why* any stop is sounding, and carry a registration's
*intent* across completely different organs with a confidence score.

It is part of the pure, JUCE-free `caecilia_core` static library. It runs
**entirely off the audio thread**; the only thing it hands across the audio seam
is a fixed-capacity, trivially-copyable `StateDelta`.

## The three layers

| Layer | Lives in | This module's types |
| --- | --- | --- |
| **Specification** (immutable facts) | `model::OrganSpec` / `model::StopSpec` | consumed, not owned |
| **State** (what is drawn now) | here | `RegistrationState`, `StopSet` |
| **Intent** (what you *mean*) | here | `StopQuery`, `Selector`, `RegistrationIntent` |

## Design in one breath

- **`StopQuery`** is the tiny declarative predicate atom — a bag of optional
  constraints on a stop's semantic metadata (`TonalFamily`, exact `Footage`,
  `PitchClass`, `ChorusRole`, division, name, engaged-state), all ANDed. This is
  the queryable representation the brief asks for *instead* of an embedded logic
  engine.
- **`Selector`** is a lazily-evaluated tree of `StopQuery` atoms combined with
  set algebra — union `|`, intersection `&`, difference `-`, complement `!` —
  that resolves against the live `OrganSpec` + `RegistrationState` on demand.
- **`SelectorParser`** is the single, **versioned** grammar (`GrammarVersion`
  1.0) shared verbatim by OSC, JSON-RPC, MIDI-learn and the UI omnibar, so there
  is zero API drift in how a stop is selected anywhere in the product.
- **`RegistrationState`** is the engaged `StopSet` plus first-class couplers.
- **`RegistrationCommand`** is the tagged variant every mutation is expressed as
  (engage / disengage / toggle a stop, selector or coupler; solo; clear;
  build-plenum; recall snapshot; apply intent). State is the fold of the command
  log.
- **`RegistrationHistory`** is command-sourced **branching** undo/redo: undoing
  then recording a new command forks a sibling branch instead of discarding the
  redo path, so every audition is preserved (a tree, not a stack).
- **`DefaultPlenumBuilder`** encodes organ-building convention: a principal-family
  16'-8'-4'-2' pyramid crowned by mixtures, mutations excluded by default (the
  exact-rational `Footage` keeps quints/tierces out of the diapason chorus),
  chorus reeds only on request.
- **`Explanation`** records provenance so `explain(StopId)` answers "why is this
  on?" — a direct draw, a selector match, a specific plenum role, a coupler, a
  ported intent, or a recalled snapshot.
- **`RegistrationIntent` / `port()`** is the marquee deliverable: a semantic
  description that transfers across *different* `OrganSpec`s, returning a
  `PortResult` with the resolved stops, a confidence score and per-clause
  degradation notes.
- **`Piston`** captures EITHER a literal snapshot OR a portable intent, and lowers
  to a single `RegistrationCommand` so recall flows through the normal apply path.
- **`RegistrationEngine`** (implementing `IRegistrationEngine`) ties it together:
  it folds commands, materialises history, tracks provenance, drives the plenum
  builder, and returns the `StateDelta` the caller pushes over the SPSC ring.

## The audio seam

```text
  (off the audio thread)                          (audio thread)
  command ─▶ RegistrationEngine::apply ─▶ StateDelta ══ SPSC ring ══▶ engine
             │  fold onto current state        (fixed-capacity POD:        │
             │  record in branching history     stops/couplers to          ▼
             │  update provenance               engage & disengage)   click-free,
             └─ diff old → new ─────────────────────────────────────▶ wind-modelled
                                                                        crossfade
```

`StateDelta` is a `static_assert`-enforced trivially-copyable POD (no heap, no
pointers), so it crosses the lock-free ring without allocation. Everything that
produces it — set algebra, rules evaluation, history, plenum derivation, intent
porting — stays off the audio thread.

## Public API

| Type | Header | Role |
| --- | --- | --- |
| `RegistrationEngine` / `IRegistrationEngine` | `RegistrationEngine.h` | Folds commands; `apply` / `undo` / `redo` / `query` / `explain` / `port`. |
| `RegistrationState` | `RegistrationState.h` | Engaged stops + first-class couplers. |
| `RegistrationCommand` | `RegistrationCommand.h` | The tagged mutation variant. |
| `StopQuery` | `StopQuery.h` | Declarative single-predicate atom. |
| `Selector` | `Selector.h` | Lazy set-algebra tree over `StopQuery`. |
| `SelectorParser` / `GrammarVersion` | `SelectorParser.h` | Versioned shared grammar. |
| `StopSet` | `StopSet.h` | Ordered, deduplicated set of `StopId` with `\| & -`. |
| `StateDelta` | `StateDelta.h` | Fixed-capacity POD crossing the audio seam. |
| `PlenumSpec` / `IPlenumBuilder` / `DefaultPlenumBuilder` | `PlenumBuilder.h` | Domain-intelligent build-plenum. |
| `Explanation` | `Explanation.h` | Per-stop provenance for `explain()`. |
| `RegistrationHistory` | `RegistrationHistory.h` | Command-sourced branching undo/redo. |
| `RegistrationIntent` / `PortResult` / `port()` | `RegistrationIntent.h` | Portable semantic registration. |
| `Piston` | `Piston.h` | Snapshot-or-intent recallable registration. |

## Quick start

```cpp
using namespace caecilia;

registration::RegistrationEngine engine;
engine.setOrgan(&loadedOrgan); // model::Organ compiled off-thread

// Draw the Swell reed chorus by intent:
registration::SelectorParser parser;
if (auto r = parser.parse("family:reed & div:swell"))
{
    const registration::StateDelta delta =
        engine.apply(registration::RegistrationCommand::engageSelector(r.selector));
    pushToEngine(delta); // SPSC ring -> click-free crossfade
}

// Build the Great plenum, then ask why the Mixture is on:
registration::PlenumSpec plenum;
plenum.division = core::DivisionId{0};
engine.apply(registration::RegistrationCommand::buildPlenum(plenum));
const registration::Explanation why = engine.explain(someMixtureStopId);
// why.reason == Explanation::Reason::PlenumMixture

// Port this registration onto a different organ:
const registration::RegistrationIntent intent = engine.captureIntent();
const registration::PortResult ported = registration::port(intent, otherOrgan);
// ported.confidence in [0,1]; ported.resolved is the nearest registration
```

## Real-time contract

Nothing in this module is real-time safe, by design — the registration brain is a
shell that runs off the audio thread. The one exception is `StateDelta`, which is
a trivially-copyable POD so the caller can memcpy it onto the SPSC command ring;
the audio engine applies it as a wind-modelled crossfade.

## Roadmap notes

Scaffold for **v0.7 — Registration brain**. The state model, selector algebra,
parser, branching history, plenum builder, provenance and intent porting are
wired end-to-end; deeper behaviour is marked in-code:

- `TODO(phase7)`: restore per-node provenance across undo/redo so `explain()`
  reports each node's original cause rather than a conservative rebuild.
- `TODO(phase8)`: graded intent-port scoring (reward exact footage+division hits
  over same-family fallbacks; division-remapping tables such as Swell↔Récit).
- Crescendo / Walze as an ordered `Selector` sequence, named groups and derived
  rules build on these primitives.
```
