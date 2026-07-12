<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Third-Party Notices

Caecilia is proprietary, closed-source software, © 2026 Alesson Queiroz, All
Rights Reserved (see [LICENSE](LICENSE)). This file documents third-party
components used **only** to build or test Caecilia. Each such component is
governed by its **own separate license**, which is entirely distinct from and
**never conflated with** Caecilia's proprietary license.

The Caecilia source code itself — every DSP algorithm, engine, synthesis model,
wind model, registration system, and control surface — is authored
independently and is not covered by any third-party license.

---

## JUCE

- **Used by:** the `Caecilia` plugin/UI target only (the `plugin` and `ui`
  modules). The pure `caecilia_core` static library does **not** link or include
  JUCE.
- **Obtained via:** CMake `FetchContent` at configure time (not vendored into
  this repository).
- **License:** JUCE is used **under its own JUCE license**, obtained and
  complied with separately by the copyright owner. Distribution of any Caecilia
  binary that links JUCE is the owner's responsibility under the applicable JUCE
  license terms. Caecilia claims no rights over JUCE, and the JUCE license does
  **not** apply to Caecilia's own source.
- **Project:** https://juce.com

## Catch2

- **Used by:** the headless unit tests only (the `tests` target). Not linked
  into `caecilia_core`, the plugin, or any shipped artifact.
- **Obtained via:** CMake `FetchContent` at configure time.
- **License:** Boost Software License 1.0 (BSL-1.0), a permissive license that
  applies solely to the Catch2 test framework and not to Caecilia.
- **Project:** https://github.com/catchorg/Catch2

---

## Independence of Caecilia DSP

All Caecilia signal processing is implemented from public mathematical
literature and first principles:

- The algorithmic reverb follows the **Feedback Delay Network** principles
  described by Jot (1991); no third-party reverb source is copied.
- The interpolator is a freshly written **16-point Kaiser-windowed-sinc** from
  standard Kaiser-window math.

Caecilia contains **no GPL code** and copies **no source** from any GPL project.
It does **not** ingest or read GPL-licensed organ sample sets; the SpectralModel
and proprietary sample format are the owner's own intellectual property, produced
by the owner's independent offline analysis toolchain.
