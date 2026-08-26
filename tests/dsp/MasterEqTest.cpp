// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Master EQ. The decisive case here is a regression guard for a bug introduced
// while fixing something else:
//
// The EQ used to reinstall its factory voicing on every prepare(), which wiped
// whatever the user had dialled in whenever the host changed sample rate or
// block size. The fix guarded setOrganDefaults() behind a `voiced_` flag -- but
// setOrganDefaults() is where the band FREQUENCIES and Qs live, not just the
// gains. Any host that calls setStateInformation() before prepareToPlay() (which
// is normal) restores an EQ gain, which sets `voiced_`, so prepare() then skips
// the defaults entirely and all five sections sit at the BandState fallback of
// 1 kHz / Q 0.7. Five stacked 1 kHz filters, no crash, no warning, and an organ
// that sounds wrong for reasons nothing reports.
//
// Shapes and gains have to be separable, and the shapes must be installed
// unconditionally.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/dsp/MasterEq.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace core = caecilia::core;
namespace dsp  = caecilia::dsp;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 512;

/// Magnitude response at one frequency, measured by driving a sine through the
/// EQ and comparing steady-state RMS in to out.
double gainAt(dsp::MasterEq& eq, double freqHz)
{
    constexpr std::size_t kTotal = 48000;
    std::vector<float> l(kTotal), r(kTotal);
    const double w = 2.0 * 3.14159265358979 * freqHz / kSr;
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        const auto v = static_cast<float>(0.25 * std::sin(w * static_cast<double>(i)));
        l[i] = v;
        r[i] = v;
    }

    double inSq = 0.0;
    for (std::size_t i = kTotal / 2; i < kTotal; ++i)
        inSq += static_cast<double>(l[i]) * l[i];

    for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
    {
        float* chans[2] = { l.data() + pos, r.data() + pos };
        core::AudioBlock b(chans, 2, kBlock);
        eq.process(b);
    }

    double outSq = 0.0;
    for (std::size_t i = kTotal / 2; i < kTotal; ++i)
        outSq += static_cast<double>(l[i]) * l[i];

    return std::sqrt(outSq / inSq);
}
} // namespace

TEST_CASE("Band shapes survive a gain being set before prepare",
          "[dsp][mastereq][regression]")
{
    // Exactly the order a host produces: restore state, then prepare.
    dsp::MasterEq eq;
    eq.setBandGain(dsp::MasterEq::Warmth, 9.0f);
    eq.prepare(kSr, kBlock, 2);

    // Warmth is a LOW SHELF at 110 Hz. +9 dB there must leave 800 Hz essentially
    // alone. If the band shapes were skipped, Warmth is a 1 kHz shelf instead and
    // 800 Hz comes out around +7 dB.
    const double at800 = gainAt(eq, 800.0);
    INFO("gain at 800 Hz = " << 20.0 * std::log10(at800) << " dB");
    CHECK(at800 < 1.15); // under +1.2 dB

    // And the boost must actually be where it belongs.
    dsp::MasterEq eq2;
    eq2.setBandGain(dsp::MasterEq::Warmth, 9.0f);
    eq2.prepare(kSr, kBlock, 2);
    const double at60 = gainAt(eq2, 60.0);
    INFO("gain at 60 Hz = " << 20.0 * std::log10(at60) << " dB");
    CHECK(at60 > 2.0); // well above +6 dB
}

TEST_CASE("A user's EQ survives a sample-rate change", "[dsp][mastereq][regression]")
{
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);
    eq.setBandGain(dsp::MasterEq::Air, -12.0f);

    // The host switches rate; prepare() runs again.
    eq.prepare(96000.0, kBlock, 2);

    CHECK(eq.bandGain(dsp::MasterEq::Air) == Approx(-12.0f));
    // ...and the factory shapes are still in force for the bands the user did not
    // touch, so Warmth is still a low shelf and not a 1 kHz default.
    CHECK(eq.bandGain(dsp::MasterEq::Boxiness) == Approx(-2.5f));
}

TEST_CASE("A disabled EQ passes the signal through untouched", "[dsp][mastereq]")
{
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);
    eq.setEnabled(false);
    CHECK(gainAt(eq, 110.0) == Approx(1.0).epsilon(0.001));
    CHECK(gainAt(eq, 9000.0) == Approx(1.0).epsilon(0.001));
}

TEST_CASE("The factory voicing is the documented organ curve", "[dsp][mastereq]")
{
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);

    // +2 warmth, -2.5 boxiness, 0 body, +1.5 presence, +2 air.
    CHECK(eq.bandGain(dsp::MasterEq::Warmth)   == Approx(2.0f));
    CHECK(eq.bandGain(dsp::MasterEq::Boxiness) == Approx(-2.5f));
    CHECK(eq.bandGain(dsp::MasterEq::Body)     == Approx(0.0f));
    CHECK(eq.bandGain(dsp::MasterEq::Presence) == Approx(1.5f));
    CHECK(eq.bandGain(dsp::MasterEq::Air)      == Approx(2.0f));

    // And it really is gentle: nothing beyond a few dB anywhere.
    for (const double f : { 60.0, 110.0, 315.0, 800.0, 3200.0, 9000.0, 15000.0 })
    {
        const double g = 20.0 * std::log10(gainAt(eq, f));
        INFO("at " << f << " Hz: " << g << " dB");
        CHECK(std::fabs(g) < 4.0);
    }
}


TEST_CASE("An EQ setting round-trips through Params", "[dsp][mastereq]")
{
    // The snapshot is what the host parameter layer and the saved document both
    // talk in, so anything it drops is a setting the user loses on reload.
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);

    dsp::MasterEq::Params want;
    want.bands[dsp::MasterEq::Warmth]   = { 90.0f,   0.9f,  4.5f };
    want.bands[dsp::MasterEq::Boxiness] = { 400.0f,  1.4f, -6.0f };
    want.bands[dsp::MasterEq::Body]     = { 950.0f,  0.6f,  1.0f };
    want.bands[dsp::MasterEq::Presence] = { 2800.0f, 1.2f, -1.5f };
    want.bands[dsp::MasterEq::Air]      = { 12000.0f, 0.5f, 3.0f };
    want.enabled = false;
    eq.setParams(want);

    const dsp::MasterEq::Params got = eq.params();
    for (std::size_t i = 0; i < dsp::MasterEq::kBands; ++i)
    {
        INFO("band " << i);
        CHECK(got.bands[i].freqHz == Approx(want.bands[i].freqHz));
        CHECK(got.bands[i].q      == Approx(want.bands[i].q));
        CHECK(got.bands[i].gainDb == Approx(want.bands[i].gainDb));
    }
    CHECK(got.enabled == want.enabled);
}

TEST_CASE("organDefaults is the curve prepare installs", "[dsp][mastereq]")
{
    // Two functions used to hold two halves of the organ voicing, so a change to
    // one could silently disagree with the other. There is one definition now, and
    // this is what pins the halves to it.
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);

    const dsp::MasterEq::Params fresh = eq.params();
    const dsp::MasterEq::Params spec  = dsp::MasterEq::organDefaults();
    for (std::size_t i = 0; i < dsp::MasterEq::kBands; ++i)
    {
        INFO("band " << i);
        CHECK(fresh.bands[i].freqHz == Approx(spec.bands[i].freqHz));
        CHECK(fresh.bands[i].q      == Approx(spec.bands[i].q));
        CHECK(fresh.bands[i].gainDb == Approx(spec.bands[i].gainDb));
    }
}

TEST_CASE("A custom band shape survives a sample-rate change",
          "[dsp][mastereq][regression]")
{
    // prepare() reinstalls the organ band shapes so nothing is ever left at the
    // 1 kHz fallback. That is right until somebody chooses their own frequency,
    // at which point reinstalling silently throws it away -- while keeping the
    // gain that was chosen to go with it, which is the worst of both.
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);
    eq.setBand(dsp::MasterEq::Warmth, 60.0f, 0.9f, 6.0f);

    eq.prepare(96000.0, kBlock, 2); // the host switched rate

    const dsp::MasterEq::Params p = eq.params();
    CHECK(p.bands[dsp::MasterEq::Warmth].freqHz == Approx(60.0f));
    CHECK(p.bands[dsp::MasterEq::Warmth].q      == Approx(0.9f));
    CHECK(p.bands[dsp::MasterEq::Warmth].gainDb == Approx(6.0f));
}

TEST_CASE("Toggling the EQ glides instead of clicking", "[dsp][mastereq]")
{
    // setEnabled(false) used to return from process() on the very next sample, so
    // the whole voicing disappeared in one step. On a sustained organ chord that
    // is an audible click -- and a second one on the way back in, as five biquads
    // resumed from state frozen at the moment of the cut.
    constexpr std::size_t kTotal = 48000;
    constexpr double      kFreq  = 110.0; // right under the Warmth shelf

    std::vector<float> l(kTotal), r(kTotal);
    const double w = 2.0 * 3.14159265358979 * kFreq / kSr;
    for (std::size_t i = 0; i < kTotal; ++i)
        l[i] = r[i] = static_cast<float>(0.25 * std::sin(w * static_cast<double>(i)));

    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);

    // On a block boundary that actually exists: kTotal/2 is not a multiple of the
    // block size, so flipping on `pos == kTotal/2` never fired at all and the test
    // passed against the very bug it was written for.
    const std::size_t flipAt = 46 * kBlock;
    bool              flipped = false;
    for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
    {
        if (!flipped && pos >= flipAt)
        {
            eq.setEnabled(false);
            flipped = true;
        }
        float* chans[2] = { l.data() + pos, r.data() + pos };
        core::AudioBlock b(chans, 2, kBlock);
        eq.process(b);
    }
    REQUIRE(flipped);

    // The largest step this sine can take between samples at the level it is
    // actually running at. Derived from the rendered peak rather than assumed, so
    // the bound tracks whatever gain the band is set to.
    double peak = 0.0;
    for (std::size_t i = flipAt / 2; i < kTotal; ++i)
        peak = std::max(peak, std::abs(static_cast<double>(l[i])));
    const double natural = peak * 2.0 * 3.14159265358979 * kFreq / kSr;

    double worstStep = 0.0;
    for (std::size_t i = flipAt - 1000; i < flipAt + 8000; ++i)
        worstStep = std::max(worstStep, std::abs(static_cast<double>(l[i] - l[i - 1])));

    // Measured on this signal, worst sample-to-sample step as a multiple of the
    // largest step the sine itself can take:
    //
    //     hard bypass branch          18.1x     the bug this replaced
    //     glide, redesigned per block  2.52x    better, still a tick
    //     glide, redesigned per 64     1.15x    inside the signal's own slew
    //
    // The threshold sits below the middle row on purpose, so removing either the
    // glide or the sub-blocking that makes it smooth fails here.
    INFO("worst step " << worstStep << " against a natural " << natural);
    CHECK(worstStep < natural * 1.6);
}

TEST_CASE("A settled bypass is an exact pass-through, and re-enabling restores the curve",
          "[dsp][mastereq]")
{
    dsp::MasterEq eq;
    eq.prepare(kSr, kBlock, 2);
    // At its own corner a low shelf is only part-way up, so this is +0.7 dB, not
    // the +2 dB the band is set to. It only has to be distinguishable from unity.
    const double voicedGain = gainAt(eq, 110.0);
    REQUIRE(voicedGain > 1.05);

    eq.setEnabled(false);
    // gainAt renders a full second, so the 30 ms glide is long finished.
    CHECK(gainAt(eq, 110.0) == Approx(1.0).epsilon(0.0005));

    // Once settled, it must be EXACT -- not merely close. A unity biquad still
    // carries two samples of stale memory, so the skip only holds if the state
    // was cleared on the way out.
    std::vector<float> in(kBlock), l(kBlock), r(kBlock);
    for (std::size_t i = 0; i < kBlock; ++i)
        in[i] = l[i] = r[i] = static_cast<float>(0.3 * std::sin(0.07 * static_cast<double>(i)));

    float* chans[2] = { l.data(), r.data() };
    core::AudioBlock b(chans, 2, kBlock);
    eq.process(b);

    bool exact = true;
    for (std::size_t i = 0; i < kBlock; ++i)
        exact = exact && (l[i] == in[i]) && (r[i] == in[i]);
    CHECK(exact);

    // And switching it back on brings the curve back.
    eq.setEnabled(true);
    CHECK(gainAt(eq, 110.0) == Approx(voicedGain).epsilon(0.005));
}
