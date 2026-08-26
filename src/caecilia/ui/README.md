<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# `caecilia::ui`

One header: [`StateMirror.h`](StateMirror.h).

## What this module is

`StateMirror` is the audio thread's window onto the console. Each block the
audio thread publishes one `ConsoleFrame` — the meters plus which keys are lit
and why — and the UI reads the newest complete frame at frame rate. It is
JUCE-free and allocation-free, and the handoff is a `core::TripleBuffer`, so
neither thread ever blocks and neither can see a torn frame.

That is the whole module. It builds nothing; a header needs no target.

## What used to be here, and why it is gone

This directory held a second, hand-built JUCE console: `ConsoleView`,
`PhotorealConsole`, `FlatConsole`, `ConsoleLayoutModel`, `ViewportController`,
`ManualKeyboardComponent`, `DrawstopComponent`, `CouplerRail`, `CombinationBar`,
`ExpressionPedal`, `VuMeter`, `WindPressureMeter`, `RegistrationPanel`,
`MidiLearnOverlay`, `ConsoleAccessibilityHandler`, `ThemeManager` and
`CaeciliaLookAndFeel` — around 5,000 lines across 37 files.

Every one of them was compiled into the shipping binary. **Not one was ever
instantiated.** The editor is a `juce::WebBrowserComponent` serving
`docs/mockups/console.html`, and has been since the console was rebuilt as a
web view because it looked better.

Carrying both cost build time. It did NOT cost binary size -- measured before and
after removal, the Standalone came out within 512 bytes of the same, because the
linker was already discarding every one of those symbols as unreachable. The real
cost was to anyone reading the tree: a `ConsoleAccessibilityHandler` sitting in the source implies
the console is reachable by a screen reader, and it is not — a WebView is opaque
to the host's accessibility layer. That is a real limitation of the current UI
and it deserves to be stated plainly rather than papered over by dead code.

The files are in the git history if the JUCE console is ever revived.

## Where the console lives now

- The markup and styling: [`docs/mockups/console.html`](../../../docs/mockups/console.html),
  compiled into the binary by `juce_add_binary_data`.
- The bridge: [`plugin/PluginEditor.cpp`](../plugin/PluginEditor.cpp) — a
  resource provider serves the page, `withNativeFunction` turns its drawstops and
  keys into engine commands, and a 30 Hz timer pushes `StateMirror` frames back.
