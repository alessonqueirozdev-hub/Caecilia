<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Third-Party Notices

Caecilia's own source code is licensed under the **Apache License 2.0** (see
[LICENSE](LICENSE)). Caecilia 2.0 is written in Rust and links no C or C++
framework: no JUCE, no WebView. Its binaries (the CLAP and VST3 plugin and the
standalone application) are therefore Apache-2.0 as well, together with the
terms of the crates below.

## Manguebyte

[Manguebyte](https://github.com/alessonqueirozdev-hub/manguebyte), the audio
framework Caecilia is built on (plugin formats, DSP, the user interface
toolkit, audio and MIDI devices), is dual-licensed **MIT OR Apache-2.0**.

## Crates from crates.io

Manguebyte and Caecilia depend on crates published on
[crates.io](https://crates.io), each under its own licence (MIT, Apache-2.0,
or both, for nearly all of them). `Cargo.lock` lists the exact set and
versions; `cargo metadata` reports each one's licence.

## Provenance of Caecilia's DSP

Every signal-processing algorithm in this repository, and in Manguebyte where
Caecilia uses it, is written from published mathematics and first principles:

- The filters, including the master EQ's shelves and peaks, are the standard
  **RBJ biquad cookbook** designs.
- The reverb is a **feedback delay network** of the kind Jot (1991) describes.
- The additive pipe voices, the wind model (bellows, windchests, trunks,
  tremulants), the spectral recipes and the registration algebra are original
  work.

**No GPL source (GrandOrgue, Aeolus, or otherwise) was referenced or copied**,
and Caecilia does not read GPL-licensed organ sample sets. Other instruments
are cited only by their observable behaviour and public documentation.

If you believe any part of this repository infringes a third party's rights,
please open an issue and it will be addressed.
