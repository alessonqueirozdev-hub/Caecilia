# `caecilia::wind` — Wind supply simulation

The wind module is Caecilia's realism moat. It makes the shared wind supply an
**audio-rate control signal, not an LFO**: a reservoir/bellows pressure ODE is
integrated once per block under the real polyphony demand, published as an
immutable per-block snapshot that every voice reads sample-accurately. Both
GrandOrgue (dead looped samples, no wind response) and Aeolus (a single global
tremulant) lack this; here pressure sag and tremulant motion drive excitation,
pitch, level and timbre through per-tonal-family response curves.

It is part of the pure, JUCE-free `caecilia_core` static library and implements
the core contract `caecilia::core::IWindSupply`.

> **Status: wired.** `CaeciliaAudioProcessor` owns a `WindModel`, builds it from
> the organ with `wind::configFromOrgan`, and binds it with
> `AudioEngine::setWindSupply`. `AudioEngine::stepWind()` books the block's demand
> and integrates the ODE before the voices read pressure, and `AdditiveVoice`
> re-points its coupling at the adopted rank's chest and family curve. Everything
> below is real in the shipping plugin.
>
> Note the fourth break, which the other three hid: `RankVoicing::chest` was never
> filled in, so every voice read chest 0 no matter which chest actually fed its
> rank — the Récit's tremulant would have shaken nothing and no division would have
> sagged under its own load.

## Design in one breath

- **Bellows** integrates a regulated pressure ODE. Its equilibrium **sags below
  nominal in proportion to instantaneous flow demand**; how it gets there depends
  on whether the top plate has been given mass.
  - Without (`plateResonanceHz == 0`, the default): a first-order relaxation, an
    exact exponential step, monotone — it approaches the new pressure and stops.
  - With (the demo organ asks for 4.5 Hz): a second-order plate on its air spring,
    so a chord landing on the reservoir drops it **past** the new pressure and
    back, a few times, before it settles. Measured on the demo organ, a full chord
    sags it 13.2 Pa and the plate passes that by 2.7 on the way down. That bounce
    is what an organist means by the wind breathing, and a first-order model cannot
    produce it at any setting.
  - Both are solved in closed form over the block, so the step is unconditionally
    stable at any buffer size a host hands us. `compliance` sets the first-order
    rate and has no effect once the plate is enabled: there, the speed is
    `plateResonanceHz` and `plateDamping`, which is what a voicer would state.
- **Windchest** distributes a bellows' wind to a group of pipes, imposing an
  extra local trunk drop from its own demand and holding the per-block sag
  trajectory that callers sample.
- **Tremulant** modulates the **wind pressure itself** (not output gain), so
  downstream it yields coupled AM + FM + timbral movement. Phase advances once
  per block; per-frame deltas are evaluated analytically for sample accuracy.
- **WindModel** owns the reservoirs, chests, tremulants and rank→chest routing,
  and *is* the `IWindSupply`. `step()` integrates the ODE once per block and
  publishes the read-only snapshot — computed once, then read-only, hence
  race-free.
- **WindResponseCurve** maps a normalised pressure deviation onto pitch (cents),
  level (dB), brightness and attack, on a different curve per `TonalFamily`
  (reeds swing hardest, flutes least).
- **WindTap** is a trivially-copyable `(supply, chest)` binding a voice keeps so
  it can sample pressure without repeating the chest lookup.

## Per-block lifecycle

```text
  (audio thread, once per block)
  voices ── registerDemand(chest, flow) ─▶ WindModel        [accumulates for NEXT block]
                                              │
  engine ── step(numFrames) ────────────────▶│  1. sum chest demand into bellows
                                              │  2. integrate each Bellows ODE   -> sag
                                              │  3. advance each Tremulant phase
                                              │  4. latch each Windchest trajectory
                                              ▼
  voices ── pressureAt / deviationAt(frame) ─▶ immutable snapshot (sag + tremulant)
```

`registerDemand()` only accumulates into a writer-owned tally that feeds the
**next** block, so it never disturbs the snapshot voices are currently reading.

## Real-time contract

`prepare()` and `configure()` are the only methods that allocate and run off the
audio thread. `step()`, `reset()`, `setTremulantEnabled()` and every
`IWindSupply` accessor are `noexcept`, allocation-free and lock-free.

## Public API

| Type | Header | Role |
| --- | --- | --- |
| `WindModel` | `WindModel.h` | Owns the wind graph; implements `core::IWindSupply`; `prepare` / `configure` / `step`. |
| `Bellows` | `Bellows.h` | Single reservoir; regulated pressure ODE with demand-driven sag. |
| `Windchest` | `Windchest.h` | Distribution node; local trunk drop; per-block sag trajectory. |
| `Tremulant` | `Tremulant.h` | Wind-pressure modulation (sine / triangle / asymmetric). |
| `WindResponseCurve` | `WindResponseCurve.h` | Per-`TonalFamily` deviation → pitch/level/brightness/attack. |
| `WindTap` | `WindTap.h` | Non-owning per-voice `(supply, chest)` sampling handle. |
| `WindModelConfig` &c. | `WindTypes.h` | Off-thread configuration PODs + `makeSingleChestConfig()`. |

## Quick start

```cpp
using namespace caecilia;

wind::WindModel windModel;
windModel.prepare(/*sampleRate*/ 48000.0, /*maxBlockFrames*/ 512);
windModel.configure(wind::makeSingleChestConfig(/*nominalPa*/ 735.0f));

// Per block, on the audio thread:
windModel.registerDemand(core::WindchestId{0}, sumOfSoundingPipeFlow);
windModel.step(numFrames);

// A voice samples its chest sample-accurately:
wind::WindTap tap = windModel.tapFor(core::WindchestId{0});
const float pressurePa = tap.pressureAt(frameInBlock);
const float deviation  = tap.deviationAt(frameInBlock);
const auto  curve      = wind::defaultResponseFor(core::TonalFamily::Reed);
const float pitchCents  = curve.pitchCents(deviation);
```

## Roadmap notes

Scaffold for **v0.2 — Wind model + tremulant realism**. The integrator, sag,
distribution and tremulant modulation are complete *within this module*; what is
still missing in the plugin is the engine that steps them. Deeper physics is
marked in-code:

- `TODO(phase2)`: bellows plate inertance (second-order shake/bounce),
  nonlinear curtain-valve regulation, passive leak, tempo-aware tremulant ramp.
- `TODO(phase3)`: weak pipe-to-pipe frequency pulling on a shared chest.
- `TODO(phase5)`: replace hand-tuned response curves with per-rank values from
  the offline wind-sensitivity analysis toolchain.
