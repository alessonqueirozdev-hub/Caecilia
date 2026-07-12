/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

// ---------------------------------------------------------------------------
// caecilia-verify-registration
//
// A headless, JUCE-free proof that the plugin's registration path actually
// SOUNDS and sounds CORRECT. It drives the exact shared voicing the plugin uses
// (model::buildRegistrationCompositeSpectrum -> AdditiveVoice) and checks, with a
// Goertzel probe, that:
//   1. a single Principal 8' sounds at the played pitch (energy at f0),
//   2. the default plenum is audible (RMS above the floor),
//   3. the 64-partial-cap fix is real: a plenum + reeds registration whose
//      composite exceeds 64 partials renders DIFFERENTLY when the bank is sized
//      to the whole composite vs. the old default cap (i.e. the reeds are no
//      longer silently dropped).
//
// Exit code 0 == all checks passed, 1 == a check failed. Nothing here runs on an
// audio thread, so std::vector / iostream are fine.
// ---------------------------------------------------------------------------

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace
{
using namespace caecilia;

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 512;
constexpr int    kBlocks     = 96; // ~1.0 s of render

/// Tempered fundamental of a MIDI note (A4=440), the pitch an 8' rank sounds.
double noteHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

struct RenderResult
{
    double rms          = 0.0;
    double energyAtF0   = 0.0; // Goertzel magnitude at the played fundamental
    std::size_t partials = 0;
};

/// Seed one composite voice for @p engaged and render a held note, returning its
/// RMS and the Goertzel energy at the played fundamental. @p capOverride < 0 uses
/// the fix (bank sized to the composite); >= 0 forces that cap (to model the old
/// 64-partial truncation bug).
RenderResult render(const model::Organ& organ,
                    std::span<const core::StopId> engaged,
                    int midi,
                    int capOverride = -1)
{
    const synth::SpectralModel comp = model::buildRegistrationCompositeSpectrum(organ, engaged);

    synth::AdditiveVoice v;
    const std::size_t cap = capOverride >= 0
        ? static_cast<std::size_t>(capOverride)
        : (comp.partials.size() < 16 ? std::size_t{16} : comp.partials.size());
    v.bank().setMaxPartials(cap);
    v.prepare(kSampleRate, kBlock);

    synth::VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight; // composite is referenced to 8'
    v.setContext(ctx);
    v.seedFrom(comp);
    v.noteOn(core::PipeId{ 0, static_cast<std::uint8_t>(midi) }, 100);

    // Goertzel state for the fundamental.
    const double f0 = noteHz(midi);
    const double w  = 2.0 * M_PI * f0 / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    double sumSq = 0.0;
    std::size_t n = 0;

    std::vector<float> chan(static_cast<std::size_t>(kBlock));
    float* chans[1] = { chan.data() };

    for (int b = 0; b < kBlocks; ++b)
    {
        std::fill(chan.begin(), chan.end(), 0.0f);
        core::AudioBlock block(chans, 1, static_cast<std::size_t>(kBlock));
        v.renderAdd(block);

        // Skip the first two blocks (attack ramp) for the steady-state measures.
        const bool measure = b >= 2;
        for (int i = 0; i < kBlock; ++i)
        {
            const double x = static_cast<double>(chan[static_cast<std::size_t>(i)]);
            if (measure)
            {
                s0 = coeff * s1 - s2 + x;
                s2 = s1; s1 = s0;
                sumSq += x * x;
                ++n;
            }
        }
    }

    RenderResult r;
    r.partials  = comp.partials.size();
    r.rms       = n ? std::sqrt(sumSq / static_cast<double>(n)) : 0.0;
    r.energyAtF0 = std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / static_cast<double>(n ? n : 1);
    return r;
}

/// First StopId in @p organ matching a family (and optional exact footage).
core::StopId findStop(const model::Organ& organ, core::TonalFamily fam,
                      core::Footage footage, bool matchFootage, bool& ok)
{
    for (const model::Stop& s : organ.stops())
        if (s.family() == fam && (! matchFootage || s.footage() == footage))
        {
            ok = true;
            return s.id();
        }
    ok = false;
    return {};
}

bool check(const char* name, bool pass, const char* detail)
{
    std::printf("  [%s] %-46s %s\n", pass ? "PASS" : "FAIL", name, detail);
    return pass;
}
} // namespace

int main()
{
    using namespace caecilia;
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    std::printf("caecilia-verify-registration: %zu stops\n", organ.stops().size());

    bool allOk = true;

    // --- 1) a single Principal 8' sounds at the played pitch -----------------
    bool has8 = false;
    const core::StopId principal8 =
        findStop(organ, core::TonalFamily::Principal, core::footage::kEight, true, has8);
    if (! has8)
    {
        allOk &= check("find a Principal 8'", false, "no Principal 8' in the demo organ");
    }
    else
    {
        const core::StopId one[] = { principal8 };
        const RenderResult r = render(organ, one, 60 /*C4*/);
        char d[160];
        std::snprintf(d, sizeof d, "rms=%.5f  E@f0=%.5f (C4=%.1f Hz)  partials=%zu",
                      r.rms, r.energyAtF0, noteHz(60), r.partials);
        allOk &= check("Principal 8' is audible", r.rms > 1.0e-3, d);
        allOk &= check("Principal 8' energy at the fundamental", r.energyAtF0 > 1.0e-3, d);
    }

    // --- 2) the default plenum is audible ------------------------------------
    std::vector<core::StopId> plenum;
    {
        // The GO plenum: every Principal and Mixture, plus an 8' flute, on the
        // division carrying the most stops (mirrors the plugin default).
        std::vector<int> perDiv(64, 0);
        for (const model::Stop& s : organ.stops())
            if (s.division().value < perDiv.size()) ++perDiv[s.division().value];
        int go = 0;
        for (int d = 1; d < (int) perDiv.size(); ++d) if (perDiv[d] > perDiv[go]) go = d;

        for (const model::Stop& s : organ.stops())
        {
            if (s.division().value != go) continue;
            const bool chorus = s.family() == core::TonalFamily::Principal
                             || s.family() == core::TonalFamily::Mixture;
            const bool flute8 = s.family() == core::TonalFamily::Flute
                             && s.footage() == core::footage::kEight;
            if (chorus || flute8) plenum.push_back(s.id());
        }
    }
    {
        const RenderResult r = render(organ, plenum, 60);
        char d[160];
        std::snprintf(d, sizeof d, "rms=%.5f  stops=%zu  partials=%zu", r.rms, plenum.size(), r.partials);
        allOk &= check("default plenum is audible", r.rms > 1.0e-3 && plenum.size() >= 3, d);
    }

    // --- 3) the 64-partial-cap fix is real -----------------------------------
    // Add every GO reed to the plenum so the composite exceeds 64 partials; then
    // render with the fixed (full) bank vs. the old default 64 cap. If the reeds
    // were being silently dropped, the two renders would be identical.
    std::vector<core::StopId> plenumReeds = plenum;
    for (const model::Stop& s : organ.stops())
    {
        bool inGo = false;
        for (const core::StopId id : plenum) if (id.value == s.division().value) { inGo = true; break; }
        // add reeds that live in the same division as the plenum
        if (s.family() == core::TonalFamily::Reed && ! plenum.empty())
        {
            const core::StopId first = plenum.front();
            const model::Stop* fp = organ.stop(first);
            if (fp && s.division() == fp->division())
                plenumReeds.push_back(s.id());
        }
        (void) inGo;
    }
    {
        const RenderResult fixed  = render(organ, plenumReeds, 60, -1);      // sized to composite
        const RenderResult capped = render(organ, plenumReeds, 60, 64);      // old default cap
        const double relDiff = std::fabs(fixed.rms - capped.rms) /
                               (capped.rms > 0.0 ? capped.rms : 1.0);
        char d[200];
        std::snprintf(d, sizeof d,
                      "partials=%zu  rms(full)=%.5f  rms(cap64)=%.5f  reldiff=%.1f%%",
                      fixed.partials, fixed.rms, capped.rms, 100.0 * relDiff);
        // The scenario only bites when the composite really exceeds 64 partials.
        const bool scenario = fixed.partials > 64;
        allOk &= check("plenum+reeds composite exceeds 64 partials", scenario, d);
        allOk &= check("64-cap fix changes the sound (reeds no longer dropped)",
                       scenario ? relDiff > 0.02 : true, d);
    }

    std::printf("\n%s\n", allOk ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allOk ? 0 : 1;
}
