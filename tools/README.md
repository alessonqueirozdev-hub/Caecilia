<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Caecilia — offline analysis tools

Host-side command-line utilities that make up Caecilia's **off-line analysis
toolchain**. They turn raw recordings into the proprietary metadata the runtime
engine later ingests (loop points, per-pipe pitch, additive `PartialBank`s that
seed the modeled sustain, and conditioned impulse responses for the convolution
reverb).

These tools are deliberately **separate from the real-time signal path**:

- They **link `caecilia_core`** to share its exact vocabulary
  (`PipeId`, `Footage`, `SampleRate`, `versionString`, …) so an artefact carries
  the very same identity keys the engine will use.
- They are **JUCE-free**, exactly like the pure core. Only the `plugin` and `ui`
  modules may include JUCE.
- Because nothing here runs on the audio thread, the RT contract does **not**
  apply: `std::vector`, `<fstream>`, `iostream` and exceptions are all fine.

## Tools

| Executable | Purpose | Status |
|---|---|---|
| `caecilia-sampleset-analyzer` | Extract loop points and pitch from a recorded pipe sample. | Scaffold: level/peak scan real; pitch + loop search are `TODO(phase04)`. |
| `caecilia-partial-extractor`  | FFT a steady pipe tone into an additive `PartialBank` (off-line `SpectralModel`). | Scaffold: bank plumbing real; STFT + partial tracking are `TODO(phase05/06)`. |
| `caecilia-ir-tool`            | Inspect and condition impulse responses for the convolution reverb. | Scaffold: `info` + `normalise` real; `trim`/`resample`/`deconvolve` are `TODO(phase03/95)`. |

## Usage

```sh
# Analyse one pipe recording, print JSON to stdout
caecilia-sampleset-analyzer --input pipe_C4.wav --rank 3 --note 60 --f0 261.63

# Extract an additive partial bank for a 2 2/3' quint, write to a file
caecilia-partial-extractor --input quint.wav --footage-num 8 --footage-den 3 \
    --fft-size 4096 --max-partials 48 --output quint.partials.json

# Describe an impulse response, then normalise it to -1 dBFS
caecilia-ir-tool info      --input hall.wav
caecilia-ir-tool normalise --input hall.wav --output hall_-1dB.wav --peak-db -1
```

Every tool supports `--help`. Options use `--key value` or `--key=value`; bare
`--flag`s have no value.

## Layout

```
tools/
  CMakeLists.txt              # 3 executables + a shared helper static lib
  common/
    AnalysisTypes.h           # POD result types (in caecilia::tools, not core)
    CliArgs.{h,cpp}           # tiny dependency-free argument parser
    WavFile.{h,cpp}           # minimal RIFF/WAVE reader + float32 writer
  sampleset-analyzer/
    SampleSetAnalyzer.{h,cpp} # loop/pitch extraction transform
    Main.cpp                  # CLI driver
  partial-extractor/
    PartialExtractor.{h,cpp}  # FFT -> PartialBank transform
    Main.cpp                  # CLI driver
  ir-tool/
    ImpulseResponseTool.{h,cpp} # IR measure/condition transform
    Main.cpp                  # CLI driver
```

The analysis classes do no console/file I/O — the `Main.cpp` drivers own that —
so each transform stays trivially unit-testable against synthetic buffers.

## Build

Enabled by default via the top-level `CAECILIA_BUILD_TOOLS` option:

```sh
cmake -B build -DCAECILIA_BUILD_TOOLS=ON
cmake --build build
# binaries land in build/tools/
```

The include convention differs slightly from the pure `src/` modules: tool
sources include their siblings relative to the tools root (`common/WavFile.h`)
and pull core headers through the standard project root
(`caecilia/core/EngineTypes.h`).
