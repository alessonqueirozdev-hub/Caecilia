// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// FdnReverb tests: numerical STABILITY (a unitary feedback matrix plus
// sub-unity per-line gains must never let the tank blow up or produce
// non-finite samples) and DECAY (energy injected by an impulse fades toward
// silence over roughly the configured RT60). Also pins the mix=0 dry-passthrough
// and reset-clears-the-tail contracts.
//
// The second half covers the coefficient GATE: setParams skips the decay/damping/
// width rebuild when nothing audible moved, so that automating a reverb knob stops
// putting a pile of transcendentals inside the audio callback on every block. A
// gate is a thing that can be wrong in two directions, so both are pinned — it
// must not fire when it should (a silent or runaway reverb), and it must not
// decline to fire while a slow ramp walks the parameter arbitrarily far away.
//
// The first three cases predate all of that and are deliberately UNCHANGED: they
// are the evidence that the cheaper arithmetic underneath (a minimax sine for the
// tail modulation, binary exponentiation for the decay gains, one shared pole for
// the sixteen dampers) did not change what the reverb sounds like.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/FdnReverb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
namespace core = caecilia::core;
namespace dsp  = caecilia::dsp;

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


// ---------------------------------------------------------------------------
// The coefficient gate.
// ---------------------------------------------------------------------------

TEST_CASE("FdnReverb rebuilds its coefficients on every prepare", "[dsp][reverb]")
{
    constexpr double      kSampleRate = 48000.0;
    constexpr std::size_t kBlock      = 256;

    dsp::FdnReverb reverb;

    // A console can choose a preset before the host has ever called prepare, and
    // the plugin's own preset path does exactly that. At that moment every delay
    // length is still zero, so the gains derived here are meaningless: 10^0 is 1,
    // and all sixteen lines land on the 0.9999 ceiling.
    reverb.setParams(dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall));

    // prepare() re-derives the delay lengths for the real sample rate and then
    // calls setParams with the SAME parameter set it already holds. If the gate
    // is allowed to discard that call, those meaningless near-unity gains survive
    // into a tank that is now real, and the network rings for the better part of
    // an hour instead of decaying. This is why prepare() clears the ready flag.
    reverb.prepare(kSampleRate, kBlock, 2);
    CHECK(reverb.coefficientRecomputes() >= 2);

    core::ReverbParams params = reverb.params();
    params.mix = 1.0f; // fully wet, so the tail is what we measure
    reverb.setParams(params);
    reverb.reset();

    const auto blockAt = [&](double seconds)
    {
        return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlock));
    };
    const std::size_t earlyStart  = blockAt(0.20);
    const std::size_t earlyEnd    = blockAt(0.60);
    const std::size_t lateStart   = blockAt(6.00);
    const std::size_t lateEnd     = blockAt(7.00);
    const std::size_t totalBlocks = lateEnd + 4;

    double earlyEnergy = 0.0;
    double lateEnergy  = 0.0;

    std::vector<float>    left(kBlock), right(kBlock);
    std::array<float*, 2> chans{};

    for (std::size_t b = 0; b < totalBlocks; ++b)
    {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        if (b == 0)
        {
            left[0]  = 1.0f;
            right[0] = 1.0f;
        }

        core::AudioBlock block = makeBlock(left, right, chans);
        reverb.process(block);

        double blockEnergy = 0.0;
        for (std::size_t n = 0; n < kBlock; ++n)
            blockEnergy += static_cast<double>(left[n]) * static_cast<double>(left[n]);

        if (b >= earlyStart && b < earlyEnd)
            earlyEnergy += blockEnergy;
        else if (b >= lateStart && b < lateEnd)
            lateEnergy += blockEnergy;
    }

    REQUIRE(earlyEnergy > 0.0);
    // Hall is a 2.6 s RT60, so by 6 s the tail is 80-odd dB down. With stale
    // pre-prepare gains it barely moves at all and this ratio sits near 1.
    CHECK(lateEnergy < earlyEnergy * 1.0e-3);
}

TEST_CASE("FdnReverb clamps a NaN parameter set instead of propagating it", "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 128, 2);

    // A NaN can reach here from an uninitialised host parameter or a corrupt
    // saved state. Before the shared clamp, a NaN mix multiplied the entire
    // output away and a NaN decay poisoned every feedback gain permanently --
    // an unrecoverable silence with no error anywhere.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    core::ReverbParams bad = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall);
    bad.mix       = nan;
    bad.decaySec  = nan;
    bad.dampingHz = nan;
    bad.widthNorm = nan;
    bad.bassBloom = nan;
    reverb.setParams(bad);

    CHECK(std::isfinite(reverb.params().mix));
    CHECK(std::isfinite(reverb.params().decaySec));
    CHECK(std::isfinite(reverb.params().dampingHz));
    CHECK(std::isfinite(reverb.params().widthNorm));
    CHECK(std::isfinite(reverb.params().bassBloom));

    // And the tank must still be usable afterwards.
    core::ReverbParams good = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Chamber);
    good.mix = 1.0f;
    reverb.setParams(good);
    reverb.reset();

    std::vector<float>    left(128), right(128);
    std::array<float*, 2> chans{};
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    left[0] = right[0] = 1.0f;

    bool   allFinite = true;
    double energy    = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        core::AudioBlock block = makeBlock(left, right, chans);
        reverb.process(block);
        for (std::size_t n = 0; n < left.size(); ++n)
        {
            allFinite = allFinite && std::isfinite(left[n]) && std::isfinite(right[n]);
            energy += static_cast<double>(left[n]) * static_cast<double>(left[n]);
        }
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    CHECK(allFinite);
    CHECK(energy > 0.0);
}

TEST_CASE("FdnReverb::wouldRecompute ignores changes below the audible thresholds",
          "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 128, 2);

    const core::ReverbParams p = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall);
    reverb.setParams(p);

    CHECK_FALSE(reverb.wouldRecompute(p));

    core::ReverbParams tiny = p;
    tiny.decaySec  += 0.005f; // half an epsilon each
    tiny.dampingHz += 10.0f;
    tiny.widthNorm -= 0.002f;
    tiny.bassBloom += 0.005f;
    CHECK_FALSE(reverb.wouldRecompute(tiny));

    // Mix and pre-delay never drive a coefficient, so no size of change to them
    // should cost a rebuild -- they are applied unconditionally instead.
    core::ReverbParams cheap = p;
    cheap.mix        = 0.9f;
    cheap.preDelayMs = 55.0f;
    CHECK_FALSE(reverb.wouldRecompute(cheap));

    core::ReverbParams audible = p;
    audible.decaySec += 0.5f;
    CHECK(reverb.wouldRecompute(audible));
}

TEST_CASE("FdnReverb::wouldRecompute compares clamped values, not raw requests",
          "[dsp][reverb]")
{
    constexpr double kSampleRate = 48000.0; // the damping ceiling lands at 23520 Hz
    dsp::FdnReverb   reverb;
    reverb.prepare(kSampleRate, 128, 2);

    core::ReverbParams p = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall);
    p.dampingHz = 30000.0f; // above the legal ceiling
    reverb.setParams(p);
    REQUIRE(reverb.params().dampingHz < 30000.0f); // it really was clamped

    // The same request asks for nothing new: the reverb already sits at the value
    // it resolves to. Comparing the raw 30000 against the stored 23520 reported
    // "changed" forever -- so a sweep that pinned the corner above Nyquist put the
    // entire coefficient rebuild inside the callback on every single block, which
    // is precisely the case the gate exists to prevent.
    CHECK_FALSE(reverb.wouldRecompute(p));

    const auto before = reverb.coefficientRecomputes();
    for (int i = 0; i < 200; ++i)
        reverb.setParams(p);
    CHECK(reverb.coefficientRecomputes() == before);
}

TEST_CASE("FdnReverb rebuilds nothing for repeated identical setParams calls",
          "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 128, 2);

    const core::ReverbParams p = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Cathedral);
    reverb.setParams(p);
    const auto afterFirst = reverb.coefficientRecomputes();

    // What a host does when a parameter is automated but parked: one message per
    // block, all identical.
    for (int i = 0; i < 200; ++i)
        reverb.setParams(p);

    CHECK(reverb.coefficientRecomputes() == afterFirst);
}

TEST_CASE("FdnReverb bounds its rebuild count across an automation ramp", "[dsp][reverb]")
{
    dsp::FdnReverb reverb;
    reverb.prepare(48000.0, 128, 2);

    core::ReverbParams p = dsp::FdnReverb::presetParams(dsp::ReverbPreset::Hall);
    p.decaySec = 2.0f;
    reverb.setParams(p);
    const auto before = reverb.coefficientRecomputes();

    // A host sweeping the decay knob sends one value per block. Every individual
    // step here is a quarter of the epsilon, so gating against the LAST STORED
    // value would rebuild exactly zero times and leave the tail stuck at 2.0 s
    // while the host believes it arrived at 2.5. Gating against the last BUILT
    // value bounds the error at one epsilon instead: about one rebuild per 0.01 s
    // of travel, so fifty rather than two hundred.
    constexpr int      kSteps = 200;
    core::ReverbParams last   = p;
    for (int i = 1; i <= kSteps; ++i)
    {
        last.decaySec = 2.0f + 0.5f * static_cast<float>(i) / static_cast<float>(kSteps);
        reverb.setParams(last);
    }

    const auto rebuilds = reverb.coefficientRecomputes() - before;
    INFO("rebuilds = " << rebuilds << " over " << kSteps << " steps");
    CHECK(rebuilds >= 40); // 0.5 s of travel against a 0.01 s epsilon
    CHECK(rebuilds <= 60);

    // ...and the coefficients really did follow the knob: asking for the value it
    // ended on is not a change.
    CHECK_FALSE(reverb.wouldRecompute(last));
}
