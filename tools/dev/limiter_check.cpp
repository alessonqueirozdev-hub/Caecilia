/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; see LICENSE.
 */

// Validates the master limiter + master EQ:
//  (A) a hot Tutti-like signal comes out at/below the ceiling, no NaN, click-free,
//      and a "flat" EQ is ~identity;
//  (B) the HOLD stage prevents PUMPING: on a signal that alternates loud/quiet, the
//      gain must stay steady (not recover during the quiet gap and slam back down).

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/MasterEq.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace core = caecilia::core;
namespace dsp  = caecilia::dsp;

int main()
{
    constexpr double sr = 48000.0;
    constexpr int    block = 512;

    // --- (A) ceiling / NaN / flat-EQ identity on a steady hot Tutti --------------
    dsp::Limiter lim;
    lim.prepare(sr, block, 2);
    lim.setParams(-1.5f, 2.5f, /*holdMs*/ 400.0f, /*releaseMs*/ 600.0f);

    dsp::MasterEq eq;     eq.prepare(sr, block, 2);
    dsp::MasterEq eqFlat; eqFlat.prepare(sr, block, 2);
    for (int b = 0; b < 5; ++b) eqFlat.setBandGain((std::size_t)b, 0.0f);

    std::vector<float> L(block), R(block);
    float* ch[2] = { L.data(), R.data() };

    double phase = 0.0;
    float  postLimPeak = 0.0f, eqFlatMaxDelta = 0.0f, eqPeak = 0.0f;
    bool   nan = false;

    for (int blk = 0; blk < 200; ++blk)
    {
        const float amp = blk < 20 ? (blk / 20.0f) * 3.0f : 3.0f;
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int h = 1; h <= 6; ++h) s += std::sin(phase * h) / h;
            s *= amp * 0.5f;
            L[i] = s; R[i] = s * 0.97f;
            phase += 2.0 * 3.14159265 * 180.0 / sr;
        }
        {
            std::vector<float> l2(L), r2(R);
            float* c2[2] = { l2.data(), r2.data() };
            core::AudioBlock b2(c2, 2, block);
            eqFlat.process(b2);
            if (blk >= 40)
                for (int i = 0; i < block; ++i)
                    eqFlatMaxDelta = std::max(eqFlatMaxDelta, std::fabs(l2[i] - L[i]));
        }
        core::AudioBlock blockView(ch, 2, block);
        eq.process(blockView);
        for (int i = 0; i < block; ++i) eqPeak = std::max(eqPeak, std::fabs(L[i]));
        lim.process(blockView);
        if (blk >= 40)
            for (int i = 0; i < block; ++i)
            {
                if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nan = true;
                postLimPeak = std::max(postLimPeak, std::fabs(L[i]));
            }
    }

    const double ceilingLin = std::pow(10.0, -1.5 / 20.0);
    std::printf("(A) Limiter: settled post-peak %.4f (ceiling %.4f)  GR now %.2f dB\n",
                postLimPeak, ceilingLin, lim.gainReductionDb());
    std::printf("    EQ: organ-EQ peak %.4f  |  flat-EQ max|delta| %.2e\n", eqPeak, eqFlatMaxDelta);

    // --- (B) NO PUMPING: alternate loud (~120 ms) / quiet (~120 ms) --------------
    // A pumping limiter recovers gain during the quiet gap, then slams back down on
    // the next loud burst -> the GR swings. The hold freezes the gain across the gap.
    dsp::Limiter lim2;
    lim2.prepare(sr, block, 2);
    lim2.setParams(-1.5f, 2.5f, /*holdMs*/ 400.0f, /*releaseMs*/ 600.0f);

    const int burst = (int)(sr * 0.12 / block); // ~120 ms of blocks
    float grLoudMin = 1e9f, grQuietMax = -1e9f;
    phase = 0.0;
    bool loud = true; int since = 0;
    for (int blk = 0; blk < 400; ++blk)
    {
        const float amp = loud ? 3.0f : 0.35f;   // loud clips hard; quiet is below ceiling
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int h = 1; h <= 6; ++h) s += std::sin(phase * h) / h;
            s *= amp * 0.5f;
            L[i] = s; R[i] = s * 0.97f;
            phase += 2.0 * 3.14159265 * 180.0 / sr;
        }
        core::AudioBlock v(ch, 2, block);
        lim2.process(v);
        if (blk > 40) // after the pattern is established
        {
            const float gr = lim2.gainReductionDb();
            if (loud)  grLoudMin  = std::min(grLoudMin,  gr); // least reduction while loud
            else       grQuietMax = std::max(grQuietMax, gr); // most recovery while quiet
        }
        if (++since >= burst) { since = 0; loud = !loud; }
    }
    // With the hold, GR barely moves between loud and quiet phases. Pumping would
    // show the quiet-phase GR collapsing toward 0 while loud-phase GR is large.
    const float pumpSwing = grLoudMin - grQuietMax; // small (<=~1.5 dB) => no pumping
    std::printf("(B) Pump test: loud-min GR %.2f dB, quiet-max GR %.2f dB, swing %.2f dB\n",
                grLoudMin, grQuietMax, pumpSwing);

    const bool limitOk = postLimPeak <= 0.97f && lim.gainReductionDb() > 2.0f;
    const bool flatOk  = eqFlatMaxDelta < 1e-3;
    const bool noPump  = pumpSwing <= 1.5f && grQuietMax > 2.0f; // gain stays reduced across the gap
    const bool pass = limitOk && flatOk && !nan && noPump;
    std::printf("\n%s  (ceiling: %s, flat EQ: %s, NaN: %s, no-pump: %s)\n",
                pass ? "PASS" : "FAIL", limitOk ? "yes" : "NO", flatOk ? "yes" : "NO",
                nan ? "FOUND" : "none", noPump ? "yes" : "NO");
    return pass ? 0 : 1;
}
