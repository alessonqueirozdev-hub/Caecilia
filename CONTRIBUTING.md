<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Contributing to Caecilia

Thanks for wanting to help build a pipe organ.

Caecilia is open source under the [Apache License 2.0](LICENSE). Contributions
of any size are welcome: a typo fix, a failing test that documents a bug, a
DSP optimisation, a whole subsystem. By opening a pull request you agree that
your contribution is licensed under Apache-2.0 (section 5 of the licence); no
separate CLA is required.

All project wording (commits, branches, PRs, comments, docs, identifiers) is
in **English**, so the codebase stays readable to contributors worldwide.
Issues and discussions may be in any language you're comfortable with.

Everyone participating is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

---

## Before you start

- [`docs/PORTING.md`](docs/PORTING.md) says what each crate is, where it came
  from in 1.x, and what 2.0 still has to do.
- [`docs/ORGAN_FILE.md`](docs/ORGAN_FILE.md) describes the organ file.
- The other documents in `docs/` were written for 1.x, the C++ instrument.
  The ideas hold (the wind model, the synthesis, registration, tuning). Paths
  under `src/` in them refer to 1.x, kept at the tag `v1-cpp-final`.

## Building

Caecilia builds against [Manguebyte](https://github.com/alessonqueirozdev-hub/manguebyte)
by path while both are developed side by side: check Manguebyte out at
`C:/manguebyte`.

```bash
cargo test --profile caecilia --workspace

# The CLAP and VST3 bundles, with Manguebyte's bundler
cargo run --release --manifest-path C:/manguebyte/crates/cargo-mangue/Cargo.toml -- \
    mangue bundle -p caecilia-plugin --profile caecilia-release --format clap,vst3

# The standalone application
cargo run --profile caecilia-release -p caecilia-plugin --features standalone --bin caecilia
```

---

## The rules that matter most

### 1. Nothing on the audio thread allocates, locks or waits

`Plugin::process` and everything it reaches: no `Box`, `Vec` growth, `String`,
`format!`, `Mutex::lock`, I/O or logging. Memory is reserved in `prepare`.
What crosses threads goes through atomics, lock-free rings
(`manguebyte_core::spsc`) or the organ slot's hand-over. A change that reaches
the audio thread comes with a test that proves it, in the manner of
`crates/caecilia-plugin/tests/realtime.rs` (a counting allocator around
`process`).

### 2. Tests that a real audio device hears are silent

No test opens a real audio interface, and none plays more than silence
through one.

### 3. The parameters and the saved state are a contract

A host keys its automation on parameter ids and restores projects from the
saved state. Changing either breaks users' work: add, don't rename, or
migrate.

### 4. No copyleft source

Never bring GPL, LGPL, AGPL or MPL source into the repository, and don't read
it to port it. Port an algorithm from the published mathematics, and say in
the PR where it came from.

---

## Testing

Please add a test with your change. Each crate has its own `tests/`; the
console is tested headless through a real plugin wrapper
(`crates/caecilia-plugin/tests/console_*.rs`), reading what it says to a
screen reader and pressing what a player would press.

A test that reproduces a bug is a genuinely useful pull request on its own,
even without the fix.

## Formatting and lints

`cargo fmt --all` and `cargo clippy --workspace --all-targets -- -D warnings`
must be clean; CI checks both. Every source file starts with the two-line SPDX
header:

```rust
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
```

## Commits and pull requests

A commit subject says what changed, in the imperative or as a statement, with
the crate as a prefix (`plugin: ...`, `engine: ...`, `docs: ...`). The body
explains **why**; the diff already shows what. Open a pull request into
`main`, and say what you listened for and on what.

## Reporting bugs

Please include your OS, host/DAW and version, sample rate, buffer size, the
organ and registration in use, and what you expected versus what happened. A
short WAV or a screenshot of a spectrogram makes an audio bug far easier to
fix.

Security-relevant issues: please open a private security advisory on GitHub
rather than a public issue.
