<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia — Wind Model

The shared wind supply is the single realism moat neither GrandOrgue nor Aeolus
reaches. This document specifies the reservoir/bellows ODE, per-windchest
distribution and sag, the tremulant as pressure modulation, the per-block
immutable `WindState` snapshot, and how every voice reads it race-free.

> Scope: complements [`ARCHITECTURE.md`](ARCHITECTURE.md) §4 and
> [`SYNTHESIS.md`](SYNTHESIS.md) §6 (how voices consume the wind). The read-only
> query surface is the core contract `core::IWindSupply`.
>
> Status: **connected.** The processor owns a `WindModel`, compiles it from the
> loaded organ's own windchests (`wind::configFromOrgan`), and hands it to the
> engine; `AudioEngine::stepWind()` registers the block's demand and integrates the
> ODE before any voice reads pressure; and a voice re-points its wind coupling at
> its rank's chest and family curve when it adopts a rank. Pressure sags under a
> chord and recovers when it is released, and the tremulant shakes the chests that
> have one.
>
> Four separate breaks stood between this document and the instrument, each
> invisible on its own: the supply was never bound, the ODE was never stepped,
> `SetTremulant` was an unhandled case, and a rank's voicing never recorded which
> chest fed it — so even with the first three fixed, every voice would have read
> chest 0. `tests/wind/OrganWindTest.cpp` guards all four.
>
> Demand is per PIPE, not per voice: flow scales with the wavelength a pipe sounds,
> so a rank's footage and the note both count. Measured on this organ, eight bass
> notes on the 16' sag the reservoir eighty times as far as eight treble notes on
> the 1 3/5'; a full-organ chord across the compass is 168 pipes and 1.4%.
>
> Still open: the demand is booked entirely on chest 0. Every chest shares one
> reservoir, so the sag that matters reaches all of them regardless; what chest 0
> additionally takes is its own trunk drop, about seven percent of the total at the
> default conductances. Per-chest attribution needs the engine to carry each voice's
> chest into the tally alongside its flow.

---

## 1. The core claim: wind is an audio-rate control signal, not an LFO

A real organ's pipes share a finite air supply. Draw more pipes and the reservoir
**sags**; the tremulant wobbles the *pressure feeding the pipes*, not their output
level. Because pressure drives the pipe's excitation, a pressure change couples
into pitch, level, brightness, harmonic development and speech **together**.

Caecilia models exactly that: `WindModel` integrates a reservoir ODE **once per
audio block** under the real, summed flow demand of the sounding voices, and
publishes an **immutable per-block snapshot** with sample-accurate pressure taps.
An amplitude LFO on the output can never reproduce this coupling.

---

## 2. Topology

```
  voices ── registerDemand(chest, flow) ──▶ Windchest (distribution node)
                                              │ trunkConductance (local drop)
                                              ▼
                                            Bellows (reservoir ODE, may be shared)
                                              │ feedConductance, compliance
                                              ▼
                                            nominal supply pressure

  Tremulant ── pressureDeltaPa(frame) ──▶ added on top of the chest's sagged pressure
```

`WindModel` owns the reservoirs (`Bellows`), the distribution nodes
(`Windchest`), the `Tremulant`s and the rank→chest routing (`PipeChestBinding`).
Multiple windchests may share one bellows (so drawing the Great sags the Swell
that shares its reservoir — real, audible cross-talk).

---

## 3. The per-block step

Once per block the engine is meant to call `WindModel::step(numFrames)` — today
`AudioEngine::stepWind()` is an empty stub, so this never happens — which:

1. sums each chest's registered demand into its feeding bellows,
2. integrates every bellows' regulated pressure ODE (producing **sag**),
3. advances each tremulant's phase for the block,
4. latches each chest's per-frame sag trajectory.

The result is an immutable snapshot voices **read** through the `IWindSupply`
accessors — computed once, then read-only, hence **race-free by construction.**

```cpp
void WindModel::step(std::size_t numFrames) noexcept; // RT-safe: no alloc/lock/throw
```

`prepare(sampleRate, maxBlockFrames)` and `configure(const WindModelConfig&)` are
the only allocating methods (off the audio thread). `registerDemand(...)` only
accumulates into a writer-owned tally that feeds the **next** block's
integration, so it never disturbs the current snapshot.

---

## 4. The reservoir ODE (`Bellows`)

Each bellows is a first-order **regulated pressure vessel**. Its equilibrium
pressure sags below nominal in proportion to the summed flow demand drawn during
the block, then relaxes toward that equilibrium at a time constant of
`compliance / feedConductance`:

```
  BellowsConfig:
    nominalPressurePa   // target (unloaded) pressure — default 735 Pa (~75 mm H₂O)
    compliance          // reservoir volume compliance (larger → slower settling)
    feedConductance     // regulator/valve stiffness: flow per Pa of deficit
    leak                // passive leak (reserved for a phase-2 refinement)
```

`integrate(dtSeconds)` advances the state with an **exact exponential relaxation
step** — unconditionally stable for any block size — recording the block-start
and block-end pressures (`pressureStartPa()`, `pressureEndPa()`) for downstream
sampling. Stiffer feed → less sag and faster recovery; larger compliance →
slower, more audible settling. `instantaneousSagPa()` exposes the current droop.

Over a block the pressure is treated as a straight line between its start and end
values, which the feeding windchests then sample sample-accurately.

---

## 5. Distribution & local drop (`Windchest`)

A `Windchest` draws from a bellows and imposes an **additional local "trunk"
pressure drop** proportional to its *own* instantaneous demand
(`trunkConductance`), so a heavily-loaded chest sags more than a lightly-loaded
sibling on the same reservoir. `updateBlock(bellowsStartPa, bellowsEndPa,
numFrames)` latches the block's sag trajectory (applying the trunk drop);
`sagPressureAt(frameInBlock)` interpolates the sagged pressure **excluding**
tremulant. Demand accumulates via `registerDemand(flow)` and is cleared each
block by `resetDemand()`.

---

## 6. Tremulant as pressure modulation (`Tremulant`)

The tremulant adds a signed pressure delta (in pascals) to the chest pressure —
it modulates the **driver**, not an output gain:

```cpp
TremulantConfig { rateHz≈5, depthFraction≈0.06, waveform, enabledAtStart };
enum class TremulantWaveform { Sine, Triangle, AsymmetricSaw };
```

`advanceBlock(numFrames, sampleRate)` captures the block-start phase and per-frame
increment; `pressureDeltaPa(frameInBlock)` then evaluates the waveform
analytically at the exact sub-block phase — **sample-accurate modulation with no
per-sample state and no allocation.** `depthFraction` is turned into an absolute
swing against the chest's nominal at `configure(...)`, and engage/disengage
ramps a smoothed depth gain for click-free on/off. Downstream, each voice's
`WindResponseCurve` turns this single pressure movement into the physically
correct coupled AM + FM + timbral tremulant.

---

## 7. The read surface (`IWindSupply` + `WindTap`)

Voices never see the model's internals — only the immutable query surface:

```cpp
float nominalPressurePa(WindchestId) const noexcept;
float pressureAt(WindchestId, frameInBlock) const noexcept;        // incl. sag + tremulant
float pressureDeviation(WindchestId, frameInBlock) const noexcept; // (actual − nominal)/nominal
WindchestId chestForPipe(PipeId) const noexcept;
void registerDemand(WindchestId, float flowDemand) noexcept;       // accumulate-only, next block
```

`pressureAt` = `sagPressureAt` + `Tremulant::pressureDeltaPa`, sample-accurate.
A voice acquires a lightweight, trivially-copyable **`WindTap`** for its pipe's
chest at `prepare()`/`noteOn()` and samples pressure while rendering without
repeating the chest lookup:

```cpp
WindTap tap = windModel.tapFor(chest);
float dev = tap.deviationAt(frameInBlock);   // → WindResponseCurve → pitch/level/brightness
```

---

## 8. Per-tonal-family response (`WindResponseCurve`)

The same pressure signal produces **family-specific** realism. The normalised
deviation is scaled onto four axes by a per-`TonalFamily` curve, so reeds,
strings and flues breathe on different curves:

| Axis                     | Field                    | Effect     |
|--------------------------|--------------------------|------------|
| Pitch (cents/deviation)  | `centsPerDeviation`      | FM         |
| Level (dB/deviation)     | `dbPerDeviation`         | AM         |
| Brightness               | `brightnessPerDeviation` | HF development |
| Speech / attack onset    | `attackPerDeviation`     | onset      |

`defaultResponseFor(core::TonalFamily)` supplies conservative starting curves
(refined later against the offline wind-sensitivity analysis). Reeds get the
steepest pitch/brightness slopes; flues the gentlest.

---

## 9. Demand → sag feedback loop

The polyphony feedback is explicit and physical:

```
  block N:   voices renderAdd(...) → each calls registerDemand(chest, flow)
                                        │ (accumulate-only tally)
  block N+1: WindModel::step()      ───┘ integrates that demand → sag
             voices read pressureAt(...) on the sagged snapshot → thinner, flatter tutti
```

A one-block latency between demand and sag is deliberate and inaudible; it keeps
the snapshot immutable within a block (the race-free guarantee) while still
producing the characteristic "the organ leans into a big chord" behaviour.

> Optional (later phase): weak pipe-to-pipe frequency pulling on a shared chest,
> exposed through the windchest bus, for the subtle locking real ranks exhibit.

---

## 10. Configuration & bring-up

`WindModelConfig` (built off-thread by the model/loader layer) describes the
whole graph: `bellows[]`, `chests[]` (each naming its `bellowsIndex` and optional
`tremulantIndex`), `tremulants[]`, and `pipeBindings[]` (rank→chest). A
convenience factory `makeSingleChestConfig(nominalPa, tremRateHz, tremDepth)`
builds a single bellows → single chest → one (disengaged) tremulant graph for
early bring-up.

---

## 11. Real-time safety summary

| Concern      | Guarantee                                                                        |
|--------------|----------------------------------------------------------------------------------|
| Allocation   | Only in `prepare(...)` / `configure(...)`                                         |
| Snapshot     | Computed once per block in `step(...)`, then read-only → **data-race-free**       |
| Demand       | `registerDemand` accumulates only; feeds the *next* block, never mutates current |
| Tremulant    | Analytic sub-block phase → sample-accurate, no per-sample state                   |
| Numerics     | Exact exponential relaxation → unconditionally stable for any block size          |

---

## 12. Roadmap touchpoints

- **v0.2 — wind model + tremulant realism.** The reservoir ODE with sag under
  polyphony, per-windchest `WindState`, `WindResponseCurve` per family, tremulant
  as pressure modulation; sine ranks audibly breathe and sag. A live
  wind-pressure gauge appears in the first flat console skin.
- **v0.6 — physical tier.** `IExcitation::setWind(...)` couples the same pressure
  into the nonlinear jet/reed drive; sag dulls harmonic development and alters
  speech.
