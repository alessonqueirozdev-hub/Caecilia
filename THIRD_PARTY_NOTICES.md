<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Third-Party Notices

Ceciliae is proprietary, closed-source software, © 2026 Alesson Queiroz, All
Rights Reserved (see [LICENSE](LICENSE)). This file documents third-party
components used **only** to build or test Ceciliae. Each such component is
governed by its **own separate license**, which is entirely distinct from and
**never conflated with** Ceciliae's proprietary license.

The Ceciliae source code itself — every DSP algorithm, engine, synthesis model,
wind model, registration system, and control surface — is authored
independently and is not covered by any third-party license.

---

## JUCE

- **Used by:** the `Ceciliae` plugin/UI target only (the `plugin` and `ui`
  modules). The pure `ceciliae_core` static library does **not** link or include
  JUCE.
- **Obtained via:** CMake `FetchContent` at configure time (not vendored into
  this repository).
- **License:** JUCE is used **under its own JUCE license**, obtained and
  complied with separately by the copyright owner. Distribution of any Ceciliae
  binary that links JUCE is the owner's responsibility under the applicable JUCE
  license terms. Ceciliae claims no rights over JUCE, and the JUCE license does
  **not** apply to Ceciliae's own source.
- **Project:** https://juce.com

## Catch2

- **Used by:** the headless unit tests only (the `tests` target). Not linked
  into `ceciliae_core`, the plugin, or any shipped artifact.
- **Obtained via:** CMake `FetchContent` at configure time.
- **License:** Boost Software License 1.0 (BSL-1.0), a permissive license that
  applies solely to the Catch2 test framework and not to Ceciliae.
- **Project:** https://github.com/catchorg/Catch2

---

## Independence of Ceciliae DSP

All Ceciliae signal processing is implemented from public mathematical
literature and first principles:

- The algorithmic reverb follows the **Feedback Delay Network** principles
  described by Jot (1991); no third-party reverb source is copied.
- The interpolator is a freshly written **16-point Kaiser-windowed-sinc** from
  standard Kaiser-window math.

Ceciliae contains **no GPL code** and copies **no source** from any GPL project.
It does **not** ingest or read GPL-licensed organ sample sets; the SpectralModel
and proprietary sample format are the owner's own intellectual property, produced
by the owner's independent offline analysis toolchain.
