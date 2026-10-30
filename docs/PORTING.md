<!--
SPDX-License-Identifier: Apache-2.0
Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
-->

# Caecilia 2.0 — porting to Rust on Manguebyte

Caecilia 1.x is a JUCE/C++20 instrument. Caecilia 2.0 is the same instrument
rewritten in Rust on the [Manguebyte Audio Framework](https://github.com/alessonqueirozdev-hub/manguebyte),
developed on the `rust-2.0` branch of this repository and merged as 2.0 when it
is ready. The C++ stays in the history.

The goal is **not parity**. 1.x is the reference that proves the port has not
lost anything; 2.0 has to be clearly better than 1.x and than the organs on the
market. Every module below is listed with what it does today, where it goes,
what it is missing, and the problems it — or the market — has that 2.0 must
solve.

## 1. Decisions

| Decision | Choice |
|---|---|
| Where | This repository, branch `rust-2.0`. Nothing reaches `main` until the owner asks. |
| Licence | Apache-2.0, unchanged. Without JUCE the shipped binary is no longer AGPL. Manguebyte is MIT OR Apache-2.0. |
| Framework dependency | By path while both are developed side by side (`C:/manguebyte/crates/...` in `[workspace.dependencies]`), exactly as Parabolic does; switched to `git` + fixed `rev` before release so a clean checkout builds. |
| Toolchain | Edition 2024, `rust-version = "1.85"` (the framework's). No crate that is not already in the local cargo cache is added without the owner's permission. |
| The C++ tree | Stays in the branch while the port is under way: it builds JUCE-free (`CAECILIA_CORE_ONLY`) and `tools/reference` regenerates the reference data from it. It is deleted in one commit once 2.0 passes every reference. |
| Approach | Port, not translate. Use what Manguebyte has; write in Caecilia only what is organ-specific; ask the framework's owner for what is generic and missing (section 6). |

### Clean-room rules

- No JUCE source is read or copied. No GPL/LGPL/AGPL/MPL source is read —
  GrandOrgue, Aeolus, or any other. Other instruments are cited only by their
  observable behaviour and public documentation.
- **A 1.x hygiene flag.** Some 1.x comments name internals of Aeolus (a variable
  called `_h_lev`, per-pipe output levels "Aeolus generates each pipe at
  ~0.08–0.21 linear", "the Aeolus study" in `tools/dev/audition.cpp`):
  `synthesis/AdditiveVoice.cpp:27,45,81-84`, `synthesis/PartialBank.cpp:302,468`,
  `synthesis/PartialBank.h:56,212,353,359`, `dsp/Limiter.h:57`. Whatever those
  values were derived from, 2.0 does not carry them over by transliteration: the
  treble tilt, the "living pipe" drift and the per-pipe headroom are re-derived
  from public acoustics (pipe-scaling and voicing literature, our own
  measurements) and documented as such in the code that implements them.

## 2. The 1.x code, module by module

Sizes are lines in the tracked `.h/.cpp/.md` files. "Live" means reached by
the shipping plugin; "unwired" means written and unit-tested but not reached.

| 1.x module | Size | What it does | 1.x status | 2.0 home | From Manguebyte | Caecilia writes |
|---|---:|---|---|---|---|---|
| `core` | 1.4k | Ids (`PipeId`, `RankId`, `StopId`…), exact-rational `Footage`, `AudioBlock`, `IVoice`/`IWindSupply`/`ITuning`/`IReverb`, `TripleBuffer`, `ReverbSendGate`, parameter ids | Live | `caecilia-model` (vocabulary) | `AudioBuffer`, `spsc`, `analysis::triple_buffer`, `AtomicF32`, `NoDenormals` | `Footage`, the id types, the traits that remain |
| `dsp` | 4.2k | 16-line Jot FDN, direct-form convolution, Kaiser-sinc interpolator, resamplers, one-pole, biquad, formant bank, oversampler, spatializer, master EQ, limiter, SIMD kernels (SSE2/AVX2 partial oscillator) | FDN, EQ, limiter, biquad live; rest unwired | `caecilia-synth` (partial kernel), `caecilia-engine` (master chain) | `Reverb` (8-line FDN), `Convolution` (uniform-partitioned, zero-latency head, lock-free IR swap), `Biquad`/`OnePole`/`StateVariableFilter`, `Limiter`, `Oversampled`, `Resampler`, `core::vector` SIMD | The organ FDN if the framework's 8-line one cannot reach cathedral density; spatializer; formant bank; the partial-bank kernel (unless the framework takes it, §6) |
| `synthesis` | 3.8k | `PartialBank` (wind-modulated additive sustain, staggered onsets, drift, mixture break-back, chiff), `AdditiveVoice`, `RankVoicing`, `SpeechProfile`, `VoiceFactory`, `SampleVoice`, `PhysicalPipeVoice` (jet/reed + waveguide/modal) | Additive live; sample and physical voices silent placeholders | `caecilia-synth` | `Voice` trait shape, `Noise`, `Adsr` where it fits | Partial bank, rank voicing, the sample tier, the physical tier |
| `wind` | 1.6k | Bellows ODE (first order, or second order with plate mass), windchests with trunk drop, tremulant on the pressure, per-family response curves, rank→chest routing | Live | `caecilia-wind` | — | All of it (domain physics) |
| `engine` | 3.8k | `AudioEngine`: command ring, one voice per (rank, note), engaged-rank table, couplers, held-key reconciliation, chest buses, swell boxes, CPU governor, meters | Live | `caecilia-engine` | `EventBuffer` + `split_at_events` for sample-accurate events; `ProcessContext` | The organ engine: key expansion over ranks and couplers, chest buses, enclosure, governor (unless §6) |
| `tuning` | 1.1k | Six temperaments, A4, stretch, per-pipe detune, `LiveTuning` handed over by triple buffer | Live | the framework's tuning module (§6 #7) | temperaments, Scala `.scl/.kbm` → 128-note table, stretch; `triple_buffer` | Only the per-pipe detune and the live handover; 12-TET until the module lands |
| `model` | 5.8k | Organ definition → immutable `Organ`; organ-file JSON with a hand-written parser (`Json.cpp`); the demo organ and its voicing recipes (`DemoOrgan.cpp`, 1.1k) | Live | `caecilia-model` | — | Organ model; organ file via `serde_json` (already a framework dependency); the demo organ |
| `registration` | 3.0k | Selector algebra and versioned parser, `RegistrationEngine`, branching undo/redo, plenum builder, provenance (`explain`), `port()` of an intent across organs, pistons | Plugin uses a stop bitmask; the engine is unwired | `caecilia-registration` | `manguebyte-state` `Document`/`Tx` for persistence and linear undo of edits | The brain itself, wired end to end this time |
| `midi` | 2.6k | Router, channel→division map, MIDI learn, learned controls, combination store, sequencer (page turn), velocity curve, program-change map | Learn + channel map live; router, sequencer, store unwired | `caecilia-engine` (router), `caecilia-plugin` (learn) | `manguebyte-midi` (MIDI 1/2, UMP, SMF for test scores) | Router, learn table, sequencer |
| `control` | 1.5k | OSC and JSON-RPC codecs over one command sink | Unwired; no transport listens | `caecilia-control` | `manguebyte-osc` (sample-accurate) | JSON-RPC over a local socket; the sink |
| `plugin` | 5.0k | JUCE processor, parameter pool for stops, command bridge, WebView2 editor, state document | Live | `caecilia-plugin` | `Plugin` trait, `export_clap!/vst3!/au!/lv2!`, `manguebyte-standalone`, `manguebyte-params`, `save_state`/`load_state`, `Harness`, `cargo-mangue bundle` | The glue |
| `ui` | 0.2k (+ 2.5k `console.html`) | `StateMirror` frame for the console; the console is a web page in WebView2 | Live | `caecilia-ui` | `manguebyte-ui` (retained tree, design system of ADR 0011: dark-with-glow default and editorial light theme, cached shadows, motion, keyboard-only focus; accessibility; golden tests), `manguebyte-gui` `Editor`; tabs/pages, action button and dialog (scheduled); `TextSource` for live language switching | The organ widgets: drawstop, piston, expression shoe, wind gauge — written in Caecilia on the toolkit. No WebView, no JavaScript |
| `tests` | 16.1k | Catch2: ~60 files across every module, including whole-instrument tests through the processor | — | `tests/` of each crate | `Harness`, the counting-allocator pattern (`tests/realtime.rs`) | Ports of every behavioural test that still applies, plus the reference tests |
| `tools` | 4.5k | WAV I/O, fingerprint, bench, verify-registration, organ-file, analyzers (mostly scaffolds), IR tool, `reference` | — | `caecilia-tools` (bins) | `manguebyte-formats` WAV, `manguebyte-analysis` | fingerprint, bench, reference comparison |

### What the 1.x audio path actually is

Read from `CaeciliaAudioProcessor::prepareToPlay/processBlock` and
`AudioEngine`: 512 `AdditiveVoice`s, each able to become any rank; one voicing
per stop built once (`buildRankVoicing`); a key is ONE command, expanded in the
engine into one voice per engaged rank (plus couplers); wind stepped once per
block before voices render; per-chest buses, swell filter on enclosed chests;
FDN reverb, master EQ, a look-ahead limiter at −3 dBFS. That path — minus the
reverb, EQ and limiter, which have their own tests — is what
`tools/reference` renders.

## 3. Reference data

`tools/reference/Main.cpp` (`caecilia-reference`, JUCE-free) drives the 1.x
instrument down the plugin's path and writes `reference/cpp-1.x/reference.json`:

- **`organ`** — every windchest, division and stop of the demo organ, with each
  stop's 8'-referenced spectrum (ratio, rank base ratio, level, wind
  sensitivity, onset, brightness track) and speech profile. Deterministic; the
  port reproduces it **exactly**.
- **`tuning`** — 128-note tables for all six temperaments at A4 = 440 and 415.
  Exact.
- **`rankNotes`** — every stop at the bottom, middle and top of its compass,
  held 2 s: steady RMS, attack (to −3 dB of steady), release (to −40 dB),
  stereo correlation, and a 48-bin harmonic profile on a half-key grid.
- **`windChord`** — everything drawn, middle C held, eight keys added at 1 s,
  all released at 3 s: per-block pressure of every chest, the published sag,
  voice count and a 10 ms RMS envelope.
- **`tremulant`** — Bourdon 8' on the Récit chest, tremulant at 5.5 Hz between
  1 s and 4 s: pressure every 32 frames, measured rate, pressure swing, and the
  envelope it puts on the note.

The partials drift on a deterministic random walk, so the tests compare these
figures within stated tolerances, not samples. To regenerate:

```sh
cmake -S . -B <build> -DCAECILIA_CORE_ONLY=ON -DCAECILIA_BUILD_TESTS=OFF -DCAECILIA_BUILD_TOOLS=ON
cmake --build <build> --config Release --target caecilia-reference
<build>/tools/Release/caecilia-reference --out reference/cpp-1.x   # add --wav to listen
```

### What the references already show

These are 1.x defects measured, not suspected — each is a 2.0 requirement:

- **The wind is nearly dead.** With all 26 stops drawn and nine keys held (108
  voices) the Grand-Orgue chest drops from 812 to 809.6 Pa: **0.23 %**. No
  listener hears that, and the pitch and level coupling downstream of it are
  correspondingly inaudible (the 1.x `WindLoadTest` says as much).
- **The tremulant barely reaches the sound.** It swings the Récit pressure by
  220 Pa peak to peak (±15 %), yet the note's envelope moves 1.4 dB against
  0.8 dB of natural drift without it.
- **The release is one colour.** Release time depends only on pitch
  (100–414 ms across the compass) and the pipe fades as a whole, its upper
  partials no faster than its fundamental. (An earlier draft of this list said
  "release ignores hold time". For a dry modeled pipe that is not a defect: a
  pipe's own release is its resonator dying away, which does not depend on how
  long it spoke. The hold-time dependence a listener hears comes from the room
  -- a staccato has not filled it -- and the reverb models that. Sample organs
  need releases picked by hold time because their releases are RECORDINGS of
  the room after a long note.)
- **Every rank lives like a Principal.** The per-family "living pipe"
  character (drift, speech glide, bloom, treble tilt) is applied once, when the
  pool is built with a Principal context, and `AdditiveVoice::adoptRank` never
  re-applies it -- so a Gambe drifts and a Bourdon tilts exactly like a Montre.
  The port reproduces this to match the references
  (`AdditiveVoice::set_per_family_liveliness` is the fix, off until the
  calibration stage turns it on with its own tests).
- **Distinct stops sound identical.** Basson-Hautbois 8' and Trompette 8' give
  the same level and harmonic profile to two decimals; the three Bourdon 8'
  likewise. A voicing per stop, not per family, is a 2.0 requirement.

### How close the port is

The Rust engine (`crates/caecilia-engine/tests/reference.rs`) renders every
case of `reference.json` the way the C++ did and measures it the same way. The
largest level difference across all 78 rank notes is **0.0001 dB**; attack and
release land in the same 5 ms window; harmonic profiles agree within 0.05 dB;
chest pressures within 0.001 Pa block by block; the voice count of the chord is
identical block by block; the tremulant's pressure trajectory within 0.001 Pa.
The tests hold the port to those tolerances, so any later change that moves the
sound has to say so.

## 2.0 calibration: what changed, measured

`Calibration::OneX` reproduces 1.x (the reference tests run under it);
`Calibration::V2` is what 2.0 plays. `crates/caecilia-engine/tests/calibration.rs`
renders each case under both and asserts the change:

| Measured | 1.x | 2.0 | What changed |
|---|---:|---:|---|
| Grand-Orgue sag, whole organ drawn, nine keys | 0.29 % | 2.91 % | Each pipe draws ten times the flow: a well-winded organ that is heard to breathe. |
| Récit Trompette C4 when an 18-key chord lands | −0.6 cent | −6.1 cents | Sag plus a level axis derived from physics (amplitude ∝ pressure; reeds ∝ p^1.3). The reeds now dip against the flues. |
| Bourdon 8' envelope swing, tremulant at 0.6 | 1.44 dB | 2.64 dB | The same physics: the tremulant reaches the sound. |
| Tremulant swing across 64…2048-frame buffers | 0.35 dB spread | 0.01 dB spread | Wind, drift and speech re-read every 64 frames, not once per host buffer: the instrument sounds the same in every DAW. |
| Basson-Hautbois vs Trompette, largest harmonic difference | 0.6 dB | 9.3 dB | Reeds have voices of their own (formant, brightness, fundamental) in the organ. |
| Montre 8' C4, H4/H1 change over 80 ms of release | 0.0 dB | −4.0 dB | Exponential release whose upper partials die faster. |
| Montre 8' C4, release to −40 dB | 211 ms | 201 ms | Length kept; no click at the release (largest sample step at note-off below the steady state's). |
| Character per family (drift, glide, bloom, tilt) | every rank a Principal | each family its own | Applied when a voice adopts a rank. |

The cost, measured (`tests/cost.rs`, ignored by default): a 10-key tutti with
the couplers drawn -- 200 voices, about 3000 partials:

| Partial kernel | 1.x calibration | 2.0 calibration |
|---|---:|---:|
| Scalar (Caecilia's own, now the test reference) | 0.90 of a core | 1.03 |
| Manguebyte `PartialBank`, AVX2 | **0.148** | **0.187** |

The framework's bank renders each partial bit for bit as the scalar kernel did
(`the_framework_bank_renders_a_partial_exactly_as_the_reference_kernel`); only
the order the partials are summed in differs, and every reference test still
passes at its tolerance. The rest -- several cores for the largest
registrations -- is the real-time pool (§6 #3).

## 4. Problems to solve, and how 2.0 solves them

Market instruments are cited by documented behaviour only.

| Problem | Who has it | How 2.0 solves it |
|---|---|---|
| **RAM** — sample sets of tens of GB loaded whole into memory | Sample-based organs (Hauptwerk, GrandOrgue sets) | The additive/modeled tier needs no samples at all. The sample tier streams from disk: attack heads preloaded, the rest read ahead on a non-RT thread, kept in RAM losslessly compressed (§6 #1). |
| **Load time** — minutes to open a large organ | Same | Additive organs build in milliseconds. Sample sets load lazily: playable as soon as heads are in, with a cache of decoded heads keyed by file hash. |
| **Polyphony / CPU** — a tutti with couplers is thousands of pipes | All; 1.x is one thread | SIMD partial kernel checked against a scalar reference; voices rendered across cores with a deterministic sum (§6 #3, #4); a per-block budget that demotes a voice's tier, then sheds partials, before it ever drops a block (§6 #8). |
| **Audible loops** in sustained samples | Sample-based organs | The modeled sustain is loop-free by construction. Where a sample is used, the recorded attack is spliced into a modeled sustain of the same spectrum (the 1.x design, never built). |
| **Release does not match hold time** | Sample sets without multiple releases | The modeled pipe's release is its resonator decaying -- exponential and darkening -- and the room's share comes from the reverb, so a staccato is short by construction. For the sample tier: releases picked by hold time with a phase-aligned crossfade (§6 #5). |
| **Dead wind** — no response to what is played | Sample-based organs; a single global tremulant in some modeled ones; 1.x at 0.23 % | Reservoir, trunk and chest calibrated against published measurements so a full chord sags audibly; sag drives pitch, level and brightness per family on curves stated in the organ file; tests assert the sag and the pitch dip lie in stated ranges. |
| **Weak tremulant** | 1.x (measured above) | Tremulant modulation reaching level, pitch and brightness by amounts a test asserts, per chest, with its own start-up and stop ramp. |
| **Fixed tuning** — sets recorded in one temperament; retuning degrades | Sample-based organs | Pitch is synthesised: any temperament, Scala `.scl/.kbm`, per-stop detune, live click-free change. For samples, high-quality polyphase sinc retuning that also carries the wind's FM (§6 #2, #6). |
| **Latency** | Large-buffer setups; 1.x look-ahead limiter (2.5 ms) | Sample-accurate note events; no look-ahead in the default chain (limiter optional, its latency reported); small-buffer operation proven by a test at block sizes 1–64. |
| **One stereo pair** — no per-division outputs for real speakers | Many | Multichannel outputs per division/chest (§6 #7). |
| **Same voice for different stops** | 1.x (measured above) | Voicing per stop in the organ file, with the demo organ's stops voiced apart. |
| **Half the brain unwired** — registration engine, selectors, control API, sequencer, couplers' provenance | 1.x | Registration engine wired from the start: console, MIDI learn, OSC/JSON-RPC and pistons all go through one command bus and one history. |
| **Inaccessible UI** — a WebView is opaque to screen readers; needs WebView2 | 1.x | Native `manguebyte-ui` console with its accessibility layer and golden-image tests. |
| **Licence** — JUCE makes the binary AGPL | 1.x | No JUCE: the whole product is Apache-2.0. |

## 5. Planned workspace

```
Cargo.toml                    workspace; manguebyte-* by path (git+rev at release)
crates/
  caecilia-model/             ids, Footage, Organ, organ file (serde_json), demo organ
                              (tuning: the framework's module; Caecilia adds per-pipe detune)
  caecilia-wind/              bellows, chests, tremulant, response curves
  caecilia-synth/             partial bank, rank voicing, sample tier, physical tier
  caecilia-engine/            the organ engine: keys → ranks → voices → chest buses → master
  caecilia-registration/      selectors, commands, history, plenum, explain, port
  caecilia-control/           OSC + JSON-RPC over one command sink
  caecilia-plugin/            Plugin impl; CLAP/VST3/AU/LV2 + standalone
  caecilia-ui/                the console
  caecilia-tools/             reference comparison, fingerprint, bench
reference/cpp-1.x/            reference.json from tools/reference
```

Every crate that runs on the audio thread has a `tests/realtime.rs` with the
framework's counting allocator: prepare, then block sizes 256, 1, 64, 255, 256
and a reset, asserting zero heap calls.

## 6. What the framework needs to give (requested from Manguebyte)

In priority order — the order the engine reaches them. The API is a proposal.
The framework's owner adopted the list (2026-10-29) and scheduled it as follows;
Caecilia does not write its own version of anything listed here.

| Items | Status in Manguebyte |
|---|---|
| 2, 5, 6 | In progress (sampler/streaming work). Streaming ships as an independent `SampleStore`/`StreamCursor` layer that honours and reports `ram_budget`, with the `Sampler` built on it; the per-sample variable-ratio sinc reader is a public type the sampler's voices also use. |
| 1, 4 | Next free slot: `PartialBank` and the general kernels on `core::vector` (x86/NEON/wasm, runtime detection), tested against the scalar reference with a stated error. Until then Caecilia renders with a local scalar kernel and swaps. |
| 3 | Right after 1 and 4: `RtPool`, fixed-order sum, bit-identical to single-threaded. |
| 7 | The framework owns tuning (Parabolic uses it too): historical temperaments, Scala, stretch. Caecilia uses 12-TET until it lands and then tests it against the `tuning` tables in `reference/cpp-1.x`. |
| 8, 9 | After those. |
| 10 | Requested 2026-10-30, with the organ change it serves. |

1. **Sine/partial-bank kernel** (`manguebyte-dsp`): N phasors with per-partial
   frequency, gain and pan ramps over a block, accumulated into a stereo buffer;
   scalar reference + SIMD backends behind `core::vector`, and a test that the
   backends match the reference within a stated error.
   `PartialBank::new(max) / set(i, Partial{freq_hz, gain, pan}) / ramp_to(i, …, frames) / render_add(&mut [f32], &mut [f32])`.
2. **Disk streaming with preload and lossless in-RAM compression**
   (`manguebyte-formats` or `-sampler`): a sample store that holds each sample's
   head decoded, streams the tail on a background thread, keeps cached data
   compressed losslessly, and serves the audio thread without blocking.
   `SampleStore::open(paths, StoreConfig{head_frames, ram_budget}) → SampleHandle; StreamCursor::read(&mut [f32]) → Frames | Underrun`.
3. **Real-time multicore voice render** (`manguebyte-dsp` or `-core`): a pool of
   pinned worker threads created at prepare; per block, a slice of jobs rendered
   in parallel into per-job buffers and summed in a fixed order, so the output is
   bit-identical to the single-threaded render. No locks or allocation on the
   audio thread; the audio thread waits on a spin/futex barrier with a deadline.
   `RtPool::new(threads) / pool.render(&mut jobs, |job, buf| …)`.
4. **SIMD kernels with scalar-reference tests** generally: gain ramp, mix-add,
   biquad block, as the partial kernel above.
5. **Release selection by hold time with phase-aligned crossfade**
   (`manguebyte-sampler`): given held time and a set of releases, pick one and
   crossfade from the sustain at a phase-matched point.
6. **High-quality polyphase sinc interpolation, variable ratio**, with the ratio
   modulated per sample (the wind's FM): `SincResampler::new(taps, phases)`
   / `read(source, ratio_per_sample: &[f32], out)`.
7. **Temperaments and Scala** (`.scl` / `.kbm` parse into a 128-note table,
   historical temperaments, stretch) — a framework module.
8. **Multichannel outputs per division**: plugin layouts with named auxiliary
   output buses in every wrapper, and the standalone mapping them to device
   channels.
9. **Per-block CPU budget with graceful degradation**: a governor that measures
   block time against the deadline and returns a budget in cost units the
   instrument spends (demote tier, shed partials, steal), disabled in offline
   render.
10. **Parameter names that change while the plugin runs**: a parameter's
   display name settable from any thread but the audio one, and the wrappers
   telling the host (CLAP `rescan(CLAP_PARAM_RESCAN_INFO)`, VST3
   `restartComponent(kParamTitlesChanged)`). Caecilia's stop and coupler
   parameters are pools drawn by number; after an organ change they draw the
   new organ's stops but keep the first organ's names until the host reloads
   the plugin.

## 7. Status (2026-10-30)

| Stage | State |
|---|---|
| Map and references | Done: this document, `tools/reference`, `reference/cpp-1.x`. |
| Sound engine | Done: model, wind, partial bank on the framework's SIMD `PartialBank`, engine; within 0.0001 dB of 1.x under `Calibration::OneX`, and calibrated past it under `V2`. A 200-voice tutti costs 0.19 of a core. |
| Threads and tuning | Done: voices render on Manguebyte's `RenderPool` in groups of 64, summed in order (bit for bit the single-threaded output, `tests/threads.rs`); the plugin uses every core but the audio thread's. Tuning is Manguebyte's module: 8 temperaments on any key, A4 380-480 Hz as host parameters, and any equal division or Scala scale through the controller; the 12 tables of 1.x match to 1e-9 cent. Scala choices are not yet saved with the session. |
| Organ file | Done: 1.x's JSON format read as it is (the 1.x example loads as the demo organ and voices identically), 2.0 additions (`formatVersion`, per-rank `recipe`), a writer that reads back to the same organ, errors with their path and the fix, spectra only from inside the organ's folder; `caecilia-organ-file --export/--check`, `caecilia --organ`, `CAECILIA_ORGAN` for plugins. The organ changes while the plugin plays (`CaeciliaParams::load_organ_file`, the controller's `load_organ`, `organ <file>` typed into the standalone): built off the audio thread, held keys carry over, the old organ fades over 50 ms and is dropped off the audio thread; its opening registration and factory generals come with it, stop and coupler learns are forgotten, a custom tuning stays. The session saves the organ's file and restores it before its registration. Not yet: the host's parameter names follow the new organ (framework item 10). |
| Registration | Done: selectors and grammar 1.0, commands, branching history with provenance, plenum, explain, graded `port()` across traditions, pistons, factory generals. |
| Plugin | CLAP and VST3 build, load in Manguebyte's host and play (`examples/host_check.rs`); stops and couplers as host parameters, MIDI channels to divisions, CC 11/64/120/123, tremulant, swell, reverb, safety limiter. Console: 16 general pistons (factory crescendo + 8 of the organist's), program change 0-15, MIDI learn of any controller or spare key (stop, coupler, piston, next/previous, cancel), all saved with the session; a `RegistrationController` with selectors, undo/redo across the host's automation and the pistons, `explain` and piston capture. Standalone app (`--features standalone --bin caecilia`), built, not run by tests. `process` proven allocation-free through all of it, an organ change included. Not yet: LV2/AU. |
| Console | Part 1 of 4 done: the approved mockup is the plugin's editor (`src/ui`, 1600 x 980), laid out from the organ the instance plays (bottom manual and pedal on the left jamb, the other manuals on the right, couplers and tremulant, the manuals top first with the key slip above the lowest, the pedalboard from the pedal's compass) and showing what plays, read once a frame: stops and couplers drawn, the general recalled, every key and pedal held, each chest's wind with its last two seconds, the history and why, the tuning, the load. The audio thread reports through `Telemetry` (atomics, nothing allocated). Each part says what it shows to assistive technology; `tests/console_ui.rs` opens it headless through the plugin and reads that. Next: clicks (part 2), the selector line and history controls (3), day and night, organ changes and the hosts' names (4). |

## 8. Stages

1. **Map and references** — this document, `tools/reference`, `reference/cpp-1.x`.
2. **Sound engine** — `caecilia-model` (with the demo organ's voicing reproduced
   exactly against the reference), 12-TET (the framework's tuning module, tested
   against the reference tables, when it lands),
   `caecilia-wind`, `caecilia-synth` (partial bank), `caecilia-engine`; tests
   against `rankNotes`, `windChord`, `tremulant`, and real-time tests. Then the
   wind and tremulant calibrated past 1.x, with the tests saying by how much.
3. **Registration** — selectors, commands, history, plenum, explain, port; wired
   to the engine.
4. **Plugin formats** — CLAP, VST3, AU, LV2, standalone; state; host tests with
   the framework's `Harness`.
5. **Console** — native, on `manguebyte-ui`/`manguebyte-gui`; no WebView2 and
   no JavaScript (owner's decision, 2026-10-29). It must be far better and more
   beautiful than the 1.x web console: `docs/UI_DESIGN.md` and the 1.x mockups
   are the starting point, not the target. A modern organ console — drawstops
   that animate, pistons, expression shoes, wind gauges that fall under
   polyphony, divisions on pages. An idle console costs ~0 CPU, proven by a
   test, and never touches the audio thread. **A mockup comes first**, rendered
   headless by the toolkit itself (as `manguebyte-ui/examples/sampler_screen.rs`
   does), and is shown to the owner for approval before any UI code is
   written. Anything GENERAL the widgets need (a drawing primitive, images,
   gradients, an input behaviour, a reusable widget) is asked of the framework.
6. **Beyond 1.x** — sample tier with streaming, multicore, multichannel, Scala,
   per-stop voicing in the organ file, physical tier.
7. **Release** — framework by `git` + `rev`, C++ removed, 2.0 merged when the
   owner asks.
