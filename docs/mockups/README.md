<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia — UI Mockups

Self-contained HTML pages that pull in nothing external — open one directly in a
browser. `console.html` is **no longer only a mockup**: `juce_add_binary_data`
compiles it into the plugin and `CaeciliaEditor::provide` serves it to the
editor's `juce::WebBrowserComponent`, so it *is* the shipping console. Editing it
changes the product.

| File | What it specifies |
|------|-------------------|
| [`console.html`](console.html) | The full playing console: 3 terraced manuals + pedalboard, drawstop jambs grouped by division, combination bar, expression shoes, VU + wind meters, and the **Wood ⇄ Flat** skin switch. |

---

## `console.html`

A pixel/behaviour target for the console. Everything is generated from a single
`ORGAN` definition object, so the mockup exercises the same **data-driven,
semantic-identity** philosophy the real module must implement.

### What it demonstrates (mapped to the original JUCE module design)

The `caecilia::ui` counterparts in the right-hand column were written, never
instantiated, and removed — see
[`src/caecilia/ui/README.md`](../../src/caecilia/ui/README.md). The column now
reads as the design each element was drawn against, not as code you can find.

| Mockup element | `caecilia::ui` counterpart |
|----------------|----------------------------|
| Console built from the `DIVISIONS` data object | `ConsoleLayoutModel` auto-laid-out from `OrganSpec` |
| **Wood** (photoreal) vs **Flat** (modern vector) — one markup, two restyles | `ISkin` → `PhotorealSkin` / `FlatVectorSkin` over one model |
| Live VU + wind gauges polled by a `requestAnimationFrame` loop | `StateMirror` polled at frame rate (audio thread never touches a Component) |
| Reservoir pressure that **sags** as `held keys × drawn stops` rises | the wind realism moat (`IWindSupply` snapshot feeding the meters) |
| Per-stop tooltip = label + taxonomy + mock OSC address + state | pillar-5 semantic identity (one fact → tooltip, a11y label, OSC target) |
| Omnibar parsing `reed & 8`, `principal \| mixture`, `rec & !reed` | the shared `SelectorParser` grammar (marquee registration feature) |
| Combination pistons recalling selector-based **intents** | `Piston(Intent)` resolved lazily against the `OrganSpec` |
| Tonal-family colour tokens | `ThemeManager` first-class family-tint token |

### Controls

- **Draw a stop** — click any drawknob. Family colour tints it; Wood pulls the
  knob out, Flat fills the chip.
- **Play** — click keys on the manuals / pedalboard; they latch so you can build
  a chord and watch the wind sag and the VU rise. *Release keys* clears them.
- **Selector omnibar** — type e.g. `reed & 8` to live-highlight matches; press
  **Enter** to draw them. Supports `&` (and), `|` (or), `!`/`-` (not), and atoms
  matching family · role · pitch-class · division · footage · stop name.
- **Combination bar** — Generals 1–8 recall registrations; **SET** arms capture;
  **0** cancels; **TUTTI** is a reversible full-organ; **◀ ▶** step the
  sequencer (the Si5/Do6 → Previous/Next mapping from the user's rig).
- **Expression shoes** — drag (or scroll) **Swell** (dims the enclosed Récit) and
  **Crescendo** (a Walze that progressively engages stops softest-to-loudest).
- **Tremulant** — toggles the *simulated* wind modulation on the Récit (visible on
  its gauge). It is inaudible inside the plugin: the page calls
  `caeciliaSetTremulant`, a name `CaeciliaEditor::makeOptions` does not register.
- **Wood / Flat** — top-right skin switch.

### Constraints honoured

- Single file, 100 % inline CSS/JS, zero network requests (CSP-safe).
- Resolution-independent (CSS/flex/grid; scales and reflows; jambs stack under
  the manuals below ~940 px). No horizontal page scroll — wide keyboards scroll
  inside their own container.
- Respects `prefers-reduced-motion`.
- Every drawstop is a real `<button>` with `aria-pressed` and a semantic
  `aria-label`, sketching the `ConsoleAccessibilityHandler` contract.

> Opened in a plain browser this is a design reference: no audio, and the meters
> are a plausible simulation. Inside the plugin the native bridge is live —
> drawstops and keys really sound, and the VU shows the engine's mastered output
> peak — but the wind gauges are still that simulation, because the engine does
> not step the wind model yet.
