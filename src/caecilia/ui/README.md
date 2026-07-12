<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# `caecilia::ui` — the console UI pillar

The `ui` module is Caecilia's **first-class user-interface pillar** (benchmark:
Organteq). It renders a resolution-independent, vector-drawn organ console with
**two runtime-selectable modes** — a tactile **PhotorealConsole** (wood, turned
drawknobs) and a clean **FlatConsole** (modern minimal) — over a single shared
model and a single live-state feed, so every capability (live key illumination,
MIDI-learn, zoom/pan, theming, accessibility) is built exactly once.

It is one of only **two modules that may include JUCE** (the other is `plugin`).
It contributes its sources to the `Caecilia` JUCE target, not to the pure
`caecilia_core` static library, and it depends on `core`, `engine`, `model` and
`registration` for its vocabulary — never the other way round.

---

## The three-layer split

The whole module is organised so both console modes share everything:

```
   OrganSpec ──layout──▶ ConsoleLayoutModel ─┐
   (model)              (immutable geometry   │
                         + semantic identity)  ├─▶ ConsoleView ──paint──▶ ISkin
                                               │   (one Component,        (Photoreal | Flat)
   audio thread ──publish──▶ StateMirror ──────┘    no per-key children)
                            (lock-free frame:
                             lit keys, meters,
                             wind sag, tremulant)
```

1. **`ConsoleLayoutModel`** *(JUCE-free, testable)* — auto-lays-out the loaded
   `OrganSpec` into immutable `ConsoleElement` records. Each carries its
   **`SemanticId`** ("Great Principal 8'"), the single fact that feeds the
   tooltip, the screen-reader label, the MIDI-learn target and the OSC address.

2. **`StateMirror`** *(JUCE-free, lock-free)* — a double-buffered SPSC bridge the
   audio thread writes (via `ConsoleFrame`: `KeyStateSnapshot` + the engine's
   `MeterSnapshot`) and the UI polls at frame rate. The audio thread **never**
   touches a `juce::Component`.

3. **`ISkin`** *(paint strategy)* — knows only *how* to draw an element.
   `PhotorealConsole` (aliased `PhotorealSkin`) and `FlatConsole` (aliased
   `FlatVectorSkin`) are the two implementations. Swapping console modes is a
   pointer swap and a repaint.

`ConsoleView` is the single Component that ties them together: it hit-tests in
logical space through a `ViewportController` (zoom/pan), repaints on a
`juce::VBlankAttachment`, and exposes semantic callbacks the plugin editor wires
to the registration engine.

---

## Type map

| Type | Role |
|------|------|
| `CaeciliaLookAndFeel` | Custom look-and-feel for **standard JUCE dialogs only** (the console is painted by skins, not the LnF) |
| `Theme` / `ThemeTokens` / `ThemeManager` | JUCE-free, hot-swappable design tokens (skins + light/dark); tonal-family tint is a first-class token |
| `ConsoleLayoutModel` / `ConsoleElement` / `SemanticId` | Data-driven layout auto-built from the `OrganSpec` |
| `StateMirror` / `ConsoleFrame` / `KeyStateSnapshot` / `KeySource` | Lock-free audio→UI truth; keys lit blue/red/purple by why they sound |
| `ISkin` / `PhotorealConsole` / `FlatConsole` | Pluggable vector paint strategy = the two console modes |
| `ConsoleView` / `ViewportController` | The single console Component + zoom/pan transform |
| `ManualKeyboardComponent` | Reusable keyboard whose keys light up live from the mirror |
| `DrawstopComponent` | Animated drawknob showing `TonalFamily` + `Footage` |
| `CouplerRail` | Rail of first-class coupler tabs |
| `CombinationBar` | Pistons + sequencer (Set / Previous / Next), pending-combo readout |
| `ExpressionPedal` | Swell shoe (drag or MIDI driven) |
| `VuMeter` / `WindPressureMeter` | Output meter; animated reservoir gauge that **sags** under polyphony |
| `RegistrationPanel` (= `RegistrationPaletteComponent`) | The registration-**by-intent** family × footage surface |
| `MidiLearnOverlay` | Badges learnable elements; emits the shared semantic identity |
| `ConsoleAccessibilityHandler` | Turns one `SemanticId` into screen-reader descriptions |
| `JuceInterop.h` / `UiGeometry.h` | The single JUCE-free ↔ `juce` conversion point and logical geometry |

---

## Registration by intent

`RegistrationPanel` is the visual face of Caecilia's registration moat. Rather
than hunting drawknobs, the player picks a **tonal family × footage** cell and a
set operation (**Add / Remove / Keep / Solo / Set**), or asks for a
domain-intelligent **Build Plenum**, all with **Undo**. Every action is built
through the **same `SelectorParser` grammar** the OSC, JSON-RPC and MIDI-learn
surfaces use (`family:reed & pitch:8`), so a click and a script mean exactly the
same thing. Hovering a cell previews the matching `StopSet` so the console can
highlight the drawstops that would change before the intent is applied.

---

## Real-time safety

- **Audio thread**: only ever calls `StateMirror::publish` — allocation-free,
  lock-free, `noexcept`. It never constructs, mutates or reads a JUCE object.
- **Message thread**: everything else (layout, painting, hit-testing, selector
  resolution, theme swaps). `ConsoleView` polls the mirror once per display
  refresh and repaints; selector previews resolve lazily against the `OrganSpec`.
- Interaction is emitted as **semantic callbacks** (`onDrawstopClicked`,
  `onApplyIntent`, …); the UI owns no registration state and mutates the engine
  only indirectly, through the host, over the command ring.

---

## Status

Scaffold (roadmap `v0.2` onward). The three-layer architecture, the JUCE-free
layout model / state mirror / theme tokens, the skin interface and every named
component are in place with coherent interfaces and self-consistent stub bodies.
Painting is deliberately simple (flat fills, gradient shading) and marked with
`// TODO(vX)` where the polished work lands: the procedural wood-grain brush and
cached chrome layers (`v0.9`), dirty-region invalidation, the true white/black
key hit geometry (`v0.2`), the drawstop pull animator, the JUCE accessibility
virtual tree (`v0.9`), and the full `RegistrationPanel` wiring to the brain
(`v0.7`). The module compiles into the `Caecilia` plugin target only; the
core-only and headless test builds are unaffected.
