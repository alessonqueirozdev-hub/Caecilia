// Caecilia v0.1 — first-sound harness.
// Drives the real AudioEngine + AdditiveVoice chain, plays a Principal-8' C-major
// chord, writes a WAV, and verifies (Goertzel) that the note pitches dominate.
// Links against libcaecilia_core.a. NOT shipped; a proof-of-life for v0.1.

#include "caecilia/engine/AudioEngine.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/synthesis/VoiceContext.h"

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <fstream>

using namespace caecilia;
using core::engine::AudioEngine;
using core::engine::EngineCommand;
using synth::AdditiveVoice;
using synth::SpectralModel;
using synth::PartialTrack;
using synth::VoiceContext;

static SpectralModel makePrincipal8()
{
    // A diapason-ish spectrum: strong fundamental with a falling harmonic series.
    SpectralModel m;
    const float ampDb[] = { 0.0f, -8.0f, -13.0f, -18.0f, -22.0f, -27.0f, -32.0f };
    for (int h = 0; h < 7; ++h) {
        PartialTrack t;
        t.ratioToF0 = float(h + 1);
        t.ampDb     = ampDb[h];
        m.partials.push_back(t);
    }
    m.fundamentalHz = 0.0f; // set per note by trigger()
    return m;
}

// Goertzel magnitude of frequency f over buffer.
static double goertzel(const std::vector<float>& x, double sr, double f)
{
    const double w = 2.0 * M_PI * f / sr;
    const double c = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return std::sqrt(re * re + im * im) / double(x.size());
}

static void writeWav(const char* path, const std::vector<float>& L,
                     const std::vector<float>& R, int sr)
{
    const uint32_t n = uint32_t(L.size());
    const uint16_t ch = 2, bits = 16;
    const uint32_t byteRate = sr * ch * bits / 8;
    const uint16_t blockAlign = ch * bits / 8;
    const uint32_t dataBytes = n * blockAlign;
    std::ofstream o(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ o.write((char*)&v,4); };
    auto u16 = [&](uint16_t v){ o.write((char*)&v,2); };
    o.write("RIFF",4); u32(36 + dataBytes); o.write("WAVE",4);
    o.write("fmt ",4); u32(16); u16(1); u16(ch); u32(sr); u32(byteRate); u16(blockAlign); u16(bits);
    o.write("data",4); u32(dataBytes);
    for (uint32_t i = 0; i < n; ++i) {
        auto clip = [](float v){ v = v < -1 ? -1 : (v > 1 ? 1 : v); return int16_t(v * 32767.0f); };
        int16_t l = clip(L[i]), r = clip(R[i]);
        o.write((char*)&l,2); o.write((char*)&r,2);
    }
}

int main()
{
    const double SR = 48000.0;
    const std::size_t BLOCK = 512;
    const int N = 16;

    // Build & prepare the voices.
    SpectralModel model = makePrincipal8();
    VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight; // 8' -> key pitch
    std::vector<AdditiveVoice> voices(N);
    std::vector<core::IVoice*> ptrs(N);
    for (int i = 0; i < N; ++i) {
        voices[i].prepare(SR, BLOCK);
        voices[i].setContext(ctx);
        voices[i].seedFrom(model);
        ptrs[i] = &voices[i];
    }

    AudioEngine engine;
    engine.prepare(SR, BLOCK, 2, 1);
    engine.bindVoices(ptrs.data(), N);

    // C major chord (C4, E4, G4).
    const core::MidiNote notes[] = { 60, 64, 67 };
    const double noteHz[] = { 261.6256, 329.6276, 391.9954 };
    core::DivisionId div{0};
    for (core::MidiNote n : notes) {
        core::PipeId pipe{ /*rankId*/ 0, n };
        engine.commandQueue().push(EngineCommand::makeNoteOn(pipe, 100, div));
    }

    // Render 3.0 s: hold 2.0 s, release, 1.0 s tail.
    const double totalSec = 3.0, releaseAt = 2.0;
    const std::size_t totalFrames = std::size_t(totalSec * SR);
    std::vector<float> L, R; L.reserve(totalFrames); R.reserve(totalFrames);
    std::vector<float> mono; mono.reserve(totalFrames);

    std::vector<float> chL(BLOCK), chR(BLOCK);
    float* chans[2] = { chL.data(), chR.data() };
    bool released = false;
    for (std::size_t pos = 0; pos < totalFrames; pos += BLOCK) {
        if (!released && double(pos) / SR >= releaseAt) {
            for (core::MidiNote n : notes)
                engine.commandQueue().push(EngineCommand::makeNoteOff(core::PipeId{0, n}, div));
            released = true;
        }
        const std::size_t frames = std::min(BLOCK, totalFrames - pos);
        core::AudioBlock out(chans, 2, frames);
        engine.processBlock(out);
        for (std::size_t f = 0; f < frames; ++f) {
            L.push_back(chL[f]); R.push_back(chR[f]);
            mono.push_back(0.5f * (chL[f] + chR[f]));
        }
    }

    // Peak / normalize for the WAV (target -3 dBFS).
    float peak = 1e-9f;
    for (float v : mono) peak = std::max(peak, std::abs(v));
    const float norm = 0.708f / peak;
    for (auto& v : L) v *= norm;
    for (auto& v : R) v *= norm;
    writeWav("caecilia-first-sound.wav", L, R, int(SR));

    // ---- Verify pitch: sustained window [0.3, 1.8] s ----
    const std::size_t a = std::size_t(0.3 * SR), b = std::size_t(1.8 * SR);
    std::vector<float> win(mono.begin() + a, mono.begin() + b);
    printf("peak(before norm)=%.4f  frames=%zu  activeVoices=%zu\n",
           peak, mono.size(), engine.activeVoiceCount());

    double noteE[3], noiseE[3];
    const double noiseHz[] = { 300.0, 455.0, 545.0 }; // between the notes, not harmonics
    bool pass = true;
    for (int i = 0; i < 3; ++i) {
        noteE[i]  = goertzel(win, SR, noteHz[i]);
        noiseE[i] = goertzel(win, SR, noiseHz[i]);
        const double ratio = noteE[i] / (noiseE[i] + 1e-12);
        printf("  note %.2f Hz: E=%.5f | off-note %.0f Hz: E=%.5f | ratio=%.1fx  %s\n",
               noteHz[i], noteE[i], noiseHz[i], noiseE[i], ratio, ratio > 10 ? "OK" : "WEAK");
        if (ratio <= 10) pass = false;
    }

    // Release tail must decay towards silence.
    double tailRms = 0; std::size_t tn = 0;
    for (std::size_t i = std::size_t(2.8 * SR); i < mono.size(); ++i) { tailRms += mono[i]*mono[i]; ++tn; }
    tailRms = std::sqrt(tailRms / std::max<std::size_t>(tn,1));
    printf("  release tail RMS @2.8-3.0s = %.6f  %s\n", tailRms, tailRms < 0.02 ? "OK (decayed)" : "STILL RINGING");
    if (tailRms >= 0.05) pass = false;

    printf("\n%s — wrote caecilia-first-sound.wav\n", pass ? "*** FIRST SOUND OK ***" : "!!! CHECK FAILED !!!");
    return pass ? 0 : 1;
}
