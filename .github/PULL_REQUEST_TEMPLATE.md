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

- [ ] `ctest --test-dir build-core --output-on-failure` passes
- [ ] Tested at more than one buffer size
- [ ] Tested at more than one sample rate

## Real-time safety

<!-- Delete this section if the change touches nothing reachable from
     AudioEngine::processBlock. -->

- [ ] No allocation, lock, logging, I/O or exception added to an audio path
- [ ] New audio-path functions are marked `noexcept`
- [ ] Any new memory is reserved in `prepare()`
- [ ] Expensive transcendentals (`pow`, `exp`, `log`, `sin`) are not called
      per-sample or per-note on the audio thread — or the cost is justified below

## Layering and licensing

- [ ] `caecilia_core` still builds with `-DCAECILIA_CORE_ONLY=ON` (no JUCE)
- [ ] No JUCE type crosses the `AudioEngine` seam
- [ ] No GPL/LGPL/AGPL source was copied into this repository
- [ ] New files carry the two-line Apache-2.0 SPDX header

## Host-visible changes

<!-- Delete if none. Parameter IDs and the saved-state format are a public
     contract: changing them breaks users' existing projects. -->

- [ ] Parameter IDs unchanged (or a migration path is included)
- [ ] Saved-state format unchanged (or versioned with a migration path)
- [ ] Reported latency (`setLatencySamples`) still matches the real delay
