<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Contributing to Caecilia

Thanks for wanting to help build a pipe organ.

Contributions of any size are welcome, from a typo fix to a whole subsystem, and
many of them need no Rust at all. Not sure where to start, or whether an idea
fits? Open an issue and ask, in any language.

## Helping Caecilia helps Manguebyte

Caecilia's development is tied to the
[Manguebyte Audio Framework](https://github.com/alessonqueirozdev-hub/manguebyte),
which it uses as its core. Many of Caecilia's improvements are made there, such as
new DSP blocks, plugin formats, audio devices and console widgets, and every program
built on the framework gains from them. So contributing to Caecilia is also
contributing to Manguebyte, and a bug you report here may be fixed there.

## Ways to help

| You are... | You could... | Start here |
|---|---|---|
| An organist or organ lover | Say what doesn't sound or feel like an organ; suggest registrations | [Sound report](https://github.com/alessonqueirozdev-hub/Caecilia/issues/new?template=sound_report.yml) |
| A voicer or sound designer | Write an organ file: a Baroque positive, a Romantic swell, your parish organ | [The organ file](docs/ORGAN_FILE.md), [organ proposal](https://github.com/alessonqueirozdev-hub/Caecilia/issues/new?template=organ_file.yml) |
| A musician with a DAW | Try the plugins in your host and on your hardware, and say what happened | [Bug report](https://github.com/alessonqueirozdev-hub/Caecilia/issues/new?template=bug_report.yml) |
| A Rust programmer | Take on a good first issue or an item from the roadmap | [Good first issues](https://github.com/alessonqueirozdev-hub/Caecilia/labels/good%20first%20issue), [roadmap](docs/ROADMAP.md) |
| A DSP person | Make the pipes, the wind or the room sound more real | [`caecilia-synth`](crates/caecilia-synth), [`caecilia-wind`](crates/caecilia-wind) |
| A writer | Bring a 1.x document up to date; improve an explanation | [Documentation index](docs/README.md) |

Before starting something big, please open an issue so the design can be discussed
first. That way nobody's work is wasted.

## Set up

You need [Rust](https://rustup.rs) 1.85 or newer, with `rustfmt` and `clippy`.

Caecilia and its framework, [Manguebyte](https://github.com/alessonqueirozdev-hub/manguebyte),
change together, so Caecilia depends on it by relative path. Clone both into the
same folder:

```bash
git clone https://github.com/alessonqueirozdev-hub/manguebyte.git
git clone https://github.com/YOUR-USERNAME/Caecilia.git
cd Caecilia
cargo test --profile caecilia --workspace
```

Your folders should look like this:

```text
some-folder/
├── manguebyte/
└── Caecilia/
```

The `caecilia` profile is optimised because tests render seconds of a full organ.
The first build compiles the whole framework, which takes a while and a few
gigabytes of disk.

If Caecilia stops compiling after a `git pull`, update your Manguebyte clone too.

Windows is the platform `main` is built and tested on today. Manguebyte only opens
windows on Windows so far, so the console can't run on Linux or macOS yet.

## Everyday commands

```bash
# Play the organ: the console in its own window, on your sound card
cargo run --profile caecilia-release -p caecilia-plugin --features standalone --bin caecilia

# Every test
cargo test --profile caecilia --workspace

# One crate, or one test file
cargo test --profile caecilia -p caecilia-wind
cargo test --profile caecilia -p caecilia-plugin --test console_demos

# What CI checks before a merge
cargo fmt --all -- --check
cargo clippy --profile caecilia --workspace --all-targets --features caecilia-plugin/standalone -- -D warnings

# The CLAP and VST3 bundles, with Manguebyte's bundler
cargo run --release --manifest-path ../manguebyte/crates/cargo-mangue/Cargo.toml -- mangue bundle -p caecilia-plugin --profile caecilia-release --format clap,vst3

# The Windows installer
cargo run --profile caecilia-release -p caecilia-installer --bin caecilia-dist -- build
```

The standalone accepts `--organ <file.organ.json>`, and `--no-window` to play from
the terminal. It keeps its session in `%APPDATA%\Caecilia`, and any `.mid` you put
in the `Demos` folder there joins the demonstrations.

## Finding your way around

| To change... | Look in... |
|---|---|
| How a pipe sounds | `crates/caecilia-synth`, and the recipes in `crates/caecilia-model/src/recipes.rs` |
| The wind and the tremulant | `crates/caecilia-wind` |
| The stops of the built-in organ | `crates/caecilia-model/src/demo.rs` |
| Keys, couplers and swell boxes | `crates/caecilia-engine` |
| Selectors, history and pistons | `crates/caecilia-registration` |
| The console | `crates/caecilia-plugin/src/ui` |
| The room and the master EQ | `crates/caecilia-plugin/src/sound.rs` |
| Demonstrations | `crates/caecilia-plugin/src/demo_files.rs`, and `assets/demos` |

[`docs/PORTING.md`](docs/PORTING.md) says what each crate is and where it came from,
and the [documentation index](docs/README.md) says which documents are current.

Something missing in the framework itself (audio devices, widgets, DSP blocks)
belongs in [Manguebyte](https://github.com/alessonqueirozdev-hub/manguebyte/blob/main/CONTRIBUTING.md),
which has its own rules. If you're not sure which side a problem is on, open the
issue here and we'll find out together.

## The rules that matter most

### 1. Nothing on the audio thread allocates, locks or waits

`Plugin::process` and everything it reaches: no `Box`, `Vec` growth, `String`,
`format!`, `Mutex::lock`, I/O or logging. Memory is reserved in `prepare`. What
crosses threads goes through atomics, lock-free rings (`manguebyte_core::spsc`) or
the organ slot's hand-over. A change that reaches the audio thread comes with a
test that proves it, in the manner of `crates/caecilia-plugin/tests/realtime.rs`
(a counting allocator around `process`).

### 2. Tests stay silent

No test opens a real audio interface.

### 3. The parameters and the saved state are a contract

A host keys its automation on parameter ids and restores projects from the saved
state. Changing either breaks people's work: add, don't rename, or migrate.

### 4. No copyleft source

Never bring GPL, LGPL, AGPL or MPL source into the repository, including JUCE,
GrandOrgue and Aeolus, and don't read it to port it. Work from published
mathematics and your own measurements, and say in the pull request where an
algorithm came from.

## Testing

Please add a test with your change. Each crate has its own `tests/`. The console is
tested headless through a real plugin wrapper
(`crates/caecilia-plugin/tests/console_*.rs`): the tests read what it says to a
screen reader and press what a player would press.

A test that reproduces a bug is a useful pull request on its own, even without the
fix.

Tests can't tell whether an organ sounds good, so for a change to the sound, please
also listen, and say in the pull request what you listened for and on what.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make one change at a time, with its test.
3. Run the commands CI checks, listed above.
4. Open a pull request into `main` and fill in the template.

Commit subjects name the crate and say what changed (`engine: ...`, `plugin: ...`,
`docs: ...`). The body explains why, since the diff already shows what.

Code, comments, documents, commits and pull requests are in English, so the project
stays readable to everyone; imperfect English is fine. Issues may be in any language.

Every source file starts with this header:

```rust
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
```

## Licensing

Caecilia is under the [Apache License 2.0](LICENSE). By opening a pull request, you
agree that your contribution is licensed the same way (section 5 of the licence).
No separate CLA is required.

## Community

Everyone participating follows the [Code of Conduct](CODE_OF_CONDUCT.md). Security
issues go through a private report: see [SECURITY.md](SECURITY.md).
