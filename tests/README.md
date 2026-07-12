<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Ceciliae tests

Headless [Catch2](https://github.com/catchorg/Catch2) unit tests for the pure
`ceciliae_core` static library. The suite links **only** `ceciliae::core` plus
Catch2 — no JUCE, no plugin host, no audio device — so every DSP, tuning,
registration, model and wind decision is exercised entirely off the audio thread,
exactly as the real-time contract intends.

Because all pure modules compile into the single `ceciliae_core` target, one test
executable reaches every module's implementation.

## Building & running

The suite is gated by the top-level `CECILIAE_BUILD_TESTS` option (ON by default),
which also fetches Catch2 via `FetchContent`.

```sh
cmake -B build -DCECILIAE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`CECILIAE_CORE_ONLY=ON` keeps JUCE out of the configure entirely while still
building and running these tests — the fastest inner loop for pure-core work.

## Layout

| File | Module | What it pins |
| --- | --- | --- |
| `core/CoreTypesTest.cpp` | `core` | Exact-rational `Footage` reduction / mutation vs octave classification; `SpscRing` FIFO / full / empty; version smoke. |
| `dsp/ReverbTest.cpp` | `dsp` | `FdnReverb` numerical **stability** (finite, bounded) and **decay** toward silence; `mix=0` dry passthrough; `reset()` clears the tail. |
| `dsp/ResamplerTest.cpp` | `dsp` | Kaiser-windowed-sinc **accuracy**: unity DC gain, exact integer taps, exact linear-ramp reproduction, near-transparent sinusoid reconstruction that beats linear, end-of-source zero-fill. |
| `tuning/TemperamentTest.cpp` | `tuning` | Equal-temperament reference pitches; exact `Footage` frequency scaling (a 2 2/3' quint sounds a true twelfth); A4 anchoring and pure octaves across every built-in temperament; quarter-comma meantone's near-pure major third. |
| `registration/RegistrationTest.cpp` | `registration` | `StopSet` set **algebra**; the shared `SelectorParser` grammar resolving intent **by family** / pitch / division; command-sourced **undo/redo** with `StateDelta` as the exact inverse transition. |
| `model/OrganLoaderTest.cpp` | `model` | `OrganLoader::compile` dense `id == index`, resolved semantic metadata & exact footage, coupler name resolution, the rank→`PipeId` activation mapping, rejection of dangling references, and the not-yet-implemented `parse`/`serialize` stubs failing loudly. |
| `wind/WindModelTest.cpp` | `wind` | Pressure **sag under load** (steady-state magnitude, monotonic in demand, recovery); baseline snapshot at nominal; tremulant as wind-pressure modulation (not an output-gain wobble). |
| `support/TestOrgan.h` | — | Shared fixture: a deterministic two-manual organ (fixed stop ids) compiled through the real loader, used by the registration and model tests. |

## Conventions

- One `TEST_CASE` per behaviour, tagged by module (`[dsp]`, `[wind]`, …) so a
  subset runs with e.g. `ctest -R '\[reverb\]'` or `./ceciliae_tests "[wind]"`.
- Assertions are meaningful and self-contained: they encode the *contract*
  (exact rationals, energy decay, sag equilibrium), not incidental scaffold
  values, so they stay valid as the `// TODO(phaseN)` implementations deepen.
- Floating-point comparisons use `Catch::Approx` with an explicit margin/epsilon.
- Fixtures build real objects through the public interfaces only — no friend
  access, no private pokes — mirroring how the plugin/UI shells consume the core.
