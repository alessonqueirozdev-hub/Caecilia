<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia

**An open-source modeled pipe organ: living wind, every rank its own voice.
CLAP, VST3 and a standalone application.**

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

> Status: **2.0 alpha.** Caecilia 2.0 is the instrument rewritten in Rust on the
> [Manguebyte Audio Framework](https://github.com/alessonqueirozdev-hub/manguebyte):
> no JUCE, no WebView, no JavaScript. It builds and plays on Windows as CLAP,
> VST3 and a standalone application. The 1.x C++/JUCE instrument it replaces
> is kept at the tag [`v1-cpp-final`](https://github.com/alessonqueirozdev-hub/Caecilia/tree/v1-cpp-final);
> what it measured is frozen in `reference/cpp-1.x`, which 2.0 is tested against.

## What it is

- **Modeled, not sampled.** Every rank speaks with its own additive voice:
  staggered onsets, chiff, mixtures that break back, drift. Every pipe takes
  its wind from a windchest fed by a modeled bellows, so a full chord sags the
  wind and the pipes answer to it. The tremulant shakes the wind, not the
  volume.
- **Any organ.** An organ is a JSON file of windchests, ranks, divisions, stops
  and couplers ([docs/ORGAN_FILE.md](docs/ORGAN_FILE.md)). A three-manual
  French demonstration organ is built in. Organs change live, without a click,
  from the console's library.
- **A console that is an organ console.** Drawstop jambs, manuals with key
  slips and general pistons, swell and crescendo shoes, a pedalboard with toe
  studs, wind gauges in millimetres of water. It is drawn natively with
  Manguebyte's toolkit, in a night or a day light, and lays itself out for
  whatever organ plays.
- **Registration that thinks.** A selector language
  (`div:recit & family:string`) draws stops by their nature. Branching undo
  and redo walk the history. Factory and captured generals sit on the pistons.
  Every stop says why it is drawn.
- **Demonstrations.** Pieces the organ plays on itself, each drawing its own
  stops, and a tour that plays every stop alone. The organ can be heard
  without a keyboard.
- **Sound and tuning.** The reverb's room (room, chamber, hall, cathedral,
  plate), a five-band master EQ in the organ's voicing, eight
  temperaments on any key, the pitch of A, and the tremulant's rate and depth.
- **MIDI learn from the console.** Press a stop or a piston, then move a key,
  pedal or controller.
- **Real-time safe.** Nothing on the audio thread allocates, locks or waits,
  and tests prove it, including while an organ is changed or a demonstration
  plays.

## Repository layout

| Crate | What it is |
|---|---|
| `crates/caecilia-model` | The organ model and its JSON file; the demonstration organ |
| `crates/caecilia-wind` | Bellows, windchests, trunks and tremulants |
| `crates/caecilia-synth` | The pipe voices |
| `crates/caecilia-engine` | The organ engine: keys over ranks and couplers, chests, swell boxes |
| `crates/caecilia-registration` | Selectors, history, generals, plenum, explanations |
| `crates/caecilia-plugin` | The CLAP/VST3 plugin, its console, and the standalone application |
| `crates/caecilia-tools` | Reference comparison against 1.x and other development tools |

## Building

Caecilia 2.0 builds against Manguebyte by path while both are developed side
by side. Check Manguebyte out at `C:/manguebyte` (the path is set in
`Cargo.toml` under `[workspace.dependencies]`).

```bash
# Every test
cargo test --profile caecilia --workspace

# The CLAP and VST3 bundles, with Manguebyte's bundler
cargo run --release --manifest-path C:/manguebyte/crates/cargo-mangue/Cargo.toml --     mangue bundle -p caecilia-plugin --profile caecilia-release --format clap,vst3

# The standalone application: the console in its own window, on the sound card
cargo run --profile caecilia-release -p caecilia-plugin --features standalone --bin caecilia
```

The standalone accepts `--organ <file.organ.json>`, and `--no-window` to play
from the terminal. It keeps its session in `%APPDATA%\Caecilia\session.txt`.

## License

Caecilia is licensed under the **Apache License, Version 2.0**.
See [LICENSE](LICENSE) and [NOTICE](NOTICE).

    SPDX-License-Identifier: Apache-2.0
    Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

Apache-2.0 was chosen for its explicit patent grant: audio DSP is a field where
third-party patents exist. Without JUCE, the 2.0 binaries are Apache-2.0 too.
Manguebyte is MIT OR Apache-2.0. Full details are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Provenance of the DSP

Every algorithm here is written from public literature and standard
mathematics. **No GPL source (GrandOrgue, Aeolus, or otherwise) was referenced
or copied.** Other instruments are cited only by their observable behaviour
and public documentation. If you believe any part infringes, please open an
issue and it will be addressed.

## Contributing

Contributions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md).
Everyone participating is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md). By contributing, you agree that your
contributions are licensed under Apache-2.0, per section 5 of the licence.
