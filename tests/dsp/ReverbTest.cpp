/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

//
// FdnReverb tests: numerical STABILITY (a unitary feedback matrix plus
// sub-unity per-line gains must never let the tank blow up or produce
// non-finite samples) and DECAY (energy injected by an impulse fades toward
// silence over roughly the configured RT60). Also pins the mix=0 dry-passthrough
// and reset-clears-the-tail contracts.
//

#include "ceciliae/core/AudioBlock.h"
#include "ceciliae/dsp/FdnReverb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace core = ceciliae::core;
namespace dsp  = ceciliae::dsp;

namespace
{
/// Wrap two channel vectors as an AudioBlock over their whole length.
core::AudioBlock makeBlock(std::vector<float>& left,
                           std::vector<float>& right,
                           std::array<float*, 2>& chans)
{
    chans = {left.data(), right.data()};
    return core::AudioBlock(chans.data(), 2, left.size());
}
} // namespace

TEST_CASE("FdnReverb passes the dry signal through unchanged at mix=0", "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 64, 2);

    core::ReverbParams params = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall);
    params.mix = 0.0f; // fully dry
    reverb.setParams(params);

    std::vector<float> left(64), right(64);
    for (std::size_t n = 0; n < left.size(); ++n)
    {
        left[n]  = std::sin(0.05f * static_cast<float>(n));
        right[n] = std::cos(0.03f * static_cast<float>(n));
    }
    const std::vector<float> refL = left;
    const std::vector<float> refR = right;

    std::array<float*, 2> chans{};
    core::AudioBlock block = makeBlock(left, right, chans);
    reverb.process(block);

    for (std::size_t n = 0; n < left.size(); ++n)
    {
        CHECK(left[n] == refL[n]);   // exact: output = 1.0*dry + 0.0*wet
        CHECK(right[n] == refR[n]);
    }
}

TEST_CASE("FdnReverb is stable and its tail decays", "[dsp][reverb]")
{
    constexpr double      kSampleRate = 48000.0;
    constexpr std::size_t kBlock      = 256;

    dsp::FdnReverb reverb;
    reverb.prepare(kSampleRate, kBlock, 2);

    core::ReverbParams params;
    params.mix        = 1.0f; // fully wet, so we observe the tail directly
    params.decaySec   = 1.2f;
    params.preDelayMs = 5.0f;
    params.dampingHz  = 6000.0f;
    params.widthNorm  = 1.0f;
    reverb.setParams(params);
    reverb.reset();

    // Window boundaries (in blocks) for the energy comparison.
    const auto blockAt = [&](double seconds)
    {
        return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlock));
    };
    const std::size_t earlyStart = blockAt(0.20);
    const std::size_t earlyEnd   = blockAt(0.60);
    const std::size_t lateStart  = blockAt(3.00);
    const std::size_t lateEnd    = blockAt(3.80);
    const std::size_t totalBlocks = lateEnd + 4;

    double earlyEnergy = 0.0;
    double lateEnergy  = 0.0;
    double peak        = 0.0;
    bool   allFinite   = true;

    std::vector<float>    left(kBlock), right(kBlock);
    std::array<float*, 2> chans{};

    for (std::size_t b = 0; b < totalBlocks; ++b)
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        if (b == 0)
        {
            left[0]  = 1.0f; // unit impulse into an otherwise silent stream
            right[0] = 1.0f;
        }

        core::AudioBlock block = makeBlock(left, right, chans);
        reverb.process(block);

        double blockEnergy = 0.0;
        for (std::size_t n = 0; n < kBlock; ++n)
        {
            const float s = left[n];
            if (!std::isfinite(s) || !std::isfinite(right[n]))
                allFinite = false;
            peak         = std::max(peak, static_cast<double>(std::abs(s)));
            blockEnergy += static_cast<double>(s) * static_cast<double>(s);
        }

        if (b >= earlyStart && b < earlyEnd)
            earlyEnergy += blockEnergy;
        else if (b >= lateStart && b < lateEnd)
            lateEnergy += blockEnergy;
    }

    // Stability: every sample is finite and the tank never runs away.
    CHECK(allFinite);
    CHECK(peak < 4.0);

    // The reverb actually produced a tail early on...
    CHECK(earlyEnergy > 0.0);
    // ...and by ~3 s (well past a 1.2 s RT60) it has decayed to a tiny fraction.
    CHECK(lateEnergy < earlyEnergy * 1.0e-3);
}

TEST_CASE("FdnReverb::reset clears the tail to silence", "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 128, 2);
    reverb.setPreset(dsp::ReverbPreset::Cathedral); // long, dense tail

    std::vector<float>    left(128), right(128);
    std::array<float*, 2> chans{};

    // Excite with an impulse to fill the delay lines.
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    left[0]  = 1.0f;
    right[0] = 1.0f;
    core::AudioBlock excite = makeBlock(left, right, chans);
    reverb.process(excite);

    // Reset, then push a silent block: the output must be exactly silent.
    reverb.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    core::AudioBlock silent = makeBlock(left, right, chans);
    reverb.process(silent);

    for (std::size_t n = 0; n < left.size(); ++n)
    {
        CHECK(left[n] == 0.0f);
        CHECK(right[n] == 0.0f);
    }
}
