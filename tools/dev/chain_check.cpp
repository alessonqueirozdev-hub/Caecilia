/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; see LICENSE.
 */

// Full master-chain gain-staging + distortion probe. Renders chords of varying
// density through engine + reverb + MasterEq + polyphony trim + Limiter (the real
// plugin chain) and reports mastered dBFS levels and a bass-chord distortion
// proxy, so loudness (sparse must be loud) and cleanliness (dense/bass must not
// distort) can be tuned without the plugin.

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/MasterEq.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/tuning/TuningModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

namespace core  = caecilia::core;
namespace eng   = caecilia::core::engine;
namespace model = caecilia::model;
namespace dsp   = caecilia::dsp;
namespace synth = caecilia::synth;
namespace tune  = caecilia::tuning;

namespace {
constexpr double kSR = 48000.0; constexpr std::size_t kBlock = 512;
constexpr float  kVoiceBase = 0.72f; // <-- per-voice base (AdditiveVoice uses 0.25; test the new value)
constexpr float  kPolyK     = 0.10f; // polyphony-compensation strength

model::RegistrationRank rk(core::TonalFamily f, double ft, bool c=false){ return {f, model::footageFromFeet(ft), c}; }
double toDb(double x){ return x>1e-9 ? 20*std::log10(x) : -120; }

// Render `notes` (held together) through the full chain; return {peakDb, rmsDb, thdProxy}.
struct Res { double peakDb, rmsDb, thd, grDb; };
Res run(const std::vector<model::RegistrationRank>& ranks, const std::vector<int>& notes,
        core::TonalFamily fam, double baseHz)
{
    const synth::SpectralModel comp =
        model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(ranks));
    tune::TuningModel tuning;
    dsp::FdnReverb rev; rev.prepare(kSR,kBlock,2); rev.setPreset(dsp::ReverbPreset::Cathedral);
    dsp::MasterEq eq; eq.prepare(kSR,kBlock,2);
    dsp::Limiter lim; lim.prepare(kSR,kBlock,2); lim.setParams(-1.5f,2.5f,90.0f);

    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices; std::vector<core::IVoice*> ptrs;
    synth::VoiceContext ctx; ctx.tuning=&tuning; ctx.family=fam; ctx.footage=core::footage::kEight;
    const std::size_t pool = std::max<std::size_t>(notes.size()*2, 8);
    for (std::size_t i=0;i<pool;++i){ auto v=std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(std::max<std::size_t>(comp.partials.size(),16));
        v->prepare(kSR,kBlock); v->setContext(ctx); v->seedFrom(comp);
        ptrs.push_back(v.get()); voices.push_back(std::move(v)); }
    eng::AudioEngine engine; engine.prepare(kSR,kBlock,2,3); engine.setTuning(&tuning);
    if (std::getenv("CAE_DRY")==nullptr) engine.setMasterReverb(&rev);
    engine.bindVoices(ptrs.data(), ptrs.size());

    const core::DivisionId dv{0};
    int k=0; for (int nt : notes){ const core::PipeId p{(std::uint16_t)k++,(std::uint8_t)nt};
        (void)engine.commandQueue().push(eng::EngineCommand::makeNoteOn(p,110,dv)); }

    std::array<float,kBlock> L{},R{}; float* ch[2]={L.data(),R.data()};
    float polyGain=1.0f;
    std::vector<float> mono; mono.reserve((std::size_t)(6.0*kSR));
    double peak=0,sumsq=0,grMax=0; long n=0;
    const int totalBlocks=(int)(6.0*kSR/kBlock);
    for (int b=0;b<totalBlocks;++b){
        L.fill(0);R.fill(0); core::AudioBlock blk(ch,2,kBlock); engine.processBlock(blk);
        eq.process(blk);
        // polyphony trim (smoothed), mirrors the processor
        const std::size_t nv = engine.activeVoiceCount();
        const float polyTarget = 1.0f/(1.0f+kPolyK*(nv>0?(float)(nv-1):0.0f));
        // AdditiveVoice now applies the ship base (0.85) itself; probe others via ratio.
        const float baseScale = kVoiceBase/0.72f;
        for (std::size_t i=0;i<kBlock;++i){ polyGain += 0.02f*(polyTarget-polyGain);
            L[i]*=baseScale*polyGain; R[i]*=baseScale*polyGain; }
        lim.process(blk);
        for (std::size_t i=0;i<kBlock;++i){ float v=std::max(-1.0f,std::min(1.0f,L[i]));
            const double t=(double)b*kBlock/kSR + (double)i/kSR;
            // Measure LATE (3.5..5.0 s) so the long cathedral reverb tail has built up.
            if (t>=3.5 && t<=5.0){ peak=std::max(peak,(double)std::fabs(v)); sumsq+=v*v; ++n; mono.push_back(v);
                grMax=std::max(grMax,(double)lim.gainReductionDb()); } }
    }
    // THD proxy: for the lowest note, energy outside the first ~12 harmonics vs total (Goertzel).
    double thd=0;
    if (!notes.empty()){
        const double f0=baseHz; double harm=0, tot=0;
        auto goertzel=[&](double f){ double w=2*3.14159265*f/kSR,c=2*std::cos(w),s1=0,s2=0;
            for(float x:mono){ double s0=x+c*s1-s2; s2=s1; s1=s0;} return s1*s1+s2*s2-c*s1*s2; };
        for (int h=1;h<=12;++h) harm+=goertzel(f0*h);
        for (int h=1;h<=24;++h) tot+=goertzel(f0*(h+0.5)); // inter-harmonic bins (distortion/IMD proxy)
        thd = tot>0 ? tot/(harm+1e-9) : 0;
    }
    const double rms = n>0? std::sqrt(sumsq/n):0;
    return { toDb(peak), toDb(rms), thd, grMax };
}
void report(const char* name, Res r){
    std::printf("  %-30s peak %6.1f dB   rms %6.1f dB   IMD %.3f   limiterGR %.1f dB\n",
                name, r.peakDb, r.rmsDb, r.thd, r.grDb);
}
}

int main(){
    std::printf("=== Master-chain gain staging (voiceBase=%.2f) ===\n", kVoiceBase);
    // note freqs for THD: C2=65.4, C3=130.8
    report("Bourdon 8' single (C3)", run({rk(core::TonalFamily::Flute,8)}, {48}, core::TonalFamily::Flute, 130.8));
    report("Plenum 4-note chord",    run({rk(core::TonalFamily::Principal,8),rk(core::TonalFamily::Principal,4),rk(core::TonalFamily::Flute,8),rk(core::TonalFamily::Mixture,2,true)}, {48,52,55,60}, core::TonalFamily::Principal, 130.8));
    report("Reeds low 4-note (bass)",run({rk(core::TonalFamily::Reed,16),rk(core::TonalFamily::Reed,8)}, {36,40,43,48}, core::TonalFamily::Reed, 65.4));
    std::vector<model::RegistrationRank> tutti;
    for (double f:{16.0,8.0,4.0,2.0}) tutti.push_back(rk(core::TonalFamily::Principal,f));
    for (double f:{16.0,8.0,4.0}) tutti.push_back(rk(core::TonalFamily::Flute,f));
    tutti.push_back(rk(core::TonalFamily::String,8));
    for (double f:{16.0,8.0,4.0}) tutti.push_back(rk(core::TonalFamily::Reed,f));
    tutti.push_back(rk(core::TonalFamily::Mixture,2,true));
    report("TUTTI 10-note cluster",  run(tutti, {36,40,43,47,48,52,55,59,60,64}, core::TonalFamily::Principal, 65.4));
    return 0;
}
