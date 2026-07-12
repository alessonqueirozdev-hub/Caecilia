<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Ceciliae — Synthesis Design

How `ceciliae::synth` turns a keypress into sound: the **layered voice pipeline**
(attack → sustain → release), the two physical synthesis engines plus the
CPU-cheap partial bank, per-pipe hand-voicing, the attack-splice hybrid, and how
every layer **breathes** by reading the shared wind supply.

> Scope: complements [`ARCHITECTURE.md`](ARCHITECTURE.md) §3 (the hybrid voice
> model), [`WIND_MODEL.md`](WIND_MODEL.md) (the audio-rate control signal these
> voices consume) and [`DSP_DESIGN.md`](DSP_DESIGN.md) (the oversampled island
> and interpolators the engines run inside).

---

## 1. The thesis: a voice is a *composition*, not an engine

GrandOrgue plays dead looped samples; Aeolus plays uniform synthetic additive
tone. Each captures in isolation what a real pipe does together. Ceciliae's
answer is that a voice is **not** "a rank picks one engine" — it is a layered
pipeline, so realism can live in the two places a monolithic engine cannot
express:

- **Intra-note** — a recorded chiff/speech attack spliced, phase- and
  spectrum-continuously, into a **loop-free modeled sustain**.
- **Per-pipe** — deterministic hand-voicing variance keyed to a stable
  `core::PipeId`, so no two pipes of a rank are clones.

```
   core::IVoice  (renderAdd += , noteOn/noteOff, kind(), tier(), cpuCostEstimate())
   └── composes ──────────────────────────────────────────────────────────────┐
        AttackLayer            SustainLayer                ReleaseLayer         │
        (ISampleSource)        (IModeledSustain)           (multi-stage,        │
        recorded chiff/speech  loop-free, wind-modulated   hold-time aware,     │
          │                      │                         pressure-collapse    │
          │   AttackSpliceConfig │                         chiff)               │
          └── phase/spectrum ────┘                                              │
              continuous splice                                                 │
                    │  all layers share ONE SpectralModel seed                  │
                    └── PerPipeVoicer → VoicingProfile (seeded by PipeId) ──────┘
```

Pure-sample and pure-additive voices are simply **degenerate configurations** of
this same pipeline — the safe, always-shippable fallback, never abandoned.

---

## 2. The layer contract (`IVoiceLayer`)

Every stage implements `synth::IVoiceLayer`:

```cpp
prepare(SampleRate, maxBlockFrames)          // the ONLY allocating call
trigger(PipeId, Velocity, double frequencyHz) noexcept
release() noexcept
renderAdd(core::AudioBlock&) noexcept         // MUST accumulate (+=), never overwrite
isActive() const noexcept
```

Two specialisations refine it:

- **`ISampleSource`** — a read-only provider of recorded material in Ceciliae's
  **own** proprietary format (`channelCount`, `sourceSampleRate`,
  `rootFrequencyHz`, `frameCount`, `loopStart`, `loopEnd`, `sampleAt`).
  Ceciliae prefers a loop-free modeled sustain, so `loopEnd == 0` signals a
  one-shot recording whose tail hands off to a modeled sustain. Streaming/loading
  lives in `model` off the audio thread; the RT path only reads frames through a
  `dsp::Resampler`.
- **`IModeledSustain`** — a loop-free sustain that can `seedFrom(const
  SpectralModel&, float phaseAlignSeconds)` so its timbre matches the recorded
  attack. `seedFrom` fills only pre-allocated storage (RT-safe); partials beyond
  capacity are dropped.

---

## 3. The analysis bridge (`SpectralModel`)

The proprietary **offline analysis toolchain** (partial tracking / formant /
wind-sensitivity estimation, built on the same core but *not* shipped in the RT
path) produces a `SpectralModel` per pipe:

```cpp
struct PartialTrack {
    float ratioToF0;        // frequency as a ratio to the fundamental
    float ampDb;            // steady-state level
    float phase;            // initial phase (phase-aligned splices)
    float windSensitivity;  // how strongly this partial tracks wind deviation
    float onsetSeconds;     // staggered onset → emergent chiff
    float brightnessTrack;  // extra HF development as pressure rises
};
struct SpectralModel {
    std::vector<PartialTrack> partials;
    FormantEnvelope           steadyFormants;   // ≤ kMaxFormants peaks (fixed-capacity)
    float                     fundamentalHz;
};
```

The same `SpectralModel` seeds the modeled sustain that the recorded attack fades
*into*, which is what makes the attack-splice spectrum-continuous. It is a
setup-time descriptor; the RT path only reads a seeded, pre-sized partial bank
derived from it — never this `std::vector` directly. It never reads a GPL'd
GrandOrgue/Hauptwerk set: this format is owned IP.

---

## 4. The two tiers of sound

Two synthesis tiers share the same voicing params and wind port. The
`VoiceFactory` picks per rank from a `RankVoiceRequest`.

### 4.1 Cheap tier — `PartialBank` (additive / modal)

`PartialBank` implements `IModeledSustain`: a wind-modulated bank of up to
`kDefaultMaxPartials = 64` `Partial`s. It is **both** the plenum-polyphony tier
**and** the demotion target when the deadline budget sheds the physical tier.

- Each `Partial` is a *function of the wind supply*, not a stored table: its
  pitch/level/brightness track `pressureDeviation` through a per-family
  `WindResponseCurve` (see §6).
- `setWindCoupling(const core::IWindSupply*, WindchestId, WindResponseCurve)`
  wires the breathing; `seedFrom(...)` installs the timbre; a simple
  attack/sustain/release envelope gates the whole bank.
- Per-partial `onsetSeconds` stagger makes chiff **emerge** rather than play a
  canned transient.

### 4.2 Expensive tier — `PhysicalPipeVoice` (source–resonator)

`PhysicalPipeVoice` composes an `IExcitation` with an `IResonator`, driven by the
per-block wind snapshot, for exposed/solo stops:

| Family          | Excitation (`IExcitation`) | Resonator (`IResonator`) | `EngineKind` |
|-----------------|----------------------------|--------------------------|--------------|
| Flue (Principal/Flute/String) | `NonlinearJet`  | `Waveguide` (bidirectional delay line) | `Waveguide` |
| Reed (Trumpet/Oboe/…)         | `BeatingReed`   | `ModalResonator` (tuned mode bank)     | `Modal`     |

- **`IExcitation`** is where chiff and speech *emerge*: `trigger` arms the note,
  `setWind(pressurePa, pressureDeviation)` couples the driver every sample, and
  `isSettled()` reports when the startup transient has locked to the resonator. A
  flue's jet takes a few ms to establish; a reed's tongue takes time to lock in —
  and both are functions of instantaneous pressure, never a stored transient.
- **`IResonator`** is linear and stable (`setFrequency`, `process(excitation)`);
  all character comes from the nonlinear coupling and the wind.
- The excitation runs inside the **oversampled polyBLEP island** from
  [`DSP_DESIGN.md`](DSP_DESIGN.md) §6, so its nonlinear output does not alias.

> Phase-0 status: `NonlinearJet` / `BeatingReed` / `Waveguide` / `ModalResonator`
> are structurally complete scaffolds whose DSP bodies return silence and only
> advance the settle timer. The band-limited nonlinear models land in v0.6.

### 4.3 Tiering & demotion

`VoiceTier { Modal, Additive, Waveguide }` ranks quality against cost. Under
`core::engine::DeadlineBudget` pressure the scheduler **demotes** a voice's tier
via a phase-aligned, inaudible crossfade (`PhysicalPipeVoice → PartialBank`), so
worst-case tutti degrades to subtle thinning, never a dropout.
`core::IVoice::cpuCostEstimate()` feeds the stealing/demotion policy.

---

## 5. Per-pipe hand-voicing (`PerPipeVoicer` → `VoicingProfile`)

A real organ is voiced pipe by pipe. `PerPipeVoicer::voice(PipeId)` resolves a
deterministic `VoicingProfile` by scattering the rank's `VoicingParams` ranges
through a small SplitMix64-style hash **seeded by the stable `PipeId`** (plus an
optional organ-id salt):

```cpp
struct VoicingProfile {  // resolved, per-pipe
    float detuneCents, levelTrimDb, brightnessTrim, attackJitter;
    float chiffAmount, speechAmount, driftDepth;
    std::uint32_t phaseSeed;   // deterministic initial-phase set
};
struct VoicingParams {   // per-rank recipe (scatter bounds)
    float maxDetuneCents, maxLevelTrimDb, maxBrightnessTrim, maxAttackJitter;
    float baseChiffAmount, baseSpeechAmount, maxDriftDepth;
};
```

Because the seed is stable, a given pipe always receives the same voicing across
runs and sessions; because each rank's seed stream is independent, a **mixture
shimmers** instead of sounding cloned. Independent per-pipe phase and slow drift
keep unisons from phase-locking. This composes with the tuning module's
`DetuneCurve` (same `PipeId` seed) — see [`TUNING.md`](TUNING.md).

---

## 6. Wind coupling (the realism moat)

Wind is an **audio-rate control signal, not an LFO** — the single realism moat
both references lack. Every layer reads the immutable per-block `WindState`
(`core::IWindSupply`) via a `wind::WindTap` and maps the normalised deviation
`(actual − nominal)/nominal` through a per-`TonalFamily` `wind::WindResponseCurve`
(the single curve type, owned by the `wind` module and consumed here):

```cpp
struct WindResponseCurve {           // ceciliae::wind
    float centsPerDeviation;      // pitch   → FM
    float dbPerDeviation;         // level   → AM
    float brightnessPerDeviation; // harmonic development
    float attackPerDeviation;     // speech / onset sensitivity
};
```

Reeds swing sharply in pitch and brightness as pressure moves; flues far less;
strings sit in between and shimmer. Because it modulates the **driver**, the
tremulant yields coupled AM + FM + timbral movement automatically — a gain LFO
cannot reproduce this. Sag under polyphony also dulls harmonic development and
alters speech. Full details in [`WIND_MODEL.md`](WIND_MODEL.md).

---

## 7. The attack-splice hybrid (`AttackSpliceConfig`)

The intra-note realism that no monolithic engine can express:

```cpp
struct AttackSpliceConfig {
    bool  useRecordedAttack   = true;  // false → pure modeled attack (fallback tier)
    float crossfadeMs         = 18.0f; // equal-power splice window
    float phaseAlignSeconds   = 0.0f;  // phase alignment into the recording
    float spectralMatchAmount = 1.0f;  // how hard the sustain matches the attack's end spectrum
    float gainMatchAmount     = 1.0f;  // level continuity across the splice
    float spliceOffsetSeconds = 0.0f;  // where the sustain hand-off begins
};
```

A recorded chiff/speech attack is equal-power cross-faded into an
`IModeledSustain` seeded from the **same** `SpectralModel`, phase-aligned via
`PartialTrack::phase` and level-matched, so the ear never hears a loop point or a
spectral jump. Setting `useRecordedAttack = false` degrades gracefully to a pure
modeled attack.

---

## 8. Release (`ReleaseSpec`)

A quickly-tapped pipe releases differently from a long-held one (reservoir
collapse and room tail differ). `ReleaseSpec::releaseMsForHold(heldSeconds)`
interpolates between `shortReleaseMs` and `longReleaseMs` across
`holdThresholdSeconds`, and `pressureCollapseChiff` injects a modeled chiff as
the wind falls away over `modeledTailSeconds` — so the release is never a single
canned fade.

---

## 9. Voice selection (`VoiceFactory`)

`VoiceFactory` encodes the tiering policy that unifies GrandOrgue and Aeolus, all
as configurations of one pipeline. From a `RankVoiceRequest`
(`family`, `role`, `footage`, `exposed`, `hasSampleSet`, `hasSpectralModel`,
`maxTier`, plus non-owning `SpectralModel` / `ISampleSource` / `VoicingParams` /
`ReleaseSpec`):

- `selectEngine(...)` / `selectTier(...)` — pure decision (an **exposed** stop
  earns the physical tier; a plenum rank with a `SpectralModel` gets the cheap
  modeled tier; a rank with only a sample set falls back to pure sample playback).
- `create(...)` — builds an owned, **already-prepared** voice off the audio
  thread; the audio thread only renders the returned voices.

---

## 10. Real-time safety summary

| Concern           | Guarantee                                                                    |
|-------------------|------------------------------------------------------------------------------|
| Allocation        | Only in `prepare(...)` / `VoiceFactory::create(...)` / `seedFrom` storage     |
| `renderAdd`       | Always accumulates (`+=`); every layer method except `prepare` is `noexcept`  |
| Wind reads        | `WindTap` reads the immutable snapshot → race-free by construction            |
| CPU pressure      | Tier demotion + voice stealing under `DeadlineBudget` → thinning, not xrun     |
| Determinism       | `PerPipeVoicer` / `DetuneCurve` seeded by `PipeId` → repeatable voicing        |

---

## 11. Roadmap touchpoints

`v0.1` sine ranks · `v0.2` wind + tremulant realism · `v0.4` sample layer +
`PerPipeVoicer` (safe fallback tier) · `v0.5` hybrid attack-splice + modeled
sustain (offline `SpectralModel`) · `v0.6` nonlinear physical tier + phase-aligned
demotion + SoA/SIMD unison batching + numerical-stability guards.
