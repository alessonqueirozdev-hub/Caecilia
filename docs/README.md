<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Documentation

## Current

These describe Caecilia 2.0 as it is:

| Document | What it covers |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Where the project is going, and where help is wanted |
| [ORGAN_FILE.md](ORGAN_FILE.md) | The JSON file that describes an organ |
| [PORTING.md](PORTING.md) | What each crate is, and how 1.x became 2.0 |
| [INSTALLER.md](INSTALLER.md) | How the installer is designed and built |

## Written for 1.x

These were written for the C++ instrument. Their ideas still hold, and they are
worth reading for the reasoning behind the design, but their paths, types and plans
describe 1.x, kept at the tag
[`v1-cpp-final`](https://github.com/alessonqueirozdev-hub/Caecilia/tree/v1-cpp-final).

| Document | What it explains | Today's code |
|---|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | The layers, and the seam at the audio thread | `crates/` |
| [WIND_MODEL.md](WIND_MODEL.md) | The bellows, the chests and the tremulant | `crates/caecilia-wind` |
| [SYNTHESIS.md](SYNTHESIS.md) | How a key press becomes sound | `crates/caecilia-synth` |
| [DSP_DESIGN.md](DSP_DESIGN.md) | The DSP building blocks | Mostly Manguebyte's `manguebyte-dsp` now |
| [REGISTRATION.md](REGISTRATION.md) | Selectors, history and explanations | `crates/caecilia-registration` |
| [TUNING.md](TUNING.md) | Temperaments and micro-tuning | `manguebyte_dsp::tuning`, `crates/caecilia-plugin` |
| [UI_DESIGN.md](UI_DESIGN.md) | The console's design | `crates/caecilia-plugin/src/ui` |
| [CONTROL_API.md](CONTROL_API.md) | Driving the organ from outside | Not all of it was carried over |

Bringing one of these up to date is a good way to learn the code, and a very
welcome contribution.
