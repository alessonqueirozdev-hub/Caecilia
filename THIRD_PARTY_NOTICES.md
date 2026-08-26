<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Third-Party Notices

Caecilia's own source code is licensed under the
[Apache License 2.0](LICENSE). This file documents the third-party components
Caecilia is built and tested against. Each carries its **own separate licence**,
and those terms — not Apache-2.0 — govern the components themselves and any
binary that links them.

Caecilia is open source, so it qualifies for the **free** side of every
dual-licensed dependency it uses. Nothing here needs to be purchased.

---

## JUCE

- **Licence:** dual-licensed — **AGPLv3 (free)** for open-source projects that
  ship under the same free terms, or a **paid commercial licence** for
  closed-source products. Caecilia is open source and takes the free AGPLv3
  route.
- **Used by:** the `Caecilia` plugin target only — the `plugin` and `ui`
  modules. The pure `caecilia_core` static library never links or includes JUCE,
  and a `-DCAECILIA_CORE_ONLY=ON` build fetches no JUCE at all.
- **Obtained via:** CMake `FetchContent` at configure time, pinned to tag
  `8.0.4`. Not vendored into this repository.
- **How the two licences combine:** Apache-2.0 is one-way compatible with
  AGPLv3. Caecilia's own source stays **Apache-2.0**; a compiled VST3 or
  Standalone binary, because it links JUCE, is distributed under the
  **AGPLv3** — which in practice means shipping it alongside its source, as this
  project does anyway. No fee is involved.
- **When a commercial JUCE licence *would* be needed:** only if you want to ship
  a **closed-source** product built on Caecilia. Building it for yourself,
  modifying it, forking it, and redistributing it as open source are all covered
  by the free route.
- **Project:** https://juce.com/get-juce/

## Catch2

- **Licence:** Boost Software License 1.0 (BSL-1.0) — permissive.
- **Used by:** the headless unit tests only (the `caecilia_tests` target). Never
  linked into `caecilia_core`, the plugin, or any shipped artifact.
- **Obtained via:** CMake `FetchContent`, pinned to tag `v3.5.4`.
- **Project:** https://github.com/catchorg/Catch2

## Microsoft Edge WebView2

- **Licence:** Microsoft's own WebView2 SDK terms.
- **Used by:** the embedded console on Windows (`juce::WebBrowserComponent` with
  the WebView2 backend, statically linked loader). Not used on macOS or Linux,
  which use WKWebView and WebKitGTK respectively.
- **Obtained via:** the `Microsoft.Web.WebView2` NuGet package at build time.
- **Runtime:** the Evergreen WebView2 Runtime ships with Windows 10/11.
- **Project:** https://developer.microsoft.com/microsoft-edge/webview2/

---

## Provenance of Caecilia's DSP

Every signal-processing algorithm in this repository is written from published
mathematics and first principles. Specifically:

- The algorithmic reverb follows the **unitary Feedback Delay Network**
  principles described by Jot (1991) — a Hadamard mixing matrix and the standard
  RT60-to-feedback-gain mapping. No third-party reverb source is copied.
- The input diffusion uses **Dattorro's (1997) published all-pass tap lengths**,
  converted to milliseconds so they scale to any sample rate.
- The filters are the standard **RBJ biquad cookbook** bilinear-transform
  designs.
- The interpolator is a freshly written **16-point Kaiser-windowed sinc** from
  standard Kaiser-window mathematics.
- The additive synthesis, wind model, spectral recipes and registration algebra
  are original work.

**No GPL DSP source — GrandOrgue, Aeolus, or otherwise — was referenced or
copied**, and Caecilia does not read GPL-licensed organ sample sets. The
`SpectralModel` and Caecilia's own sample-set format are original designs.

If you believe any part of this repository infringes a third party's rights,
please open an issue and it will be addressed.
