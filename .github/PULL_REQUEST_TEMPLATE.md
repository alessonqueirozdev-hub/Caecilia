<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

## What and why

<!-- What changed, and the reason it needed to change. The diff already shows
     what; this is where the "why" lives. -->

Fixes #

## How it was verified

<!-- Tests added, manual checks, hosts tried, sample rates and buffer sizes.
     For audio changes, say what you listened for and on what. -->

- [ ] `cargo test --profile caecilia --workspace` passes
- [ ] `cargo fmt --all` and `cargo clippy --workspace --all-targets -- -D warnings` are clean
- [ ] Tested at more than one buffer size and sample rate

## Real-time safety

<!-- Delete this section if the change touches nothing reachable from
     Plugin::process. -->

- [ ] No allocation, lock, wait, I/O or logging added to the audio thread
- [ ] Any new memory is reserved in `prepare`
- [ ] A test proves it (see `crates/caecilia-plugin/tests/realtime.rs`)

## Licensing

- [ ] No GPL/LGPL/AGPL/MPL source was copied or read to write this
- [ ] New files carry the two-line Apache-2.0 SPDX header

## Host-visible changes

<!-- Delete if none. Parameter ids and the saved state are a public contract:
     changing them breaks users' existing projects. -->

- [ ] Parameter ids unchanged (or a migration path is included)
- [ ] Saved state unchanged (or versioned with a migration path)
- [ ] Reported latency still matches the real delay
