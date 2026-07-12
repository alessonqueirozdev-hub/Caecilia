<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia — UI Mockups

Interactive, self-contained HTML mockups that are the **visual and behavioural
design reference** for the JUCE `caecilia::ui` module. They are not shipped
code and pull in nothing external — open any file directly in a browser.

| File | What it specifies |
|------|-------------------|
| [`console.html`](console.html) | The full playing console: 3 terraced manuals + pedalboard, drawstop jambs grouped by division, combination bar, expression shoes, VU + wind meters, and the **Wood ⇄ Flat** skin switch. |

---

## `console.html`

A pixel/behaviour target for the console. Everything is generated from a single
`ORGAN` definition object, so the mockup exercises the same **data-driven,
semantic-identity** philosophy the real module must implement.

### What it demonstrates (mapped to the module design)

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
- **Tremulant** — toggles wind modulation on the Récit (visible on its gauge).
- **Wood / Flat** — top-right skin switch.

### Constraints honoured

- Single file, 100 % inline CSS/JS, zero network requests (CSP-safe).
- Resolution-independent (CSS/flex/grid; scales and reflows; jambs stack under
  the manuals below ~940 px). No horizontal page scroll — wide keyboards scroll
  inside their own container.
- Respects `prefers-reduced-motion`.
- Every drawstop is a real `<button>` with `aria-pressed` and a semantic
  `aria-label`, sketching the `ConsoleAccessibilityHandler` contract.

> This is a **design reference only** — no audio, no real DSP. The numbers on the
> meters are a plausible simulation, not the engine.
