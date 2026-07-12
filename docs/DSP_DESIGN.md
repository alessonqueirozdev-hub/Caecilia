<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia — DSP Design

The independently-authored DSP building blocks of `caecilia::dsp` and the master
signal chain: the dual reverb (algorithmic FDN + convolution), fractional-delay
interpolation and resampling, the filter family, oversampling for the nonlinear
synthesis island, per-pipe spatialization, and the SIMD kernel seam.

> Scope: this document complements [`ARCHITECTURE.md`](ARCHITECTURE.md) (§the
> master signal flow) and [`SYNTHESIS.md`](SYNTHESIS.md) (the nonlinear island
> that consumes the oversampler and band-limiter). Every algorithm here is
> written fresh from public-domain / textbook mathematics; **no GPL DSP source
> (GrandOrgue or otherwise) is copied or referenced.**

---

## 1. Design axioms

1. **`prepare()`-then-`process()`.** Every processor allocates delay lines,
   coefficient tables and scratch buffers in `prepare(...)` (off the audio
   thread). Every `process(...)` / `readBlock(...)` is `noexcept`,
   allocation-free and lock-free.
2. **One buffer type crosses the seam.** DSP hot paths take `core::AudioBlock`
   (a non-owning, offset-carrying float view) or raw `float*` spans — never a
   `juce::AudioBuffer`. `dsp` links only the standard library.
3. **Denormals are the enemy of feedback.** Every recursive path (FDN feedback,
   IIR memory) runs under per-audio-thread FTZ/DAZ (set by `plugin`) **plus**
   explicit flushing so a decaying tail cannot fall into denormal CPU spikes.
4. **A scalar reference is the source of truth.** Vectorised kernels are
   bit/ULP-compared against a portable scalar reference in CI (see §8).

---

## 2. The master signal chain

The master chain is owned and driven by `core::engine::AudioEngine::processBlock`
(see [`ARCHITECTURE.md`](ARCHITECTURE.md) §7). DSP appears in two places:

```
  per voice (synthesis)                        engine mixdown & master
  ┌───────────────────────┐                    ┌──────────────────────────────┐
  │ voice mono bus         │  dsp::Spatializer  │ per-windchest stereo bus      │
  │  → dsp::Resampler      │ ─ pan + distance ─▶│  Σ  →  dsp::IReverb (master)   │→ out
  │  → dsp::Oversampler    │   + depth pre-delay│      (FdnReverb | Convolution) │
  │    (nonlinear island)  │   early reflection │                                │
  └───────────────────────┘                    └──────────────────────────────┘
```

- **Per-pipe** spatialization (`dsp::Spatializer`) turns each voice's mono
  output into a stereo pair with equal-power pan, distance attenuation and a
  case-depth pre-delay that seeds early reflections *ahead* of the shared tail.
- **Per-instrument** reverb (`core::IReverb`, bound via
  `AudioEngine::setMasterReverb`) processes the summed stereo bus in place.

`AudioEngine` reports total added latency (oversampler group delay + convolution
first-partition delay) to the host for plugin delay compensation (PDC).

---

## 3. Reverb

Both reverbs implement the shared `core::IReverb` contract
(`prepare / setParams / process / reset / latencySamples`) and read a
`core::ReverbParams` snapshot:

| Field        | Meaning                                    | Default |
|--------------|--------------------------------------------|---------|
| `mix`        | dry/wet in [0, 1]                          | 0.25    |
| `decaySec`   | approximate RT60 decay time                | 2.5     |
| `preDelayMs` | pre-delay before the tail                  | 12.0    |
| `dampingHz`  | HF damping corner                          | 6000    |
| `widthNorm`  | stereo width of the tail in [0, 1]         | 1.0     |

`setParams` is RT-safe (a handful of `pow`/`exp` recomputations, no realloc), so
a controller can automate the acoustic without an xrun.

### 3.1 `FdnReverb` — the algorithmic path (Jot 1991 FDN)

A 16-line **Feedback Delay Network** (`kLines = 16`) built from the public
principles of Jot's unitary-feedback FDN:

```
        ┌──────────────── 16 mutually-incommensurate delay lines ─────────────┐
  in ─▶ ⊕ ─▶ [ z^-L0 ]─┬─▶ OnePole damper ─▶ ×feedback ─┐
        │              │                                 │
        │   ...  16×    ... per-line ...                 │
        │              │                                 │
        └──────────────┴─────── H·v  (unitary Hadamard mixing) ◀──────────────┘
                                  │
                          stereo output taps (tapLeft_/tapRight_, widthNorm)
```

- **Mixing matrix.** The lossless feedback recombination is
  `kernels::hadamard16` — an in-place, orthonormal 16-point Walsh–Hadamard
  transform (radix-2 butterflies, scaled by `1/sqrt(16) = 0.25`). Being unitary,
  it preserves energy while scattering every line into every other, which is what
  makes the tail dense and colourless.
- **Decay.** `updateDecay()` maps `decaySec` (RT60) to a per-line feedback gain
  from each line's length via the standard `g = 10^(-3·L/(RT60·fs))` relation.
- **Damping.** Each line carries a `dsp::OnePole` low-pass damper
  (`updateDamping()` from `dampingHz`) so highs decay faster than lows, as in a
  real stone room.
- **Pre-delay.** A ring buffer (`preDelay_`) delays the wet send by `preDelayMs`.
- **Width.** `updateWidth()` derives the stereo output-tap weights from
  `widthNorm`.
- **Denormals.** The feedback write is flushed every sample; `latencySamples()`
  is `0` (the FDN adds no bulk latency).

Five presets voice common organ acoustics via `presetParams(ReverbPreset)`:
`Room · Chamber · Hall · Cathedral · Plate`.

### 3.2 `ConvolutionReverb` — the impulse-response path

The second path convolves the bus with a measured room impulse response for
maximum fidelity to a specific acoustic. The production structure is
**uniform-partitioned block convolution** (an FFT per partition, overlap-add
across partitions) so latency stays at one partition while long tails remain
affordable.

- `loadImpulseResponse(const float* ir, std::size_t length)` copies and
  partitions the IR **off the audio thread**; a length of `0` clears it (the
  reverb then passes audio through dry).
- `latencySamples()` returns the first-partition delay so the host can apply PDC.
- Only Caecilia's own / public-domain impulse responses are used; **no GPL
  sample set or GPL convolution code is referenced.**

---

## 4. Interpolation & resampling

Three complementary tools, all built from public windowed-sinc / Kaiser-window
theory:

### 4.1 `SincKaiser16Interpolator` — the fractional-delay kernel

A 16-tap Kaiser-windowed-sinc interpolator (`kTaps = 16`). `build(numPhases,
kaiserBeta)` precomputes a dense **polyphase table** of the windowed kernel
(`numPhases * kTaps` coefficients; default `numPhases = 512`, `kaiserBeta ≈ 8.6`
for ~ -80 dB sidelobes). Run-time `process(const float* x16, float frac)` is then
a single 16-tap dot product (`kernels::dotProduct`) against the nearest phase row
— `noexcept`, allocation-free, fixed cost.

### 4.2 `Resampler` — arbitrary, moving-ratio playback

Wraps the sinc kernel for **continuously-variable** ratios (per-note pitch,
per-pipe detune, temperament, wind-driven pitch modulation). Selectable quality:

| Quality        | Kernel                    | Use                              |
|----------------|---------------------------|----------------------------------|
| `Linear`       | 2-point                   | cheapest; mild HF loss           |
| `KaiserSinc16` | 16-tap Kaiser-sinc        | near-transparent default         |

`interpolateAt(source, length, pos)` reconstructs one sample (edge taps clamped
so the kernel never reads out of bounds); `readBlock(...)` streams `outCount`
samples advancing a fractional cursor by `ratio` each step, zero-filling past the
source end.

### 4.3 `PolyphaseResampler` — fixed rational L/M conversion

For a *fixed, known* integer ratio (bringing proprietary sample material recorded
at one rate onto the host rate) the efficient structure is a single windowed-sinc
prototype split into `L` polyphase sub-filters — upsample by `L`, decimate by `M`
without materialising the zero-stuffed intermediate. `prepare(interpFactor,
decimFactor, tapsPerPhase)` designs the prototype at the lower band edge so
neither imaging nor aliasing leaks.

> Rule of thumb: **fixed** ratio → `PolyphaseResampler`; **moving** ratio →
> `Resampler`.

---

## 5. Filters

| Type            | Form                                  | Role                                                  |
|-----------------|---------------------------------------|-------------------------------------------------------|
| `OnePole`       | 1st-order LP/HP (`y=b0·x+b1·x1−a1·y1`) | FDN damping, DC blocking, gentle tone shaping         |
| `Biquad`        | Transposed Direct Form II             | general EQ / crossover, per-block or per-sample        |
| `FormantFilter` | parallel bank of ≤ 5 peaks            | reed/flue steady-state formant colour                 |

- **`Biquad`** carries per-instance state plus a `BiquadCoeffs` designer set
  (`lowpass · highpass · bandpass · notch · peaking · lowShelf · highShelf`) from
  standard bilinear-transform formulas. `setCoeffs` is click-free (swaps
  coefficients without clearing state); coefficient design is done off the audio
  thread on parameter change.
- **`FormantFilter`** sums up to `dsp::kMaxFormants = 5` peaking biquads
  (F1..F5 covers vowels and most reeds). Each `Formant` is
  `{centreHz, bandwidthHz, gainDb}`; `setFormants(...)` derives the biquad Q from
  centre/bandwidth in place (RT-safe). It renders the strongest peaks of the
  `FormantEnvelope` carried by the analysis `SpectralModel` (see
  [`SYNTHESIS.md`](SYNTHESIS.md)) — note that envelope has its own capacity
  (`synth::kMaxFormants`), so the filter tracks its most significant regions
  rather than mirroring it one-for-one.

---

## 6. Oversampling & band-limiting (the nonlinear island)

Nonlinear synthesis stages (jet saturation, beating-reed clipping) generate
harmonics that would alias at the base rate. `dsp` supplies two cooperating
tools consumed by `synthesis`:

- **`Oversampler`** — a 1x/2x/4x/8x wrapper around an in-place nonlinearity.
  `prepare(maxBlockFrames, factor)` designs a **half-band** interpolation/
  decimation cascade (every other polyphase coefficient is zero, so it is cheap).
  Usage: `upsample` a base-rate block → apply the nonlinearity across
  `oversampledData()` / `oversampledFrames()` → `downsample` back.
  `latencySamples()` reports the cascade group delay (in **base**-rate samples)
  for PDC.
- **`Bandlimited`** — closed-form polyBLEP/polyBLAMP residuals (`polyBlep`,
  `polyBlamp`, `bandlimitedResidual`) added around sharp discontinuities (jet
  startup, reed beating) to cancel most aliased energy without a full BLIT table.

Together they form the "oversampled polyphase island" the physical voice tier
runs inside (see [`SYNTHESIS.md`](SYNTHESIS.md) §5).

---

## 7. Per-pipe spatialization

`dsp::Spatializer` places one pipe/voice in the stereo field. A `PipeSpatialParams`
POD (`panNorm`, `distanceNorm`, `depthMs`, `earlyLevel`) is adapted from
`model::PipeSpatial` at note-on (keeping the DSP seam model-agnostic).

- `processAdd(...)` accumulates the placed stereo pair into the destination
  (mirroring the voice `renderAdd` += contract); `processReplace(...)`
  overwrites.
- Equal-power panning (`gainLeft_`, `gainRight_`) + a short `depthMs` pre-delay
  ring seeds the **pipe-position early reflection** that arrives before the
  shared reverb tail — the spatial cue that a diapason on the façade sounds
  closer than a bourdon deep in the case.

---

## 8. The SIMD kernel seam

`caecilia::dsp::kernels` is the one place inner loops are vectorised. Each kernel
has a portable **scalar reference** in `Kernels.cpp`; architecture backends
(SSE/AVX2 on x86-64, NEON on Apple Silicon) are selected by compile-time/runtime
dispatch and **bit/ULP-compared against the scalar reference in CI** within a
bounded tolerance. Callers only ever see the free functions:

| Kernel                                     | Purpose                                             |
|--------------------------------------------|-----------------------------------------------------|
| `accumulate(dst, src, n, gain)`            | `dst[i] += src[i]·gain` — the core voice mix bus op |
| `dotProduct(a, b, n)`                      | windowed-sinc tap sum                               |
| `biquadBlock(data, n, b0..a2, z1, z2)`     | raw-coefficient TDF-II block (vectorised per chan)  |
| `hadamard16(v)`                            | unitary 16-point FDN feedback mixing matrix         |

Because the reference is authoritative, a backend can never silently diverge:
the CI trap fails the build if any lane drifts past the ULP/RMS budget.

---

## 9. Real-time safety summary

| Concern            | Guarantee                                                                   |
|--------------------|-----------------------------------------------------------------------------|
| Allocation         | Only in `prepare(...)` / `build(...)` / `loadImpulseResponse(...)`           |
| Locks / exceptions | None on any `process` / `readBlock` / `interpolateAt` path (`noexcept`)      |
| Denormals          | FTZ/DAZ per audio thread + explicit flushing in FDN and IIR feedback        |
| Latency            | `latencySamples()` on reverb + oversampler group delay → host PDC           |
| Correctness        | Scalar-reference kernels; SIMD backends ULP-verified in CI                  |

---

## 10. Roadmap touchpoints

- **v0.3 — DSP core.** Sinc interpolator + polyphase resampling, filters, the
  Jot-style FDN with denormal-flushed feedback, per-pipe spatialization, and the
  scalar-vs-SIMD ULP harness. Master chain wired into `AudioEngine`.
- **v0.6 — nonlinear tier.** The oversampled polyBLEP island (`Oversampler` +
  `Bandlimited`) hosts `NonlinearJet` / `BeatingReed`.
- **v0.95 — convolution reverb + hardening.** The `ConvolutionReverb` path,
  denormal/NaN guards across FDN and wind feedback, and the cross-platform
  AVX2/NEON CI matrix.
