/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// ===========================================================================
// Caecilia demo render / verification harness.
//
// A standalone `main` that proves the whole audio product end-to-end:
//   1. builds the Caecilia demonstration organ (model::buildCaeciliaDemoOrgan),
//   2. draws a full grand-plenum registration plus flute/solo colour and turns
//      it into a bound AdditiveVoice pool (model::buildDemoRegistrationVoices),
//   3. wires a real signal chain into a core::engine::AudioEngine — a WindModel
//      (setWindSupply), an equal-tempered TuningModel (setTuning) and a Cathedral
//      FdnReverb (setMasterReverb),
//   4. plays a short 4-chord I-IV-V-I progression across the manuals + pedal by
//      pushing NoteOn/NoteOff EngineCommands with sample-accurate timing,
//   5. renders ~8 s to a normalised (~-3 dBFS) stereo WAV, and
//   6. SELF-VERIFIES the result: a Goertzel analysis proves the intended
//      chord-tone pitches dominate deliberately off-note bins, and an energy
//      analysis proves the reverb tail persists (and decays) after the final
//      release.
//
// It uses ONLY the audited public signatures of the modules. It is NOT part of
// the CMake build; the caller compiles it against libcaecilia_core.a with
// `-I C:\Ceciliae\src`, e.g.
//   g++ -std=c++20 -O2 -I C:/Ceciliae/src tools/dev/render_demo.cpp \
//       -L build -lcaecilia_core -o render_demo
// ===========================================================================

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Rank.h"
#include "caecilia/model/Stop.h"
#include "caecilia/tuning/TuningModel.h"
#include "caecilia/wind/WindModel.h"
#include "caecilia/wind/WindTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{
namespace core  = caecilia::core;
namespace eng   = caecilia::core::engine;
namespace model = caecilia::model;
namespace dsp   = caecilia::dsp;
namespace wind  = caecilia::wind;
namespace tune  = caecilia::tuning;

// --- Fixed render format ---------------------------------------------------
constexpr double      kSampleRate = 48000.0;
constexpr std::size_t kBlock      = 512;
constexpr std::size_t kChannels   = 2;
constexpr double      kDurationSec = 8.0;
constexpr std::size_t kPoolCapacity = 120;   // rich plenum: ~4-5 voices/stop across 26 stops
constexpr std::size_t kVoicesPerNote = 6;    // stacked timbres per key => a real plenum
constexpr core::Velocity kVelocity  = 100;

// -3 dBFS linear target for the final normalisation.
constexpr double kTargetPeak = 0.707945784; // 10^(-3/20)

// --- The 4-chord progression (I-IV-V-I in C major) -------------------------
// notes[0] is the pedal bass; notes[1..3] are the manual triad.
struct Chord
{
    int    notes[4];
    double onSec;
    double offSec;
};

constexpr std::array<Chord, 4> kProgression{{
    // C major:  pedal C2 + C4 E4 G4
    { { 36, 60, 64, 67 }, 0.10, 1.50 },
    // F major:  pedal F2 + C4 F4 A4
    { { 41, 60, 65, 69 }, 1.65, 3.05 },
    // G major:  pedal G2 + D4 G4 B4
    { { 43, 62, 67, 71 }, 3.20, 4.60 },
    // C major:  pedal C2 + C4 E4 G4   (this is the analysis chord)
    { { 36, 60, 64, 67 }, 4.75, 6.30 },
}};

// The final chord is what we analyse for pitch. Its release ends the piece,
// leaving a pure reverb tail from ~6.45 s onward.
constexpr std::size_t kAnalysisChord = 3;

// --- Small helpers ---------------------------------------------------------
[[nodiscard]] double noteToHz(int midiNote) noexcept
{
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

[[nodiscard]] std::size_t secToFrame(double sec) noexcept
{
    return static_cast<std::size_t>(sec * kSampleRate + 0.5);
}

// Goertzel single-bin power (energy) of x[0..N) at frequency freq.
[[nodiscard]] double goertzelPower(const float* x, std::size_t n, double freq) noexcept
{
    const double w     = 2.0 * 3.14159265358979323846 * freq / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double s0 = static_cast<double>(x[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return (n > 0) ? power / static_cast<double>(n) : 0.0;
}

[[nodiscard]] double rmsRange(const std::vector<float>& l,
                              const std::vector<float>& r,
                              std::size_t a, std::size_t b) noexcept
{
    a = std::min(a, l.size());
    b = std::min(b, l.size());
    if (b <= a)
        return 0.0;
    double sumSq = 0.0;
    for (std::size_t i = a; i < b; ++i)
        sumSq += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
    const std::size_t n = (b - a) * 2;
    return std::sqrt(sumSq / static_cast<double>(n));
}

[[nodiscard]] double toDb(double lin) noexcept
{
    return lin > 1.0e-12 ? 20.0 * std::log10(lin) : -240.0;
}

// Minimal 16-bit PCM stereo WAV writer.
bool writeWav16(const std::string& path,
                const std::vector<float>& l,
                const std::vector<float>& r)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    const std::uint32_t sr        = static_cast<std::uint32_t>(kSampleRate);
    const std::uint16_t channels  = 2;
    const std::uint16_t bits       = 16;
    const std::uint32_t frames     = static_cast<std::uint32_t>(l.size());
    const std::uint32_t byteRate   = sr * channels * (bits / 8);
    const std::uint16_t blockAlign = channels * (bits / 8);
    const std::uint32_t dataBytes  = frames * blockAlign;
    const std::uint32_t riffBytes  = 36 + dataBytes;

    auto put32 = [&](std::uint32_t v) { f.put(char(v & 0xFF)).put(char((v >> 8) & 0xFF)).put(char((v >> 16) & 0xFF)).put(char((v >> 24) & 0xFF)); };
    auto put16 = [&](std::uint16_t v) { f.put(char(v & 0xFF)).put(char((v >> 8) & 0xFF)); };

    f.write("RIFF", 4); put32(riffBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(16); put16(1); put16(channels);
    put32(sr); put32(byteRate); put16(blockAlign); put16(bits);
    f.write("data", 4); put32(dataBytes);

    for (std::uint32_t i = 0; i < frames; ++i)
    {
        auto q = [](float s) -> std::int16_t {
            const float c = std::clamp(s, -1.0f, 1.0f);
            const int   v = static_cast<int>(std::lround(c * 32767.0f));
            return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
        };
        const std::int16_t sl = q(l[i]);
        const std::int16_t sr16 = q(r[i]);
        put16(static_cast<std::uint16_t>(sl));
        put16(static_cast<std::uint16_t>(sr16));
    }
    return static_cast<bool>(f);
}

// A time-stamped engine command.
struct Event
{
    std::size_t   frame;
    eng::EngineCommand cmd;
};

} // namespace

int main()
{
    std::printf("=== Caecilia demo render / verification ===\n");

    // -----------------------------------------------------------------------
    // 1. Build the instrument.
    // -----------------------------------------------------------------------
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    std::printf("Organ: \"%s\" by %s (%d) - %zu stops, %zu ranks, %zu windchests\n",
                organ.name().c_str(), organ.builder().c_str(), organ.year(),
                organ.stops().size(), organ.ranks().size(), organ.windchests().size());

    // -----------------------------------------------------------------------
    // 2. Draw a full grand plenum + flute/solo colour registration.
    //    We engage every stop so the bound pool spans every tonal family
    //    (principals, flutes, strings, reeds, mutations and mixtures): a true
    //    plenum with solo/flute colour available for the lighter passages.
    // -----------------------------------------------------------------------
    std::vector<core::StopId> engaged;
    engaged.reserve(organ.stops().size());
    for (const model::Stop& s : organ.stops())
        engaged.push_back(s.id());

    // -----------------------------------------------------------------------
    // 3a. Tuning: an equal-tempered oracle at A4 = 440 Hz (TuningModel default).
    // -----------------------------------------------------------------------
    tune::TuningModel tuning; // equal temperament, A4 = 440, no stretch/detune.

    // -----------------------------------------------------------------------
    // 3b. Wind: a WindModel with one bellows+chest per division, so each rank's
    //     windchest reads a stable nominal pressure. Wired as the engine's wind
    //     supply and as each voice's wind coupling source.
    // -----------------------------------------------------------------------
    wind::WindModel windModel;
    windModel.prepare(kSampleRate, kBlock);
    {
        wind::WindModelConfig cfg;
        const auto nChests = organ.windchests().size();
        cfg.bellows.resize(nChests);
        cfg.chests.resize(nChests);
        for (std::size_t i = 0; i < nChests; ++i)
        {
            const float nomPa = organ.windchests()[i].nominalPressurePa;
            cfg.bellows[i].nominalPressurePa = nomPa;
            cfg.chests[i].id                 = core::WindchestId{ static_cast<std::uint16_t>(i) };
            cfg.chests[i].bellowsIndex       = static_cast<std::uint16_t>(i);
            cfg.chests[i].tremulantIndex     = -1;   // tremulants left disengaged for a clean pitch render
            cfg.chests[i].nominalPressurePa  = nomPa;
        }
        // Bind every rank to its own windchest so chestForPipe() routes correctly.
        for (const model::Rank& rk : organ.ranks())
            cfg.pipeBindings.push_back(wind::PipeChestBinding{ rk.id(), rk.windchest() });
        windModel.configure(cfg);
        windModel.reset();
    }

    // -----------------------------------------------------------------------
    // 2b. Materialise the voice pool for the registration.
    // -----------------------------------------------------------------------
    model::DemoVoiceBank bank = model::buildDemoRegistrationVoices(
        organ, engaged, &windModel, &tuning, kSampleRate, kBlock, kPoolCapacity);
    if (bank.empty())
    {
        std::printf("FAIL: registration produced no voices.\n");
        return 1;
    }
    std::printf("Registration: %zu stops engaged -> %zu voices bound.\n",
                engaged.size(), bank.size());

    // -----------------------------------------------------------------------
    // 3c. Reverb: Cathedral FdnReverb on the master chain.
    // -----------------------------------------------------------------------
    dsp::FdnReverb reverb;
    reverb.prepare(kSampleRate, kBlock, kChannels);
    reverb.setPreset(dsp::ReverbPreset::Cathedral);
    const core::ReverbParams rp = reverb.params();
    std::printf("Reverb: Cathedral (mix %.2f, decay %.1fs, preDelay %.0fms).\n",
                rp.mix, rp.decaySec, rp.preDelayMs);

    // -----------------------------------------------------------------------
    // 3d. Assemble the engine and bind everything.
    // -----------------------------------------------------------------------
    eng::AudioEngine engine;
    engine.prepare(kSampleRate, kBlock, kChannels, organ.windchests().size());
    engine.setTuning(&tuning);
    engine.setWindSupply(&windModel);
    engine.setMasterReverb(&reverb);
    engine.bindVoices(bank.data(), bank.size());

    // -----------------------------------------------------------------------
    // 4. Build the sample-accurate command schedule.
    //    Each key is voiced by kVoicesPerNote stacked voices (distinct PipeIds
    //    so the pool grants distinct slots); the engine grants whatever free
    //    voices it has, stacking a plenum of timbres on each pitch.
    // -----------------------------------------------------------------------
    std::vector<Event> events;
    const core::DivisionId anyDiv{ 0 }; // the engine ignores division on note routing
    for (const Chord& ch : kProgression)
    {
        const std::size_t onF  = secToFrame(ch.onSec);
        const std::size_t offF = secToFrame(ch.offSec);
        for (int note : ch.notes)
        {
            for (std::size_t k = 0; k < kVoicesPerNote; ++k)
            {
                const core::PipeId pipe{ static_cast<std::uint16_t>(k),
                                         static_cast<std::uint8_t>(note) };
                events.push_back({ onF,  eng::EngineCommand::makeNoteOn(pipe, kVelocity, anyDiv) });
                events.push_back({ offF, eng::EngineCommand::makeNoteOff(pipe, anyDiv) });
            }
        }
    }
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.frame < b.frame; });

    // -----------------------------------------------------------------------
    // 5. Render loop, block by block.
    // -----------------------------------------------------------------------
    const std::size_t totalFrames = secToFrame(kDurationSec);
    std::vector<float> outL(totalFrames, 0.0f);
    std::vector<float> outR(totalFrames, 0.0f);

    std::array<float, kBlock> scratchL{};
    std::array<float, kBlock> scratchR{};
    float* chans[kChannels] = { scratchL.data(), scratchR.data() };

    std::size_t pos       = 0;
    std::size_t nextEvent = 0;
    std::size_t peakActive = 0;
    while (pos < totalFrames)
    {
        const std::size_t n = std::min(kBlock, totalFrames - pos);

        // Push every command due within this block; the engine drains the ring
        // at the top of processBlock().
        while (nextEvent < events.size() && events[nextEvent].frame < pos + n)
        {
            (void) engine.commandQueue().push(events[nextEvent].cmd);
            ++nextEvent;
        }

        core::AudioBlock block(chans, kChannels, n);
        engine.processBlock(block);

        peakActive = std::max(peakActive, engine.activeVoiceCount());

        for (std::size_t i = 0; i < n; ++i)
        {
            outL[pos + i] = scratchL[i];
            outR[pos + i] = scratchR[i];
        }
        pos += n;
    }
    std::printf("Rendered %zu frames (%.2fs); peak simultaneous voices: %zu.\n",
                totalFrames, kDurationSec, peakActive);

    // -----------------------------------------------------------------------
    // 5b. Normalise to ~-3 dBFS.
    // -----------------------------------------------------------------------
    float peak = 0.0f;
    for (std::size_t i = 0; i < totalFrames; ++i)
    {
        peak = std::max(peak, std::fabs(outL[i]));
        peak = std::max(peak, std::fabs(outR[i]));
    }
    if (peak <= 1.0e-9f)
    {
        std::printf("FAIL: rendered signal is silent (peak %.3e).\n", peak);
        return 1;
    }
    const float gain = static_cast<float>(kTargetPeak) / peak;
    for (std::size_t i = 0; i < totalFrames; ++i)
    {
        outL[i] *= gain;
        outR[i] *= gain;
    }
    const double normPeakDb = toDb(kTargetPeak);
    std::printf("Normalised: raw peak %.4f -> %.2f dBFS.\n", peak, normPeakDb);

    // -----------------------------------------------------------------------
    // 5c. Write the WAV.
    // -----------------------------------------------------------------------
    const std::string wavPath = "tools/dev/caecilia-demo.wav";
    if (!writeWav16(wavPath, outL, outR))
        std::printf("WARNING: could not write %s (checks still run).\n", wavPath.c_str());
    else
        std::printf("Wrote %s (%.2fs stereo, 16-bit, %.0f Hz).\n",
                    wavPath.c_str(), kDurationSec, kSampleRate);

    // =======================================================================
    // 6. SELF-VERIFICATION.
    // =======================================================================
    std::printf("\n--- Verification ---\n");
    bool pass = true;

    // -- Check A: pitch. The intended chord-tone fundamentals must dominate a
    //    set of deliberately off-note bins (a quarter-tone either side of each
    //    chord tone, plus three absolute non-chord pitches), measured by
    //    Goertzel over a window well inside the final chord's sustain.
    const Chord& ac = kProgression[kAnalysisChord];
    const std::size_t winStart = secToFrame(5.10);
    const std::size_t winLen   = secToFrame(0.80);
    std::vector<float> mono(winLen, 0.0f);
    for (std::size_t i = 0; i < winLen; ++i)
    {
        const std::size_t s = winStart + i;
        mono[i] = 0.5f * (outL[s] + outR[s]);
    }

    // Chord-tone target frequencies (the played keys' equal-tempered pitches).
    std::array<double, 4> chordHz{};
    for (int k = 0; k < 4; ++k)
        chordHz[static_cast<std::size_t>(k)] = noteToHz(ac.notes[k]);

    // Off-note bins: +/-50 cents from every chord tone (guaranteed non-harmonic
    // and adjacent - the sharpest discriminator) ...
    const double centUp   = std::pow(2.0,  0.5 / 12.0);
    const double centDown = std::pow(2.0, -0.5 / 12.0);
    std::vector<double> offHz;
    for (double f : chordHz)
    {
        offHz.push_back(f * centUp);
        offHz.push_back(f * centDown);
    }
    // ... plus three absolute pitches that are NEVER played anywhere in the
    //     progression (so no direct tone and no reverb-tail energy lands on
    //     them) and are not low harmonics of any chord tone: Bb3, Eb4, F#4.
    offHz.push_back(noteToHz(58)); // Bb3 233.08 Hz
    offHz.push_back(noteToHz(63)); // Eb4 311.13 Hz
    offHz.push_back(noteToHz(66)); // F#4 369.99 Hz

    double minChord = 1.0e300;
    std::printf("Chord-tone Goertzel power (window 5.10-5.90s):\n");
    for (int k = 0; k < 4; ++k)
    {
        const double p = goertzelPower(mono.data(), winLen, chordHz[static_cast<std::size_t>(k)]);
        minChord = std::min(minChord, p);
        std::printf("   note %-3d  %8.2f Hz : %.4e\n",
                    ac.notes[k], chordHz[static_cast<std::size_t>(k)], p);
    }

    double maxOff = 0.0;
    double maxOffHz = 0.0;
    for (double f : offHz)
    {
        const double p = goertzelPower(mono.data(), winLen, f);
        if (p > maxOff) { maxOff = p; maxOffHz = f; }
    }
    std::printf("Loudest off-note bin: %8.2f Hz : %.4e\n", maxOffHz, maxOff);

    const double pitchRatio = maxOff > 0.0 ? minChord / maxOff : 1.0e300;
    const double kPitchThreshold = 4.0;
    const bool pitchPass = pitchRatio > kPitchThreshold;
    std::printf("Pitch dominance: weakest chord tone / loudest off-note = %.1fx "
                "(threshold %.1fx) -> %s\n",
                pitchRatio, kPitchThreshold, pitchPass ? "PASS" : "FAIL");
    pass = pass && pitchPass;

    // -- Check B: reverb tail. After the final release (~6.30s note-off; voices
    //    fully silent by ~6.45s given the 0.15s release), audible energy must
    //    persist and decay - the Cathedral tail.
    const double sustainRms = rmsRange(outL, outR, secToFrame(5.10), secToFrame(5.90));
    const double earlyTail  = rmsRange(outL, outR, secToFrame(6.55), secToFrame(6.95));
    const double lateTail   = rmsRange(outL, outR, secToFrame(7.50), secToFrame(7.90));

    std::printf("\nReverb tail RMS:\n");
    std::printf("   sustain  (5.10-5.90s): %.4e (%.1f dBFS)\n", sustainRms, toDb(sustainRms));
    std::printf("   early tail(6.55-6.95s): %.4e (%.1f dBFS)\n", earlyTail, toDb(earlyTail));
    std::printf("   late tail (7.50-7.90s): %.4e (%.1f dBFS)\n", lateTail, toDb(lateTail));

    const bool tailPresent = earlyTail > 1.0e-4 && earlyTail < sustainRms;
    const bool tailDecays  = earlyTail > lateTail && lateTail > 1.0e-6;
    const bool tailPass    = tailPresent && tailDecays;
    std::printf("Tail persists after release: %s;  tail decays: %s (%.1f dB over 0.95s) -> %s\n",
                tailPresent ? "yes" : "no",
                tailDecays ? "yes" : "no",
                toDb(lateTail) - toDb(earlyTail),
                tailPass ? "PASS" : "FAIL");
    pass = pass && tailPass;

    // -----------------------------------------------------------------------
    // Verdict.
    // -----------------------------------------------------------------------
    std::printf("\n=== %s ===\n", pass ? "OVERALL: PASS" : "OVERALL: FAIL");
    return pass ? 0 : 1;
}
