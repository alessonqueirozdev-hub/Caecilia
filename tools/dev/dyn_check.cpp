/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// Dynamic-range check for the console composite path (buildCompositeFromRegistration).
// Renders a sustained note for several registrations and prints the sustain RMS,
// proving soft registrations (psalmody) are genuinely quieter than the tutti.

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/VoiceContext.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
constexpr double kSR = 48000.0;
constexpr std::size_t kBlock = 512;

model::RegistrationRank rank(core::TonalFamily fam, double feet, bool compound = false)
{
    return model::RegistrationRank{ fam, model::footageFromFeet(feet), compound };
}

double renderRms(const std::vector<model::RegistrationRank>& ranks)
{
    const synth::SpectralModel comp =
        model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(ranks));

    synth::AdditiveVoice v;
    v.bank().setMaxPartials(std::max<std::size_t>(comp.partials.size(), 16));
    v.prepare(kSR, kBlock);
    synth::VoiceContext ctx;
    ctx.family = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight;
    v.setContext(ctx);
    v.seedFrom(comp);
    v.noteOn(core::PipeId{ 0, 60 }, 100); // middle C

    std::array<float, kBlock> l{}, r{};
    float* ch[2] = { l.data(), r.data() };
    // Render ~1.2 s; measure RMS over 0.4..1.1 s (past the attack bloom).
    const int totalBlocks = static_cast<int>(1.2 * kSR / kBlock);
    double sumSq = 0.0; long count = 0;
    double t = 0.0;
    for (int b = 0; b < totalBlocks; ++b)
    {
        l.fill(0.0f); r.fill(0.0f);
        core::AudioBlock block(ch, 2, kBlock);
        v.renderAdd(block);
        for (std::size_t i = 0; i < kBlock; ++i)
        {
            const double tt = t + static_cast<double>(i) / kSR;
            if (tt >= 0.4 && tt <= 1.1) { sumSq += l[i] * l[i]; ++count; }
        }
        t += static_cast<double>(kBlock) / kSR;
    }
    return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
}

void report(const std::string& name, const std::vector<model::RegistrationRank>& ranks)
{
    const double rms = renderRms(ranks);
    const double db = rms > 1e-9 ? 20.0 * std::log10(rms) : -120.0;
    std::printf("  %-28s  RMS %.4f  (%6.1f dBFS)  [%zu ranks]\n",
                name.c_str(), rms, db, ranks.size());
}
} // namespace

int main()
{
    std::printf("=== Console registration dynamic range ===\n");

    report("Salmodia (Bourdon 8')",       { rank(core::TonalFamily::Flute, 8) });
    report("Fonds doux (Bourdon+cordas)", { rank(core::TonalFamily::Flute, 8),
                                            rank(core::TonalFamily::String, 8),
                                            rank(core::TonalFamily::String, 8) });
    report("Principal 8' solo",           { rank(core::TonalFamily::Principal, 8) });
    report("Petit plenum (8+4+2)",        { rank(core::TonalFamily::Principal, 8),
                                            rank(core::TonalFamily::Principal, 4),
                                            rank(core::TonalFamily::Principal, 2) });
    report("Grand plenum",                { rank(core::TonalFamily::Principal, 16),
                                            rank(core::TonalFamily::Principal, 8),
                                            rank(core::TonalFamily::Principal, 4),
                                            rank(core::TonalFamily::Principal, 2),
                                            rank(core::TonalFamily::Flute, 8),
                                            rank(core::TonalFamily::Mixture, 2, true) });
    std::vector<model::RegistrationRank> tutti;
    for (double ft : { 16.0, 8.0, 4.0, 2.0 }) tutti.push_back(rank(core::TonalFamily::Principal, ft));
    for (double ft : { 16.0, 8.0, 4.0 })      tutti.push_back(rank(core::TonalFamily::Flute, ft));
    tutti.push_back(rank(core::TonalFamily::String, 8));
    tutti.push_back(rank(core::TonalFamily::Reed, 16));
    tutti.push_back(rank(core::TonalFamily::Reed, 8));
    tutti.push_back(rank(core::TonalFamily::Reed, 4));
    tutti.push_back(rank(core::TonalFamily::Mixture, 2, true));
    report("Tutti", tutti);

    return 0;
}
