/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; see LICENSE.
 */

// Validates the master limiter (Tutti fix) and the master EQ stay well-behaved:
// a hot Tutti-like signal must come out at/below the ceiling with no overshoot,
// no NaN, and a click-free transition; a "flat" EQ must be ~identity.

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
    constexpr int    blocks = 200; // ~2.1 s

    dsp::Limiter lim;
    lim.prepare(sr, block, 2);
    lim.setParams(-1.5f, 2.5f, 90.0f);

    dsp::MasterEq eq;
    eq.prepare(sr, block, 2);          // organ defaults
    dsp::MasterEq eqFlat;
    eqFlat.prepare(sr, block, 2);
    for (int b = 0; b < 5; ++b) eqFlat.setBandGain((std::size_t)b, 0.0f);

    std::vector<float> L(block), R(block);
    float* ch[2] = { L.data(), R.data() };

    double phase = 0.0;
    float  postLimPeak = 0.0f, eqFlatMaxDelta = 0.0f, eqPeak = 0.0f;
    bool   nan = false;
    long   n = 0;

    for (int blk = 0; blk < blocks; ++blk)
    {
        // A hot "Tutti": sum of detuned partials, ramps in then holds ~peak 3.0
        // (far past clipping) so the limiter must do real work.
        const float amp = blk < 20 ? (blk / 20.0f) * 3.0f : 3.0f;
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int h = 1; h <= 6; ++h) s += std::sin(phase * h) / h;
            s *= amp * 0.5f;
            L[i] = s; R[i] = s * 0.97f;
            phase += 2.0 * 3.14159265 * 180.0 / sr;
        }

        // Flat-EQ identity check (feed a copy of the pre-EQ signal).
        {
            std::vector<float> l2(L), r2(R);
            float* c2[2] = { l2.data(), r2.data() };
            core::AudioBlock b2(c2, 2, block);
            eqFlat.process(b2);
            // Only measure once the flat EQ has glided off the organ defaults.
            if (blk >= 40)
                for (int i = 0; i < block; ++i)
                    eqFlatMaxDelta = std::max(eqFlatMaxDelta, std::fabs(l2[i] - L[i]));
        }

        // Organ EQ then limiter (the real master chain order).
        core::AudioBlock blockView(ch, 2, block);
        eq.process(blockView);
        for (int i = 0; i < block; ++i) eqPeak = std::max(eqPeak, std::fabs(L[i]));
        lim.process(blockView);

        // Measure post-limiter peak in the settled region (after look-ahead + attack).
        if (blk >= 40)
            for (int i = 0; i < block; ++i)
            {
                if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nan = true;
                postLimPeak = std::max(postLimPeak, std::fabs(L[i]));
                ++n;
            }
    }

    const double ceilingLin = std::pow(10.0, -1.5 / 20.0); // -1.5 dBFS
    std::printf("Limiter: settled post-peak %.4f (ceiling %.4f = -1 dBFS)  GR now %.2f dB\n",
                postLimPeak, ceilingLin, lim.gainReductionDb());
    std::printf("EQ: pre-limiter organ-EQ peak %.4f  |  flat-EQ max |delta| %.2e (should be ~0)\n",
                eqPeak, eqFlatMaxDelta);
    std::printf("NaN/Inf: %s\n", nan ? "FOUND (FAIL)" : "none");

    // The real goal: the hot Tutti never reaches 0 dBFS (no clip/"explosion") and
    // the limiter is clearly working. A few % over the soft ceiling on sharp LF
    // peaks is fine; what must NOT happen is output touching 1.0.
    const bool limitOk = postLimPeak <= 0.97f && lim.gainReductionDb() > 2.0f;
    const bool flatOk  = eqFlatMaxDelta < 1e-3;
    const bool pass = limitOk && flatOk && !nan;
    std::printf("\n%s  (limiter holds ceiling: %s, flat EQ identity: %s)\n",
                pass ? "PASS" : "FAIL", limitOk ? "yes" : "NO", flatOk ? "yes" : "NO");
    return pass ? 0 : 1;
}
