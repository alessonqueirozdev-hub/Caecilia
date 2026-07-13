/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// ===========================================================================
// Caecilia "living pipe" audition.
//
// Renders ONE sustained C-major chord on a moderate registration so the ear
// can clearly hear the two new qualities the Aeolus study brought in:
//   1. a soft, blooming attack (upper partials speak after the fundamental),
//   2. a living, chorus-like shimmer from independent per-partial pitch drift.
//
// Not part of the CMake build (compiled by tools/dev/build_audition.sh).
// ===========================================================================

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
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

constexpr double      kSampleRate  = 48000.0;
constexpr std::size_t kBlock       = 512;
constexpr std::size_t kChannels    = 2;
constexpr double      kDurationSec = 9.0;
constexpr std::size_t kPoolCapacity = 48;
constexpr std::size_t kVoicesPerNote = 3;
constexpr core::Velocity kVelocity = 90;

bool writeWav16(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::uint32_t sr = static_cast<std::uint32_t>(kSampleRate);
    const std::uint16_t ch = 2, bits = 16;
    const std::uint32_t frames = static_cast<std::uint32_t>(l.size());
    const std::uint16_t blockAlign = ch * (bits / 8);
    const std::uint32_t dataBytes = frames * blockAlign;
    auto put32 = [&](std::uint32_t v){ f.put(char(v&0xFF)).put(char((v>>8)&0xFF)).put(char((v>>16)&0xFF)).put(char((v>>24)&0xFF)); };
    auto put16 = [&](std::uint16_t v){ f.put(char(v&0xFF)).put(char((v>>8)&0xFF)); };
    f.write("RIFF",4); put32(36+dataBytes); f.write("WAVE",4);
    f.write("fmt ",4); put32(16); put16(1); put16(ch); put32(sr); put32(sr*blockAlign); put16(blockAlign); put16(bits);
    f.write("data",4); put32(dataBytes);
    for (std::uint32_t i=0;i<frames;++i){
        auto q=[](float s)->std::int16_t{ float c=std::clamp(s,-1.0f,1.0f); return static_cast<std::int16_t>(std::lround(c*32767.0f)); };
        put16(static_cast<std::uint16_t>(q(l[i]))); put16(static_cast<std::uint16_t>(q(r[i])));
    }
    return static_cast<bool>(f);
}

std::size_t secToFrame(double s){ return static_cast<std::size_t>(s*kSampleRate+0.5); }

struct Event { std::size_t frame; eng::EngineCommand cmd; };
} // namespace

int main()
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Moderate GO registration: Montre 8 + Bourdon 8 + Prestant 4.
    // (Stop ids 7=Montre 8, 9=Bourdon 8, 10=Prestant 4 in the GO block.)
    std::vector<core::StopId> engaged;
    for (const model::Stop& s : organ.stops())
    {
        const std::string n = s.name();
        if (n == "Montre 8" || n == "Bourdon 8" || n == "Prestant 4")
            engaged.push_back(s.id());
    }
    std::printf("Audition registration: %zu stops.\n", engaged.size());

    tune::TuningModel tuning;

    wind::WindModel windModel;
    windModel.prepare(kSampleRate, kBlock);
    {
        wind::WindModelConfig cfg;
        const auto nChests = organ.windchests().size();
        cfg.bellows.resize(nChests);
        cfg.chests.resize(nChests);
        for (std::size_t i=0;i<nChests;++i){
            const float nomPa = organ.windchests()[i].nominalPressurePa;
            cfg.bellows[i].nominalPressurePa = nomPa;
            cfg.chests[i].id = core::WindchestId{ static_cast<std::uint16_t>(i) };
            cfg.chests[i].bellowsIndex = static_cast<std::uint16_t>(i);
            cfg.chests[i].tremulantIndex = -1;
            cfg.chests[i].nominalPressurePa = nomPa;
        }
        for (const model::Rank& rk : organ.ranks())
            cfg.pipeBindings.push_back(wind::PipeChestBinding{ rk.id(), rk.windchest() });
        windModel.configure(cfg);
        windModel.reset();
    }

    model::DemoVoiceBank bank = model::buildDemoRegistrationVoices(
        organ, engaged, &windModel, &tuning, kSampleRate, kBlock, kPoolCapacity);
    if (bank.empty()){ std::printf("FAIL: no voices.\n"); return 1; }

    dsp::FdnReverb reverb;
    reverb.prepare(kSampleRate, kBlock, kChannels);
    reverb.setPreset(dsp::ReverbPreset::Cathedral);

    eng::AudioEngine engine;
    engine.prepare(kSampleRate, kBlock, kChannels, organ.windchests().size());
    engine.setTuning(&tuning);
    engine.setWindSupply(&windModel);
    engine.setMasterReverb(&reverb);
    engine.bindVoices(bank.data(), bank.size());

    // One sustained C-major chord: C3 E3 G3 C4, on from 0.3s, off at 6.0s.
    const int chord[] = { 48, 52, 55, 60 };
    const core::DivisionId anyDiv{ 0 };
    std::vector<Event> events;
    const std::size_t onF = secToFrame(0.3), offF = secToFrame(6.0);
    for (int note : chord)
        for (std::size_t k=0;k<kVoicesPerNote;++k){
            const core::PipeId pipe{ static_cast<std::uint16_t>(k), static_cast<std::uint8_t>(note) };
            events.push_back({ onF, eng::EngineCommand::makeNoteOn(pipe, kVelocity, anyDiv) });
            events.push_back({ offF, eng::EngineCommand::makeNoteOff(pipe, anyDiv) });
        }
    std::sort(events.begin(), events.end(), [](const Event&a,const Event&b){ return a.frame<b.frame; });

    const std::size_t total = secToFrame(kDurationSec);
    std::vector<float> outL(total,0.0f), outR(total,0.0f);
    std::array<float,kBlock> sL{}, sR{};
    float* chans[kChannels] = { sL.data(), sR.data() };

    std::size_t pos=0, ne=0;
    while (pos<total){
        const std::size_t n = std::min(kBlock, total-pos);
        while (ne<events.size() && events[ne].frame < pos+n){ (void)engine.commandQueue().push(events[ne].cmd); ++ne; }
        core::AudioBlock block(chans, kChannels, n);
        engine.processBlock(block);
        for (std::size_t i=0;i<n;++i){ outL[pos+i]=sL[i]; outR[pos+i]=sR[i]; }
        pos+=n;
    }

    float peak=0.0f;
    for (std::size_t i=0;i<total;++i){ peak=std::max(peak,std::fabs(outL[i])); peak=std::max(peak,std::fabs(outR[i])); }
    if (peak>1e-9f){ const float g=0.70794578f/peak; for(std::size_t i=0;i<total;++i){ outL[i]*=g; outR[i]*=g; } }
    std::printf("Rendered %.1fs, raw peak %.3f.\n", kDurationSec, peak);

    writeWav16("tools/dev/caecilia-audition.wav", outL, outR);
    std::printf("Wrote tools/dev/caecilia-audition.wav\n");
    return 0;
}
