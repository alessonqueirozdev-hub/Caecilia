<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia — UI Design

The UI is a first-class pillar, not an afterthought. This document specifies the
three-layer split (data-driven model / lock-free state mirror / pluggable skin),
the vector rendering strategy, live audio-thread feedback, the token-based theme
system, the registration-by-intent palette, accessibility, and — explicitly —
the **Organteq benchmark and how Caecilia beats it**.

> Scope: complements [`ARCHITECTURE.md`](ARCHITECTURE.md), [`REGISTRATION.md`](REGISTRATION.md)
> (the palette drives that brain) and the engine metering in
> [`WIND_MODEL.md`](WIND_MODEL.md). `ui` is one of only two modules allowed to
> include JUCE (the other is `plugin`).

---

## 1. Benchmark: Organteq, and how Caecilia wins

Modartt Organteq sets the bar for polish: crisp vector graphics, a clean console,
smooth interaction. Caecilia's stated goal is to **match Organteq on polish** and
**beat everyone on programmable, portable, explainable registration**, while the
audio pillars (hybrid voice + wind) beat GrandOrgue and Aeolus on sound.

| Capability                         | GrandOrgue        | Aeolus        | Organteq        | **Caecilia**                                   |
|------------------------------------|-------------------|---------------|-----------------|------------------------------------------------|
| Vector, crisp-at-4K console        | ✗ (bitmap themes) | ✗ (utilitarian)| ✓              | ✓ (pure vector + texture brushes)              |
| Photoreal **and** flat skins       | partial (skins)   | ✗             | one look        | ✓ two skins over **one** model + feed          |
| Live key illumination w/ *reason*  | basic             | ✗             | basic           | ✓ blue/red/purple = direct/coupled/combination |
| Visible wind physics (sag gauges)  | ✗                 | ✗             | limited         | ✓ animated reservoir/pressure gauges           |
| Registration **by intent** surface | ✗                 | ✗             | ✗               | ✓ family × footage palette (union/∩/−/solo)    |
| "Why is this stop on?" in the UI   | ✗                 | ✗             | ✗               | ✓ `explain()` provenance tooltip               |
| One identity → tooltip/SR/MIDI/OSC | ✗                 | ✗             | ✗               | ✓ pillar-5 `SemanticId`                        |
| Programmable console (OSC/RPC)     | ✗                 | ✗             | ✗               | ✓ omnibar shares the control grammar           |

Caecilia doesn't merely draw prettier drawknobs — it makes the console a
**window into a queryable instrument**: every element knows what it *is*, why it
is on, and how to be driven by a script.

---

## 2. The three-layer split

Both console modes share everything because the UI is split into three layers;
only the paint strategy differs.

```
  ┌─ 1. ConsoleLayoutModel ─────────────────────────────────────────────┐
  │   auto-laid-out from OrganSpec → immutable ConsoleElement geometry   │
  │   records, each carrying one SemanticId (the pillar-5 fact)          │  JUCE-free,
  └──────────────────────────────────────────────────────────────────────┘  unit-testable
  ┌─ 2. StateMirror ────────────────────────────────────────────────────┐
  │   lock-free double buffer: audio thread WRITES a ConsoleFrame        │  JUCE-free,
  │   (meters + lit keys), UI READS at frame rate. No Component touched. │  RT-safe writer
  └──────────────────────────────────────────────────────────────────────┘
  ┌─ 3. ISkin ──────────────────────────────────────────────────────────┐
  │   PhotorealSkin | FlatVectorSkin — paints model + mirror into a      │  JUCE (paint)
  │   logical-space juce::Graphics. Both modes are Skins over ONE model. │
  └──────────────────────────────────────────────────────────────────────┘
```

Because live feedback, hit-testing, MIDI-learn, zoom/pan, theming and
accessibility are all built against layers 1–2, they are implemented **exactly
once** and work identically in both skins.

---

## 3. Layer 1 — `ConsoleLayoutModel` (data-driven, JUCE-free)

`layoutFrom(const model::Organ&, LayoutRect canvas)` auto-lays-out the loaded
organ (variable manuals, pedal, jambs, coupler rail, combination bar, meters,
expression shoes) into a flat, immutable list of `ConsoleElement`s. Geometry is in
**logical canvas units** (`UiGeometry`: `LayoutPoint/Size/Rect`), so it is
resolution-independent and unit-testable without a JUCE host. Layout is
deterministic for a given organ + canvas, so re-layout on resize is stable.

Each element carries its role and its semantic identity:

```cpp
enum class ElementRole {
  Manual, Pedalboard, Drawstop, Coupler, Piston, SequencerButton,
  ExpressionPedal, WindGauge, VuMeter, Jamb, Label, Unknown
};
struct ConsoleElement {
  ElementRole role; LayoutRect bounds; SemanticId semantic;
  Footage footage; TonalFamily family;         // for label + theme tint
  MidiNote lowNote, highNote;                   // keyboard compass
  uint16_t index; bool learnable;
};
```

`hitTest(LayoutPoint)` resolves the topmost element (back-to-front, so visual
top wins) in logical space — one custom `ConsoleView` Component, **no per-key
sub-Component**.

### 3.1 The pillar-5 `SemanticId` — one fact, four consumers

```cpp
struct SemanticId {
  ElementRole role; StopId stop; DivisionId division; CouplerId coupler;
  TonalFamily family;
  std::string label;      // "Great Principal 8'"
  std::string oscAddress; // "/caecilia/great/principal-8"
};
```

This *single* fact is the source for the **tooltip**, the **screen-reader
label**, the **MIDI-learn target** and the **OSC address** — so they can never
disagree. `slugify(text)` derives the OSC path segment from the label.

---

## 4. Layer 2 — `StateMirror` (lock-free live feedback)

The audio thread **never** touches a `juce::Component`. It publishes an immutable
`ConsoleFrame` into a double-buffered slot and flips an atomic index; the UI polls
a fully-consistent frame at frame rate.

```cpp
struct ConsoleFrame {
  core::engine::MeterSnapshot meters;   // levels, wind sag, tremulant phase, voice count
  KeyStateSnapshot            keys;     // which keys are lit and WHY
};
// audio thread:  mirror.publish(frame);            // RT-safe: write back buffer, release flip
// message thread: ConsoleFrame f = mirror.read();  // acquire, copy front buffer (never torn)
```

### 4.1 Key illumination with *reason* (real console practice)

`KeyStateSnapshot` stores a `KeySource` byte per (division, note), which chooses
the illumination colour exactly as a real console does:

| `KeySource`    | Colour  | Meaning                                        |
|----------------|---------|------------------------------------------------|
| `PlayedDirect` | blue    | pressed on this division's own keyboard        |
| `Coupled`      | red     | sounded via a coupler from another division    |
| `Combination`  | purple  | driven by a combination / sequencer action     |

Today only `PlayedDirect` and `Off` are ever published: couplers are defined in
the model but never applied, so red and purple never appear.

The metering feed carries `windPressurePa`, `windSagNorm` and `tremulantPhase`
(from [`WIND_MODEL.md`](WIND_MODEL.md)), which is what would make the wind physics
**visible** — see §7. They are published as zeros until the engine steps the wind
model, so the console's gauges are driven by its own JS curve instead.

---

## 5. Layer 3 — `ISkin` (pluggable paint strategy)

A skin knows only *how* to draw an element; it owns no state about *which*
elements exist (layer 1) or their live values (layer 2). Both modes implement one
interface with cohesive, named routines (so the photoreal skin can cache
per-role brushes):

```cpp
class ISkin {
  string_view name(); SkinId id();
  void setTheme(const Theme&);                 // rebuild cached brushes/gradients (off-thread)
  // static chrome (cacheable into a background layer):
  void paintBackground(g, canvas);  void paintJamb(g, el);  void paintLabel(g, el);
  // dynamic, live-feedback:
  void paintDrawstop(g, el, DrawstopVisual{engaged, hovered, pullNorm});
  void paintKeyboard(g, el, KeyStateSnapshot, divisionSlot);
  void paintCoupler(g, el, CouplerVisual{engaged, hovered});
  void paintMeter(g, el, MeterFrame);
  void paintWindGauge(g, el, pressurePa, sagNorm);
};
```

- **`PhotorealSkin`** — wood via **vector geometry + tiled texture brushes +
  procedural shading**, crisp at 4K (no bitmap themes to pixelate).
- **`FlatVectorSkin`** — modern minimal flat-vector look.

`juce::LookAndFeel` (`CaeciliaLookAndFeel`) is confined to standard dialogs only;
the console itself is fully custom.

---

## 6. Rendering strategy

- **One `ConsoleView` Component.** Hit-tests in logical space through a
  `ViewportController` `AffineTransform` (zoom/pan); no per-key sub-Component.
- **Vector-first, crisp at any zoom.** Skins paint into the caller-supplied,
  already-transformed logical-space `juce::Graphics`, so a drawknob is sharp at
  400% just as at 100%.
- **Cheap repaints.** Rendered via a `VBlankAttachment` with **dirty-region
  invalidation** and **cached static chrome layers** (background/jambs/labels
  painted once, live elements on top).
- **Modern backends.** Targets JUCE 8 Direct2D (Windows) / Metal (macOS) with an
  OpenGL/software fallback.

---

## 7. Making wind physics visible

`ElementRole::WindGauge` elements animate the reservoir/pressure state straight
from the mirror: `paintWindGauge(g, el, pressurePa, sagNorm)` renders a gauge that
**sags under polyphony** and ripples at the tremulant rate
(`MeterSnapshot::tremulantPhase`). This turns the realism moat from
[`WIND_MODEL.md`](WIND_MODEL.md) into something the player can *see* — no other
organ VST surfaces live wind sag on the console.

---

## 8. Registration by intent — `RegistrationPaletteComponent`

The novel surface: a **family × footage** grid that drives the registration brain
by *intent* rather than by poking individual drawknobs. Selecting cells composes a
`Selector` (via the shared grammar) and issues union / difference / intersection /
solo / clear / build-plenum commands, highlighting the matching drawstops **live**:

```
            8'   4'   2 2/3'  2'   1 3/5'  Mixt
 Principal [■]  [■]   [ ]    [■]   [ ]    [■]     ⟵ selecting a column/row builds a Selector:
 Flute     [ ]  [ ]   [ ]    [ ]   [ ]    [ ]        family:principal & (pitch:8 | pitch:4 | ...)
 String    [ ]  [ ]   [ ]    [ ]   [ ]    [ ]
 Reed      [■]  [ ]   [ ]    [ ]   [ ]    [ ]     [∪ union] [∩] [− diff] [solo] [clear] [plenum] [undo]
```

Because it goes through the same `RegistrationEngine`, it inherits branching
undo/redo and `explain()` for free — a tooltip can answer **"why is this stop
on?"** (see [`REGISTRATION.md`](REGISTRATION.md) §5). A console **omnibar** takes
the same selector text a script would send over OSC/JSON-RPC, so the UI and the
control API are one programmable surface.

`MidiLearnOverlay` badges every `learnable` element; clicking arms `midi::MidiLearn`
with that element's `SemanticId`, so a learned control carries the identical
semantic identity (see [`CONTROL_API.md`](CONTROL_API.md) §7).

---

## 9. Theming — token-based, hot-swappable

A `Theme` is a flat bag of **semantic tokens** (`ThemeTokens`), JUCE-free so both
skins and the look-and-feel read the same source of truth. Switching skins or
toggling light/dark is a pointer swap plus a repaint — no component teardown.

```cpp
struct ThemeTokens {
  ThemeMode mode;                     // Light | Dark
  Rgba surface, surfaceRaised, surfaceSunken, textPrimary, textSecondary, outline;
  Rgba accent, accentActive;
  Rgba keyLitDirect, keyLitCoupled, keyLitCombination;   // blue / red / purple
  Rgba meterLow, meterMid, meterHigh, windGauge;
  std::array<Rgba, 8> familyTint;     // FIRST-CLASS token, indexed by TonalFamily
  float cornerRadius, hairline, drawstopSize;
};
```

The **tonal-family tint table is a first-class token**: a drawstop is always
tinted by what it *is* (a red reed, a blue-ish principal), consistently across
both skins and both light/dark variants (`tintFor(family)`). `ThemeManager`
registers named themes, exposes the active one, and fans a change notification to
every listener (`ConsoleView`, `CaeciliaLookAndFeel`, open panels), with built-in
`makeDefaultFlatDark()` / `makeDefaultFlatLight()` / `makeDefaultPhotoreal()`.

---

## 10. Accessibility

In the design, the pillar-5 `SemanticId` is the single source for the
`ConsoleAccessibilityHandler` screen-reader label, so assistive tech reads
"Great Principal 8', drawn" from the same fact that feeds the tooltip and the OSC
address. Today there is no such handler and the console is a WebView, which is
opaque to the host's accessibility layer; the only screen-reader surface is the
page's own `aria-label` / `aria-pressed` markup on each drawstop. Because registration is fully scriptable through the shared grammar
(omnibar + control API), the whole instrument is operable without pixel-precise
mouse control — an accessibility win no mainstream organ VST offers.

---

## 11. Real-time safety summary

| Concern            | Guarantee                                                                    |
|--------------------|------------------------------------------------------------------------------|
| Audio → UI         | Only via lock-free `StateMirror` double buffer; audio thread never sees a Component |
| Layout / skins     | JUCE-free model + geometry; skins paint on the message thread only            |
| Repaint cost       | Dirty-region invalidation + cached chrome + VBlank-driven repaint             |
| Theming            | Token swap + repaint; no teardown; one token set shared by both skins         |
| Identity           | One `SemanticId` → tooltip, screen reader, MIDI-learn, OSC address            |

---

## 12. Roadmap touchpoints

- **v0.2** — `StateMirror` + first `FlatVectorSkin` `ConsoleView` (keys lighting,
  live wind gauge), data-driven from the `OrganSpec`.
- **v0.7** — `RegistrationPaletteComponent` (family × footage by-intent) wired to
  the registration brain.
- **v0.9** — `PhotorealSkin` (vector + texture brushes + procedural shading, 4K),
  cached chrome + VBlank dirty-region repaint on Direct2D/Metal with software
  fallback, `ViewportController` zoom/pan, `ThemeManager` hot-swap,
  `ConsoleAccessibilityHandler`, animated wind gauges.
