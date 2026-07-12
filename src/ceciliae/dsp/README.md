<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Ceciliae is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# ceciliae::dsp

The independently-written DSP building blocks and the primitives of the master
signal chain. This module contributes to the pure `ceciliae_core` static library
(standard-library only, **no JUCE**) and is fully headless-unit-testable.

Every algorithm here is authored fresh from **public-domain mathematics** — Jot's
1991 unitary-feedback FDN principles and standard Kaiser-window / windowed-sinc
theory. No GPL DSP source (GrandOrgue, Hauptwerk sets, or otherwise) is copied or
referenced.

## Real-time contract

- All state and buffers are allocated in `prepare()` / `build()` / `load…()`.
- `process()` / `render()` paths are `noexcept`, allocation-free, lock-free.
- Feedback paths flush denormals (`DspMath::flushDenormal`) and assume per-thread
  FTZ/DAZ set by the host seam.
- SIMD backends live behind the `kernels::` seam and are ULP/bit-compared against
  the scalar reference in CI.

## Contents

| Type | Header | Role |
| --- | --- | --- |
| `FdnReverb` | `FdnReverb.h` | 16-line Jot-style feedback delay network: Hadamard mixing, per-line damping, RT60→gain mapping, pre-delay, `ReverbPreset`s. Implements `core::IReverb`. |
| `ConvolutionReverb` | `ConvolutionReverb.h` | Impulse-response reverb (direct-form today, uniform-partitioned FFT to come). Implements `core::IReverb`. |
| `SincKaiser16Interpolator` | `SincKaiser16Interpolator.h` | Fresh 16-tap Kaiser-windowed-sinc polyphase fractional interpolator. |
| `Resampler` | `Resampler.h` | Arbitrary, continuously-variable-ratio sample reader (linear or Kaiser-sinc). |
| `PolyphaseResampler` | `PolyphaseResampler.h` | Fixed rational-ratio (L/M) polyphase resampler. |
| `OnePole` | `OnePole.h` | First-order low-/high-pass (damping, DC block, tone). |
| `Biquad` | `Biquad.h` | Second-order IIR + bilinear-transform coefficient designers. |
| `FormantFilter` | `FormantFilter.h` | Parallel resonant-peak bank for reed/vowel formant colour. |
| `Oversampler` | `Oversampler.h` | 1×/2×/4×/8× oversampling wrapper for the nonlinear synthesis island. |
| `Spatializer` | `Spatializer.h` | Per-pipe equal-power stereo placement + case-depth early reflection. |
| `kernels::…` | `Kernels.h` | SIMD-friendly inner-loop seam (accumulate, dot product, biquad block, Hadamard-16). |
| free functions | `Bandlimited.h` | polyBLEP / polyBLAMP band-limited residuals. |
| free functions | `DspMath.h` | Constants, `sinc`, `besselI0`, `kaiserWindow`, `dbToGain`, denormal flush, equal-power pan. |

## Reverb usage sketch

```cpp
#include "ceciliae/dsp/FdnReverb.h"

ceciliae::dsp::FdnReverb reverb;
reverb.prepare(sampleRate, maxBlockFrames, /*numChannels*/ 2); // off-thread
reverb.setPreset(ceciliae::dsp::ReverbPreset::Cathedral);      // RT-safe

// audio thread:
ceciliae::core::AudioBlock block{channels, 2, frames};
reverb.process(block); // in place, denormal-flushed
```

Both reverbs satisfy the same `core::IReverb` interface, so the engine's master
chain can swap or run them interchangeably.

## Status / roadmap notes

The scaffold wires every type end-to-end with coherent, compilable bodies. Items
marked `// TODO(phaseN)` in the sources are the remaining depth work, chiefly:

- `phase3` — SSE/AVX2/NEON backends for the `kernels::` seam; full polyphase
  commutator inner loop; adjacent-phase blending in the sinc interpolator.
- `phase6` — true half-band interpolation/decimation cascade in `Oversampler`;
  minBLEP table in `Bandlimited`.
- `phase9` — uniform-partitioned FFT overlap-add in `ConvolutionReverb`.

These are all internal quality upgrades behind stable public signatures.
