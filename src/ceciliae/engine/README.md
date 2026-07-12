<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# `ceciliae::core::engine` — real-time voice engine

The scheduler and voice-lifecycle layer of the pure `ceciliae_core` library. It
owns the **hard real-time contract**: a fixed-capacity, lock-free voice pool, a
per-block render scheduler with graceful CPU-budget degradation, and the single
narrow **`AudioEngine` seam** that the JUCE plugin drives. No JUCE type appears
in any signature, and nothing on the audio thread allocates, locks, or throws.

This module lives inside `ceciliae_core`. It depends **only** on the core
headers (`AudioBlock`, `EngineTypes`, `IVoice`, `IWindSupply`, `ITuning`,
`IReverb`) and the C++ standard library. Concrete wind / DSP / synthesis / model
implementations are reached exclusively through those core interfaces, so strict
layering is preserved.

### Namespace — a recorded exception

Every other module maps its directory to a single sibling sub-namespace
(`dsp` → `ceciliae::dsp`, `wind` → `ceciliae::wind`, and so on). The engine
module is the **one intentional exception**: its symbols live in
`ceciliae::core::engine`, nested under `core`. The engine *is* the real-time
seam of the pure core library — it owns no domain concepts of its own, only the
scheduling contract wrapped around the `core` interfaces — so nesting it under
`core` reads as "the core's engine" rather than as a peer domain module. The
nesting is deliberate; it does **not** imply that `core` depends on `engine`
(the dependency runs the other way, engine → core only).

## The audio callback, in one place

`AudioEngine::processBlock(AudioBlock&)` is the whole callback body:

1. **Drain commands** — pop every `EngineCommand` off the SPSC ring (note
   on/off, panic, reverb params, and the stubs for stop-delta / tremulant /
   wind / temperament) and apply it at the block boundary.
2. **Step wind** — advance the reservoir ODE once (stub until the `wind`
   module lands) so the read-only snapshot reflects sag + tremulant.
3. **Zero the per-windchest buses**, build the immutable `RenderContext`.
4. **Refresh the pool** — cull finished voices, sort survivors by `EngineKind`
   into contiguous SoA runs.
5. **Render** every active voice into its chest bus under the `DeadlineBudget`.
6. **Mix** the chests down, **apply** the master chain (reverb), advance the
   frame clock.
7. **Publish** a double-buffered `MeterSnapshot` for the UI to poll.

## Public API

| Type | Role |
| --- | --- |
| `AudioEngine` (alias `OrganEngine`) | The seam: `prepare` / `bindVoices` / `setWindSupply` / `setTuning` / `setMasterReverb` off-thread; `processBlock` / `commandQueue` / `latestMeters` on-thread. |
| `VoicePool<N>` | Fixed-capacity pool: `acquire` / `release` / `resolve` with generation-tagged `VoiceHandle`s that reject stale references after a steal; `refresh` culls + groups by kind; `batch` / `view` expose SoA runs; `chooseVictim` applies the steal policy. |
| `VoiceScheduler` | Pulls the active set batch-by-batch and renders into the chest buses, shedding under budget pressure. |
| `RenderContext` | Immutable per-block bundle: timing, `IWindSupply` / `ITuning` snapshots, `DeadlineBudget`, and the pre-zeroed chest buses. |
| `EngineCommand` | Trivially-copyable tagged union carried over the ring; build with the `make*` factories. |
| `SpscRing<T, Cap>` | Wait-free single-producer/single-consumer ring — the only inbound-mutation channel. |
| `VoiceHandle` / `DeadlineBudget` / `StealPolicy` / `MeterSnapshot` / `VoiceBatchView` | Supporting RT-safe value types. |

## Real-time rules honored here

- **Allocation only in `prepare()`** — the voice pool, command ring, chest buses
  and meter buffers are all sized once. `processBlock` and every method it calls
  are `noexcept`, allocation-free and lock-free.
- **Generation-tagged handles** — a stolen slot bumps its generation, so a
  former owner's `VoiceHandle` fails `resolve` instead of addressing a foreign
  note.
- **SPSC in, snapshot out** — engine state is mutated only via the command ring
  and read only via the double-buffered meter snapshot; the audio thread never
  shares mutable state with another thread.
- **Graceful degradation** — the `DeadlineBudget` sheds the cheapest voices (and,
  in a later phase, demotes tiers / steals the quietest) so a worst-case tutti
  thins subtly rather than xrunning.

## Roadmap touch-points

Stubs are marked `// TODO(phaseN)`:

- **v0.1** — bind real sine/additive voices; prove the allocation-free callback.
- **v0.2** — drive `WindModel::step`, forward tremulant/wind commands, fill wind
  meters.
- **v0.6** — tier demotion + quietest-voice stealing; SIMD batch kernels over the
  per-`EngineKind` runs.
- **v0.7** — apply registration `StateDelta`s as click-free, wind-modeled
  crossfades.
