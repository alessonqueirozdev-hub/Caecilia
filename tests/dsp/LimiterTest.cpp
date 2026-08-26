// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Master-limiter tests. The first case is the regression guard for the defect
// that made every output sample carry a hard splice: prepare() sized the
// look-ahead ring from the DEFAULT lookAheadMs_, and the caller's subsequent
// setParams() raised look_ past the ring length, so the read offset wrapped and
// the effective delay jumped between two values twice per ring cycle.
//
// The invariant that catches it is simple and worth keeping: with no gain
// reduction, a limiter is a pure delay line of exactly latencySamples().
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/Limiter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
namespace core = caecilia::core;
namespace dsp  = caecilia::dsp;

namespace
{
constexpr core::SampleRate kSr    = 44100.0;
constexpr std::size_t      kBlock = 512;

/// Wrap two channel vectors as an AudioBlock over [offset, offset + frames).
core::AudioBlock blockOver(std::vector<float>& l, std::vector<float>& r,
                           std::size_t offset, std::size_t frames)
{
    static float* chans[2];
    chans[0] = l.data() + offset;
    chans[1] = r.data() + offset;
    return core::AudioBlock(chans, 2, frames);
}
} // namespace

TEST_CASE("Limiter delays by exactly latencySamples when it is not reducing",
          "[dsp][limiter][regression]")
{
    dsp::Limiter lim;
    lim.prepare(kSr, kBlock, 2);
    // The real call order from the plugin: prepare() first, then a LONGER
    // look-ahead than the default. This is what used to break the ring.
    lim.setParams(/*ceilingDb*/ -3.0f, /*lookAheadMs*/ 2.5f,
                  /*holdMs*/ 400.0f, /*releaseMs*/ 600.0f);

    const std::size_t look = lim.latencySamples();
    REQUIRE(look > 1);

    // A quiet ramp: well under the ceiling, so gEnv_ stays at 1 and the limiter is
    // a pure delay. Any wrap in the read offset shows up as a broken ramp.
    constexpr std::size_t kTotal = 4096;
    std::vector<float> l(kTotal), r(kTotal);
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        const float v = 0.10f * std::sin(0.05f * static_cast<float>(i));
        l[i] = v;
        r[i] = v;
    }
    const std::vector<float> input = l;

    for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
    {
        core::AudioBlock b = blockOver(l, r, pos, kBlock);
        lim.process(b);
    }

    // Every output sample past the priming window must equal the input `look`
    // samples earlier. Before the fix the delay alternated between two different
    // values, so this failed within the first ring cycle.
    std::size_t mismatches = 0;
    for (std::size_t i = look; i < kTotal; ++i)
        if (std::fabs(l[i] - input[i - look]) > 1.0e-6f)
            ++mismatches;

    CHECK(mismatches == 0);
}

TEST_CASE("Limiter holds the ceiling on a signal that would clip", "[dsp][limiter]")
{
    dsp::Limiter lim;
    lim.prepare(kSr, kBlock, 2);
    lim.setParams(-3.0f, 2.5f, 400.0f, 600.0f);

    const float ceilingLin = std::pow(10.0f, -3.0f / 20.0f);

    constexpr std::size_t kTotal = 8192;
    std::vector<float> l(kTotal), r(kTotal);
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        // +6 dBFS sine: twice the ceiling, sustained, exactly the fff Tutti case.
        const float v = 2.0f * std::sin(0.03f * static_cast<float>(i));
        l[i] = v;
        r[i] = v;
    }

    for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
    {
        core::AudioBlock b = blockOver(l, r, pos, kBlock);
        lim.process(b);
    }

    // Skip the priming window; after that nothing may exceed the ceiling by more
    // than a hair. A too-short look-ahead leaks peaks straight through.
    float peak = 0.0f;
    for (std::size_t i = 2 * kBlock; i < kTotal; ++i)
        peak = std::max(peak, std::fabs(l[i]));

    CHECK(peak <= ceilingLin * 1.02f);
    CHECK(lim.gainReductionDb() > 0.0f); // it really is working, not bypassed
}

TEST_CASE("Limiter latency is stable across sample rates", "[dsp][limiter]")
{
    for (const core::SampleRate sr : {44100.0, 48000.0, 88200.0, 96000.0})
    {
        dsp::Limiter lim;
        lim.prepare(sr, kBlock, 2);
        lim.setParams(-3.0f, 2.5f, 400.0f, 600.0f);

        // The reported latency must be the requested window in samples — the host
        // compensates by this number, so a mismatch shifts the whole track.
        const auto expected = static_cast<std::size_t>(2.5 * 0.001 * sr + 0.5);
        CHECK(lim.latencySamples() == expected);
    }
}

TEST_CASE("Limiter refuses to let a NaN through", "[dsp][limiter][robustness]")
{
    dsp::Limiter lim;
    lim.prepare(kSr, kBlock, 2);
    lim.setParams(-3.0f, 2.5f, 400.0f, 600.0f);

    std::vector<float> l(kBlock, 0.1f), r(kBlock, 0.1f);
    l[10] = std::numeric_limits<float>::quiet_NaN();
    r[11] = std::numeric_limits<float>::infinity();

    core::AudioBlock b = blockOver(l, r, 0, kBlock);
    lim.process(b);

    for (std::size_t i = 0; i < kBlock; ++i)
    {
        CHECK(std::isfinite(l[i]));
        CHECK(std::isfinite(r[i]));
    }
}
