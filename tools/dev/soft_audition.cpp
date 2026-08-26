// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

// Soft / liturgical audition: renders a slow, legato, chant-accompaniment phrase
// through the CONSOLE composite path (buildCompositeFromRegistration) for several
// dark, gentle registrations, in the cathedral acoustic. One WAV, three
// registrations in sequence, so the ear can hear the monastic character.

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/tuning/TuningModel.h"
#include "caecilia/wind/WindModel.h"
#include "caecilia/wind/WindTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace core  = caecilia::core;
namespace eng   = caecilia::core::engine;
namespace model = caecilia::model;
namespace dsp   = caecilia::dsp;
namespace wind  = caecilia::wind;
namespace tune  = caecilia::tuning;
namespace synth = caecilia::synth;

namespace
{
constexpr double      kSR    = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kPool  = 24;
constexpr std::size_t kVoicesPerNote = 2;

model::RegistrationRank rk(core::TonalFamily fam, double feet, bool comp = false)
{ return model::RegistrationRank{ fam, model::footageFromFeet(feet), comp }; }

// A slow, soft modal phrase (D dorian) — the sort of thing that accompanies
// plainchant: legato, low-ish, unhurried. {pedal, 3 upper voices}, on/off in sec.
struct Chord { int n[4]; double on, off; };
const std::array<Chord, 5> kPhrase = {{
    { { 38, 50, 53, 57 }, 0.20, 1.90 },  // Dm
    { { 36, 48, 52, 55 }, 1.95, 3.55 },  // C
    { { 34, 46, 50, 53 }, 3.60, 5.20 },  // Bb
    { { 36, 48, 55, 60 }, 5.25, 6.85 },  // C (open)
    { { 38, 50, 53, 57 }, 6.90, 9.20 },  // Dm (final, held)
}};
constexpr double kSegSec = 12.5; // phrase + reverb tail

bool writeWav16(const std::string& p, const std::vector<float>& l, const std::vector<float>& r)
{
    std::ofstream f(p, std::ios::binary); if (!f) return false;
    const std::uint32_t sr=(std::uint32_t)kSR; const std::uint16_t ch=2,bits=16;
    const std::uint32_t fr=(std::uint32_t)l.size(); const std::uint16_t ba=ch*(bits/8);
    const std::uint32_t db=fr*ba;
    auto p32=[&](std::uint32_t v){f.put(char(v&0xFF)).put(char((v>>8)&0xFF)).put(char((v>>16)&0xFF)).put(char((v>>24)&0xFF));};
    auto p16=[&](std::uint16_t v){f.put(char(v&0xFF)).put(char((v>>8)&0xFF));};
    f.write("RIFF",4);p32(36+db);f.write("WAVE",4);f.write("fmt ",4);p32(16);p16(1);p16(ch);
    p32(sr);p32(sr*ba);p16(ba);p16(bits);f.write("data",4);p32(db);
    for(std::uint32_t i=0;i<fr;++i){auto q=[](float s)->std::int16_t{float c=std::clamp(s,-1.0f,1.0f);return (std::int16_t)std::lround(c*32767.0f);};
        p16((std::uint16_t)q(l[i]));p16((std::uint16_t)q(r[i]));}
    return (bool)f;
}
std::size_t f2s(double s){ return (std::size_t)(s*kSR+0.5); }

struct Ev { std::size_t fr; eng::EngineCommand cmd; };

// Render one registration's phrase into freshly-allocated stereo buffers.
void renderSegment(const std::vector<model::RegistrationRank>& ranks,
                   core::TonalFamily family,
                   tune::TuningModel& tuning, wind::WindModel& windModel,
                   std::vector<float>& outL, std::vector<float>& outR)
{
    const synth::SpectralModel comp =
        model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(ranks));
    const std::size_t maxP = std::max<std::size_t>(comp.partials.size(), 16);

    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*> ptrs;
    synth::VoiceContext ctx; ctx.tuning=&tuning; ctx.wind=&windModel;
    ctx.family=family; ctx.footage=core::footage::kEight;
    ctx.chest=core::WindchestId{2}; // Récit chest (soft)
    for (std::size_t i=0;i<kPool;++i){
        auto v=std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(maxP);
        v->prepare(kSR,kBlock); v->setContext(ctx); v->seedFrom(comp);
        ptrs.push_back(v.get()); voices.push_back(std::move(v));
    }

    dsp::FdnReverb reverb; reverb.prepare(kSR,kBlock,2); reverb.setPreset(dsp::ReverbPreset::Cathedral);
    eng::AudioEngine engine; engine.prepare(kSR,kBlock,2,3);
    engine.setTuning(&tuning); engine.setWindSupply(&windModel); engine.setMasterReverb(&reverb);
    engine.bindVoices(ptrs.data(), ptrs.size());

    std::vector<Ev> ev; const core::DivisionId dv{0};
    for (const Chord& c : kPhrase){
        const std::size_t on=f2s(c.on), off=f2s(c.off);
        for (int nt : c.n) for (std::size_t k=0;k<kVoicesPerNote;++k){
            const core::PipeId pipe{(std::uint16_t)k,(std::uint8_t)nt};
            ev.push_back({on, eng::EngineCommand::makeNoteOn(pipe,55,dv)});   // soft velocity
            ev.push_back({off,eng::EngineCommand::makeNoteOff(pipe,dv)});
        }
    }
    std::sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&b){return a.fr<b.fr;});

    const std::size_t total=f2s(kSegSec);
    outL.assign(total,0.0f); outR.assign(total,0.0f);
    std::array<float,kBlock> sL{},sR{}; float* ch[2]={sL.data(),sR.data()};
    std::size_t pos=0,ne=0;
    while(pos<total){
        const std::size_t n=std::min(kBlock,total-pos);
        while(ne<ev.size()&&ev[ne].fr<pos+n){(void)engine.commandQueue().push(ev[ne].cmd);++ne;}
        core::AudioBlock b(ch,2,n); engine.processBlock(b);
        for(std::size_t i=0;i<n;++i){outL[pos+i]=sL[i];outR[pos+i]=sR[i];}
        pos+=n;
    }
}
} // namespace

int main()
{
    tune::TuningModel tuning;
    wind::WindModel windModel; windModel.prepare(kSR,kBlock);
    {
        wind::WindModelConfig cfg; cfg.bellows.resize(3); cfg.chests.resize(3);
        const float pa[3]={980.0f,812.0f,735.0f};
        for(std::size_t i=0;i<3;++i){cfg.bellows[i].nominalPressurePa=pa[i];
            cfg.chests[i].id=core::WindchestId{(std::uint16_t)i};cfg.chests[i].bellowsIndex=(std::uint16_t)i;
            cfg.chests[i].tremulantIndex=-1;cfg.chests[i].nominalPressurePa=pa[i];}
        windModel.configure(cfg); windModel.reset();
    }

    struct Seg { std::string name; std::vector<model::RegistrationRank> ranks; core::TonalFamily fam; };
    std::vector<Seg> segs = {
        { "Salmodia (Bourdon 8')",        { rk(core::TonalFamily::Flute,8) }, core::TonalFamily::Flute },
        { "Acompanhamento (fonds doux)",  { rk(core::TonalFamily::Flute,8), rk(core::TonalFamily::String,8),
                                            rk(core::TonalFamily::String,8) }, core::TonalFamily::Flute },
        { "Voix Celeste (cordas)",        { rk(core::TonalFamily::String,8), rk(core::TonalFamily::String,8) },
                                          core::TonalFamily::String },
        { "Monastico (flautas 8+4)",      { rk(core::TonalFamily::Flute,8), rk(core::TonalFamily::Flute,4) },
                                          core::TonalFamily::Flute },
    };

    std::vector<float> L, R;
    const std::size_t gap = f2s(1.2);
    for (const Seg& s : segs){
        std::printf("Rendering: %s ...\n", s.name.c_str());
        std::vector<float> l,r; renderSegment(s.ranks, s.fam, tuning, windModel, l, r);
        L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end());
        L.insert(L.end(), gap, 0.0f); R.insert(R.end(), gap, 0.0f);
    }

    // One gentle common normalisation (preserve relative softness between segments).
    float peak=0.0f; for(std::size_t i=0;i<L.size();++i){peak=std::max(peak,std::fabs(L[i]));peak=std::max(peak,std::fabs(R[i]));}
    if(peak>1e-9f){const float g=0.79f/peak; for(std::size_t i=0;i<L.size();++i){L[i]*=g;R[i]*=g;}}
    std::printf("Total %.1fs, raw peak %.3f\n", (double)L.size()/kSR, peak);

    writeWav16("tools/dev/caecilia-suave.wav", L, R);
    std::printf("Wrote tools/dev/caecilia-suave.wav\n");
    return 0;
}
