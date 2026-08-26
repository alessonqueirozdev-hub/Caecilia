<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Contributing to Caecilia

Thanks for wanting to help build a pipe organ.

Caecilia is open source under the [Apache License 2.0](LICENSE). Contributions
of any size are welcome — a typo fix, a failing test that documents a bug, a
DSP optimisation, a whole subsystem. By opening a pull request you agree that
your contribution is licensed under Apache-2.0 (section 5 of the licence); no
separate CLA is required.

All project wording — commits, branches, PRs, comments, docs, identifiers — is
in **English**, so the codebase stays readable to contributors worldwide.
Issues and discussions may be in any language you're comfortable with.

Everyone participating is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

---

## Before you start

Two documents will save you time:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the layering, the audio-thread
  seam, and how MIDI, registration and wind are meant to reach the engine.
- The open **engineering audit** — a forensic review of the current state, with
  45 findings, each carrying file, function, evidence and a suggested fix. Many
  of them are self-contained and make good first contributions. If you're
  looking for somewhere to start, that's the list.

A note on honesty: this project is a **playable alpha**. Large parts of the tree
(the wind model, historical temperaments, semantic registration, the physical
voice tier, the control API) are implemented and unit-tested but **not yet wired
into the plugin**. Doxygen comments in a few headers still describe the intended
behaviour in the present tense. Don't assume a subsystem is live because its
tests pass — check that something actually calls it.

---

## Building

Requirements: **CMake ≥ 3.22** and a **C++20** compiler (MSVC 2022, Clang 15+,
GCC 12+). JUCE 8 and Catch2 are fetched automatically, and only when needed.

```bash
# Pure core, no JUCE fetched at all — the fastest loop for DSP work
cmake -S . -B build-core -DCAECILIA_CORE_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# Full build: core + JUCE plugin + tests
cmake -S . -B build
cmake --build build
```

Linux also needs the JUCE system dependencies (ALSA, X11, FreeType, GTK and
WebKitGTK); see the `linux` job in
[`.github/workflows/build.yml`](.github/workflows/build.yml) for the exact
`apt-get` line.

---

## The three rules that matter most

Everything else in this document is convention. These three are correctness.

### 1. The core is JUCE-free

`caecilia_core` links **only the C++ standard library**. Only the `plugin` and
`ui` modules may include JUCE, and no JUCE type may cross the `AudioEngine`
seam — the audio types that cross are `core::AudioBlock` and `EngineCommand`,
and nothing else. `-DCAECILIA_CORE_ONLY=ON` must always configure and build.

### 2. Nothing on an audio path may allocate, lock, log or throw

Any function reachable from `AudioEngine::processBlock` — every `process()`,
`render()`, `renderAdd()`, `step()`, and every command handler — is **hard
real-time**. That means no `new`/`delete`/`malloc`, no growing containers, no
`std::string`, no mutex, no file or console I/O, no system calls, and no
exceptions. Mark such functions `noexcept` and say so in the doc comment.

Allocation happens in exactly one place: `prepare()`. If you need memory on the
audio thread, reserve it in `prepare()` instead.

Watch the subtler cases too. `std::pow`, `std::exp` and `std::log` are
deterministic but expensive; a few hundred of them in a note-on is already a
real problem (this is finding `RT-001` in the audit). Prefer precomputing
off-thread.

### 3. Cross-thread state moves through the sanctioned channels only

- **Message thread → audio thread**: push an `EngineCommand` onto the SPSC ring.
  Exactly one producer.
- **Audio thread → UI**: publish into the double-buffered snapshot
  (`StateMirror` / `MeterSnapshot`). Never touch a `juce::Component` from the
  audio thread.

Taking the JUCE callback lock from the message thread is tolerated for short,
pointer-swapping hand-offs, but do the expensive construction *before* you take
it and the destruction *after* you release it.

---

## Testing

Please add a test with your change. The suite is Catch2 and links only
`caecilia_core`, so DSP, tuning, registration, model and engine logic can all be
exercised headless, off the audio thread.

```bash
ctest --test-dir build-core --output-on-failure
```

Especially valuable, because the current suite doesn't cover them:

- **Buffer-size independence** — render the same input at 16/64/512/2048 and
  assert the outputs match.
- **Sample-rate independence** — render at 44.1/48/96 kHz and compare measured
  pitch and envelope.
- **Round-trips** — save state, restore it, assert equality.
- **Convergence** — after a random storm of note-on/note-off, active voices must
  return to zero.

A test that reproduces a bug is a genuinely useful pull request on its own, even
without the fix.

---

## Commit convention

[Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <imperative subject, ≤ 72 chars>

<body: explain WHY, wrapped ~72 cols>

<footer: BREAKING CHANGE / Fixes #123>
```

| Type       | Use for                                                   |
|------------|-----------------------------------------------------------|
| `feat`     | A new capability or user-visible feature                  |
| `fix`      | A bug fix                                                 |
| `docs`     | Documentation only                                        |
| `style`    | Formatting / whitespace, no behaviour change              |
| `refactor` | Neither fixes a bug nor adds a feature                    |
| `perf`     | A performance improvement                                 |
| `test`     | Adding or correcting tests                                |
| `build`    | Build system, CMake, or dependency changes                |
| `ci`       | CI configuration and scripts                              |
| `chore`    | Routine maintenance that doesn't fit the above            |

**Scope** is the module you touched: `core`, `engine`, `synthesis`, `wind`,
`model`, `dsp`, `tuning`, `midi`, `registration`, `control`, `plugin`, `ui`, or
`build` for cross-cutting build changes.

```
fix(dsp): size the limiter ring for the maximum look-ahead
feat(midi): route channel to division through ChannelToDivisionMap
perf(synthesis): replace per-sample sin() with an incremental oscillator
test(engine): assert voice count converges after a note storm
```

Subject in the imperative ("add", "fix", "guard" — not "added"/"adds"), ≤ 72
characters, no trailing period, lowercase after the prefix. The body explains
**why**; the diff already shows what.

---

## Branch and pull-request flow

- `main` is the protected mainline. Never commit to it directly.
- Branch per unit of work: `type/short-topic` — e.g. `fix/limiter-ring-size`,
  `feat/mixture-breaks`, `docs/architecture`.
- Open a PR into `main`. Say **what** changed and **why**, link the issue, and
  explicitly call out any real-time-safety or licence-boundary implication.
- Keep PRs focused. Several small ones beat one sprawling change — they get
  reviewed faster and revert cleanly.
- Green CI is required: build on Windows, macOS and Linux, plus the test suite.

---

## Formatting and licence headers

- Code style is enforced by [`.clang-format`](.clang-format) (LLVM-based, C++20).
  Run `clang-format` before committing.
- Every source, header and CMake file starts with the two-line SPDX header:

```cpp
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
```

  (`#` for CMake/YAML, `<!-- -->` for Markdown and HTML.) Please don't add a
  personal copyright line to an existing file — Apache-2.0 attributes
  contributors collectively, and the git history is the real record. New files
  you author entirely are yours to attribute as you prefer, as long as the SPDX
  line stays Apache-2.0.

- **Never** introduce GPL, LGPL or AGPL source into `caecilia_core`. It would
  relicense the whole library. If you're porting an algorithm, port it from the
  published mathematics, not from someone else's implementation, and say in the
  PR where it came from.

---

## Reporting bugs

Please include: your OS, host/DAW and version, sample rate, buffer size,
registration in use, and what you expected versus what happened. Audio bugs are
enormously easier to fix with a short WAV or a screenshot of a spectrogram.

Security-relevant issues: please open a private security advisory on GitHub
rather than a public issue.
