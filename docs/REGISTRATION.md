<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia — Registration Design

The queryable, relational registration brain: the clearest structural win over
every mainstream organ VST. This document specifies the three layers
(specification / state / intent), the `Selector` algebra and its shared grammar,
the terminating rules engine with provenance and **branching** undo/redo,
domain-intelligent build-plenum, pistons, and the marquee deliverable — porting a
semantic **intent** across *different* organs with a confidence score.

> Scope: complements [`ARCHITECTURE.md`](ARCHITECTURE.md) §5 and
> [`CONTROL_API.md`](CONTROL_API.md) (the transports that drive this one brain).
> Everything here runs **off the audio thread**; only a fixed-capacity
> `StateDelta` ever crosses the audio seam.

---

## 1. Lineage & credit

The idea of treating organ registration as a **queryable, semantic, voice- and
script-driven** system — rather than a flat list of on/off drawknobs — is
inspired by the open **talon-organteq** community's registration-by-voice work.
Caecilia credits that inspiration for the *concept*.

Caecilia's implementation is an **independent re-implementation of the concept**,
not a port: it uses a purpose-built, terminating `Selector`/rules engine in
C++20. **No talon-organteq Prolog (or any GPL source) is copied or referenced**;
the selector algebra, rules derivation and porting are original work (see
[`LICENSE`](../LICENSE) and [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)).

---

## 2. Three layers the baseline conflated

| Layer            | Type(s)                              | Nature                                   |
|------------------|--------------------------------------|------------------------------------------|
| **Specification**| `model::Stop` / `OrganSpec`          | immutable per-organ metadata             |
| **State**        | `RegistrationState`                  | which stops + couplers are drawn *now*   |
| **Intent**       | `Selector` (over `StopQuery` atoms)  | a lazily-evaluated semantic query        |

Keeping these apart is what makes registration *portable*: state is bound to one
organ, but intent means the same thing on any organ.

### 2.1 Specification — the semantic taxonomy

Every `model::Stop` carries the "pillar-5" identity plus a rich taxonomy the
selectors resolve against:

```cpp
TonalFamily family;    // Principal, Flute, String, Reed, Mixture, Hybrid, Percussion
Footage     footage;   // EXACT reduced rational: 8'=8/1, 2 2/3'=8/3, 1 3/5'=8/5
PitchClass  pitchClass;// Sub, Unison, Octave, Super, Mutation, Compound
ChorusRole  role;      // Foundation, Chorus, MixtureCrown, Solo, Color, Effect
DivisionId  division;  RankId rank;
std::vector<Footage> mixtureComposition;  // constituent footages of a compound stop
```

The **exact-rational `Footage`** is a correctness requirement, not a nicety: it
is what lets a pitch selector or build-plenum keep quints and tierces out of a
principal chorus (`Footage::isMutation()`, `octaveClassFrom8()`).

### 2.2 State — `RegistrationState`

The live set of engaged stops (`StopSet`) plus engaged couplers, **first-class**
so a key expansion folds coupled divisions in before the pipe-activation mapping
is queried. Equality is by engaged-set content, so re-deriving the same
registration always compares equal — which makes `StateDelta` diffs
deterministic.

---

## 3. The Selector algebra (the INTENT layer)

A `Selector` is a lazily-evaluated tree of `StopQuery` atoms combined with set
algebra:

```
  Op = Atom | Union(|) | Intersect(&) | Difference(−) | Complement(!)
```

A `StopQuery` is a tiny, data-only conjunctive predicate — every populated field
is ANDed:

```cpp
optional<TonalFamily> family;   optional<ChorusRole> role;
optional<PitchClass>  pitchClass; optional<Footage>   footage;
optional<DivisionId>  division;  std::string          divisionName;   // "div:swell" (resolved lazily)
std::string           nameContains;
Engaged engaged{Any|Only|Exclude};
bool mutationsOnly, excludeMutations;
```

Resolution is **lazy** against the live `OrganSpec` + `RegistrationState`, so a
*stored* selector keeps meaning as the registration — or even the loaded organ —
changes. A default-constructed query matches every stop; the `Complement`
universe is "every stop in the spec".

```cpp
// value-semantic, self-contained tree; ergonomic operators mirror the grammar:
Selector s = family(Reed) & pitch(8) & div("swell");   // = one StopQuery, three constraints
StopSet  m = s.resolve(organ, state);
```

---

## 4. One grammar, one authority (`SelectorParser`)

A **single** `SelectorParser` turns text into a `Selector` tree, and it is reused
**verbatim** by OSC, JSON-RPC, MIDI-learn and the UI omnibar — zero API drift.

```
expression   := union
union        := intersection ( '|' intersection )*
intersection := unary ( ( '&' | '-' ) unary )*        // '-' is set difference
unary        := '!' unary | primary                   // '!' is complement
primary      := '(' expression ')' | predicate | keyword
predicate    := key ':' value
key          := family | role | class | pitch | footage | div | division | name
keyword      := engaged | all | '*'
```

- Footage values are **feet**: an integer (`pitch:8`, `pitch:16`) or an exact
  improper fraction (`pitch:8/3` == 2 2/3', `pitch:16/3` == 5 1/3'), mirroring
  `core::Footage` so text and code agree exactly.
- Names may be quoted: `name:"open diapason"`.
- Errors are **returned, never thrown** (`Result{ ok, selector, error, errorPos }`).

The grammar is a **versioned public contract** (`GrammarVersion{1,0}`) from day
one, because external scripts depend on it: bump `minor` for additive changes,
`major` for a breaking one.

Example expressions:

```
family:reed & pitch:8 & div:swell
family:principal - class:mutation                 ; the diapason chorus, no quints/tierces
(family:reed | family:mixture) & div:great
engaged & div:swell                               ; what's currently drawn in the Swell
```

---

## 5. The rules engine (`RegistrationEngine`)

A **terminating, deterministic, RT-budgeted** engine (explicitly **not** full
Prolog) folds `RegistrationCommand`s into state off the audio thread. State is
the fold of the command log, so every mutation is one declarative command:

```cpp
enum class Kind {
  EngageStop, DisengageStop, ToggleStop,
  EngageSelector, DisengageSelector, ToggleSelector,
  Solo, Clear, BuildPlenum,
  EngageCoupler, DisengageCoupler, ToggleCoupler,
  RecallSnapshot, ApplyIntent
};
```

`IRegistrationEngine` is the shared contract every driver compiles against:

```cpp
StateDelta apply(const RegistrationCommand&);   // fold; returns the delta to push over the ring
StateDelta undo();  StateDelta redo();
const RegistrationState& currentState() const noexcept;
StopSet     query(const Selector&) const;
Explanation explain(StopId) const;              // "why is this on?"
PortResult  port(const RegistrationIntent&) const;
```

Every `apply()`/`undo()`/`redo()` returns a `StateDelta` the caller pushes over
the SPSC ring to the audio engine for a **click-free, wind-modeled stop
crossfade.**

### 5.1 Provenance — `explain(StopId)`

The engine records *why* every stop is in its state, so the console can attribute
the sound — transparency no mainstream organ VST offers:

```cpp
enum class Reason {
  NotEngaged, DirectCommand, SelectorMatch,
  PlenumPrincipal, PlenumMixture, PlenumReed,
  CouplerImplied, IntentPort, SnapshotRecall
};
// e.g. explain(stop) → { reason = SelectorMatch, detail = "matched family:reed", commandIndex = 12 }
```

### 5.2 Branching undo/redo (`RegistrationHistory`)

State is materialised per node, so undo/redo is **O(1)** (no replay). Undo does
**not** discard the redo path: recording a new command after an undo creates a
*sibling branch*, so the history is a **tree**, not a stack — every explored
registration is preserved, invaluable when auditioning plenum variations.

```
   root ─▶ +Principal 8' ─▶ +Octave 4' ─▶ +Fifteenth 2'
                     └────▶ +Mixture IV        (a sibling branch after undo)
```

---

## 6. Domain-intelligent build-plenum (`PlenumBuilder`)

`BuildPlenum` derives the full chorus from organ-building conventions, using the
exact `Footage` so pitch reasoning is correct:

```cpp
struct PlenumSpec {
  optional<DivisionId> division;
  bool includeMixtures    = true;    // crown with mixtures / cornets
  bool includeChorusReeds = false;   // add unison/chorus reeds (not solo colour)
  bool excludeMutations   = true;    // keep quints/tierces OUT of the diapason chorus
  int  lowestOctaveClass  = -1;      // -1 = 16', -2 = 32'
  int  highestOctaveClass = +2;      // +2 = 2', +3 = 1'
  bool additive           = false;   // add to, vs replace within, the division
};
```

The `DefaultPlenumBuilder` selects principal-family, **unison-pitched**
16'-8'-4'-2' within the octave-class window, crowns with mixtures, optionally
adds chorus reeds, and drops mutations by default. `IPlenumBuilder` is a
first-class strategy interface, so a style-aware builder can be swapped in
without touching the engine.

---

## 7. Pistons & crescendo

A `Piston` is a recallable combination storing **either** a literal snapshot
**or** a portable intent:

```cpp
enum class Mode { Snapshot, Intent };
// Snapshot → exact stops on the captured organ (classic console behaviour)
// Intent   → resolves the semantically-nearest registration on a DIFFERENT organ
Piston p = Piston::fromIntent(engine.captureIntent(), "Gen 1");
engine.apply(p.toCommand());   // lowers to RecallSnapshot / ApplyIntent → same apply path
```

Crescendo / Walze is an **ordered `Selector` sequence** applied as the shoe
advances — each step is just another selector command through the one apply path.

---

## 8. The marquee win — portable `RegistrationIntent`

Where `RegistrationState` is concrete `StopId`s (meaningful only on one organ), a
`RegistrationIntent` is a list of **semantic clauses** that transfers across
*different* organs:

```cpp
struct RegistrationIntent {
  std::string sourceOrganName;
  struct Item { Selector selector; std::string label; float importance; bool essential; };
  std::vector<Item> items;
};
```

`captureFrom(spec, state)` turns each engaged stop into a `family & pitch & div`
clause. `port(intent, targetSpec)` re-resolves every clause against the target and
returns a **confidence-scored** result with graceful degradation:

```cpp
struct PortResult {
  StopSet resolved;              // the concrete registration to apply on the target
  float   confidence;            // weighted overall match quality in [0, 1]
  std::vector<ItemOutcome> outcomes;   // per-clause: matchedCount, essential, score
  bool    allEssentialsMatched() const noexcept;
};
```

So "the Swell reed chorus plus a Great foundation" recalls sensibly on an organ
that has never seen the original registration — and the UI can show exactly what
transferred cleanly and what degraded.

---

## 9. The audio seam — `StateDelta`

The **only** registration object that crosses to the audio thread is a
fixed-capacity, **trivially-copyable POD** (no pointers, no `std::vector`):

```cpp
struct StateDelta {                                   // static_assert(trivially_copyable)
  array<StopId,128> stopsToEngage;     size_t numEngage;
  array<StopId,128> stopsToDisengage;  size_t numDisengage;
  array<CouplerId,32> couplersToEngage/Disengage; ...
  float crossfadeMs = 12.0f;   bool truncated = false;
};
StateDelta d = computeDelta(from, to);
```

The whole brain runs off-thread on allocating value types (`StopSet`,
`RegistrationState`); `computeDelta(from, to)` diffs them into a `StateDelta` the
engine drops straight into the SPSC ring and applies as a click-free,
wind-modeled crossfade — no heap, no lock. A transition larger than the fixed
capacity sets `truncated` (only reachable by a full-organ recall; the caller then
splits it).

---

## 10. Data-flow recap

```
  OSC / JSON-RPC / MIDI-learn / UI omnibar
        │ selector text (one grammar)
        ▼
  SelectorParser → Selector           RegistrationCommand
        │                                    │
        └────────────▶ RegistrationEngine.apply(cmd)   (OFF audio thread)
                          │   folds into RegistrationState; records provenance;
                          │   branches history
                          ▼
                    computeDelta → StateDelta ── SPSC ring ──▶ AudioEngine
                                                              (click-free, wind-modeled crossfade)
```

---

## 11. Roadmap touchpoints

- **v0.7 — registration brain.** Selector algebra + grammar, first-class
  couplers, branching undo/redo, `explain()`, build-plenum, pistons
  (Snapshot|Intent), crescendo; atomic `StateDelta` → click-free crossfade; the
  `RegistrationPaletteComponent` (family × footage by-intent) in the UI.
- **v0.8 — port + MIDI-learn.** `RegistrationIntent::port()` across organ
  definitions with a confidence score; learned controls carry the same semantic
  identity.
