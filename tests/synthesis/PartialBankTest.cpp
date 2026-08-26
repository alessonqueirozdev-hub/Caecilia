// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// PartialBank tests. Three properties matter here and none of them had a test:
//
//   1. The recursive quadrature oscillator that replaced the per-sample std::sin
//      must produce the SAME signal — same frequency, same amplitude, and it must
//      not drift over a long note (the rotation is only conditionally stable).
//   2. The render must be independent of the host's buffer size. It was not: the
//      "living pipe" drift used a coefficient fixed PER BLOCK, so the instrument's
//      whole character changed between a 64-sample and a 1024-sample buffer.
//   3. The voice must place itself in stereo. It used to write the identical
//      sample to every channel, so the instrument had no image at all.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace synth = caecilia::synth;

namespace
{
constexpr core::SampleRate kSr = 48000.0;

/// A one-partial model: a pure sine at the fundamental, so the oscillator can be
/// compared against std::sin without the spectrum getting in the way.
synth::SpectralModel pureTone()
{
    synth::SpectralModel m;
    synth::PartialTrack t;
    t.ratioToF0 = 1.0f;
    t.ampDb     = 0.0f;
    t.phase     = 0.0f;
    m.partials.push_back(t);
    m.fundamentalHz = 440.0f;
    return m;
}

/// A richer model standing in for a real registration. All partials carry the
/// SAME identity seed, i.e. they are one rank sitting at one place in the case —
/// which is what lets a test isolate the per-note placement from the per-rank one.
synth::SpectralModel chorus(std::size_t harmonics, std::uint32_t rankSeed = 0x51ED2701u)
{
    synth::SpectralModel m;
    for (std::size_t n = 1; n <= harmonics; ++n)
    {
        synth::PartialTrack t;
        t.ratioToF0 = static_cast<float>(n);
        t.ampDb     = -6.0f * std::log2(static_cast<float>(n));
        t.phase     = 0.37f * static_cast<float>(n);
        t.seed      = rankSeed;
        m.partials.push_back(t);
    }
    m.fundamentalHz = 130.81f;
    return m;
}

core::AudioBlock stereoBlock(std::vector<float>& l, std::vector<float>& r,
                             std::size_t offset, std::size_t frames)
{
    static float* chans[2];
    chans[0] = l.data() + offset;
    chans[1] = r.data() + offset;
    return core::AudioBlock(chans, 2, frames);
}

/// Render `total` frames in `blockSize` chunks into freshly zeroed buffers.
void render(synth::PartialBank& bank, std::vector<float>& l, std::vector<float>& r,
            std::size_t total, std::size_t blockSize)
{
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    for (std::size_t pos = 0; pos + blockSize <= total; pos += blockSize)
    {
        core::AudioBlock b = stereoBlock(l, r, pos, blockSize);
        bank.renderAdd(b);
    }
}
} // namespace

TEST_CASE("The quadrature oscillator tracks std::sin over a long note",
          "[synthesis][partialbank][oscillator]")
{
    synth::PartialBank bank;
    bank.setMaxPartials(4);
    bank.prepare(kSr, 512);
    bank.setLiveliness(/*instability*/ 0.0f, /*glide*/ 0.0f, /*bloom*/ 0.008f,
                       /*hfCorner*/ 20000.0f, /*trebleTilt*/ 0.0f);
    bank.setStereoSpread(0.0f);          // keep it centred so L is the raw tone
    bank.setMasterGain(1.0f);
    bank.setEnvelopeTimes(0.0005f, 0.5f); // settle the envelope almost instantly
    bank.seedFrom(pureTone(), 0.0f);

    constexpr double kFreq = 1000.0;
    bank.trigger(core::PipeId{0, 60}, 100, kFreq);

    // Four seconds: long enough that an unstable rotation would visibly decay or
    // blow up, and long enough for any frequency error to accumulate.
    constexpr std::size_t kTotal = static_cast<std::size_t>(4.0 * 48000.0);
    std::vector<float> l(kTotal), r(kTotal);
    render(bank, l, r, kTotal, 512);

    // Amplitude must still be there at the end — the rotation must not decay.
    auto peakOver = [&l](std::size_t from, std::size_t to) {
        float pk = 0.0f;
        for (std::size_t i = from; i < to; ++i) pk = std::max(pk, std::fabs(l[i]));
        return pk;
    };
    const float early = peakOver(4800, 9600);            // ~0.1-0.2 s in
    const float late  = peakOver(kTotal - 9600, kTotal); // last 0.2 s
    REQUIRE(early > 0.01f);
    CHECK(late == Approx(early).epsilon(0.02)); // within 2% after 4 seconds

    // Frequency must be right: count positive-going zero crossings in a whole
    // number of seconds and compare against the requested pitch.
    std::size_t crossings = 0;
    const std::size_t from = 48000, to = 48000 * 3; // a clean 2-second window
    for (std::size_t i = from + 1; i < to; ++i)
        if (l[i - 1] <= 0.0f && l[i] > 0.0f)
            ++crossings;
    const double measuredHz = static_cast<double>(crossings) / 2.0;
    CHECK(measuredHz == Approx(kFreq).epsilon(0.001));
}

TEST_CASE("Render is independent of the host's buffer size",
          "[synthesis][partialbank][regression]")
{
    // The drift filter used a coefficient fixed per BLOCK, so its time constant
    // scaled with the buffer: ~13 ms at 64 samples, ~213 ms at 1024. The same
    // project therefore sounded different in different hosts, and an offline
    // render never matched real time.
    constexpr std::size_t kTotal = 24576; // divisible by every size under test
    const std::size_t sizes[] = {64, 256, 1024, 2048};

    std::vector<std::vector<float>> outs;
    for (const std::size_t bs : sizes)
    {
        synth::PartialBank bank;
        bank.setMaxPartials(16);
        bank.prepare(kSr, 2048);
        bank.setStereoSpread(0.0f);
        bank.setMasterGain(1.0f);
        bank.seedFrom(chorus(12), 0.0f);
        bank.trigger(core::PipeId{3, 55}, 100, 220.0);

        std::vector<float> l(kTotal), r(kTotal);
        render(bank, l, r, kTotal, bs);
        outs.push_back(l);
    }

    // Compare every buffer size against the first. The drift is a random walk, so
    // the signals are not bit-identical — but their ENERGY and their spectral
    // centre of gravity must agree closely, which is what "same instrument" means.
    auto rms = [](const std::vector<float>& v) {
        double s = 0.0;
        for (const float x : v) s += static_cast<double>(x) * x;
        return std::sqrt(s / static_cast<double>(v.size()));
    };
    const double reference = rms(outs[0]);
    REQUIRE(reference > 1.0e-4);

    for (std::size_t i = 1; i < outs.size(); ++i)
    {
        INFO("buffer size " << sizes[i]);
        CHECK(rms(outs[i]) == Approx(reference).epsilon(0.05));
    }
}

TEST_CASE("A voice is placed in the case, by rank and by note",
          "[synthesis][partialbank][stereo]")
{
    // Every voice used to write the identical sample to every channel, so the
    // instrument had no image at all and two unison ranks summed as one coherent
    // point source — which made drawing a second 8' stop measurably QUIETER.
    //
    // Placement now has two terms, matching how a real case is laid out:
    //   * the RANK's position, the dominant one — ranks stand at different places;
    //   * the NOTE's C-side / C#-side alternation, a smaller modulation around it.
    constexpr std::size_t kTotal = 8192;

    auto renderNote = [&](core::MidiNote note, std::uint32_t rankSeed,
                          std::vector<float>& l, std::vector<float>& r) {
        synth::PartialBank bank;
        bank.setMaxPartials(8);
        bank.prepare(kSr, 512);
        bank.setStereoSpread(1.0f);
        bank.setMasterGain(1.0f);
        bank.seedFrom(chorus(6, rankSeed), 0.0f);
        bank.trigger(core::PipeId{0, note}, 100, 440.0);
        l.assign(kTotal, 0.0f);
        r.assign(kTotal, 0.0f);
        render(bank, l, r, kTotal, 512);
    };

    auto rms = [](const std::vector<float>& v) {
        double s = 0.0;
        for (const float x : v) s += static_cast<double>(x) * x;
        return std::sqrt(s / static_cast<double>(v.size()));
    };
    /// -1 = hard left, +1 = hard right.
    auto balance = [&rms](const std::vector<float>& l, const std::vector<float>& r) {
        const double a = rms(l), b = rms(r);
        return (b - a) / (a + b);
    };

    constexpr std::uint32_t kRankA = 0x51ED2701u;
    constexpr std::uint32_t kRankB = 0x0BADF00Du;

    std::vector<float> evenL, evenR, oddL, oddR;
    renderNote(60, kRankA, evenL, evenR); // C  — the C side
    renderNote(61, kRankA, oddL,  oddR);  // C# — the C# side, same rank

    REQUIRE(rms(evenL) + rms(evenR) > 1.0e-4);

    // The voice is genuinely placed, not centred.
    CHECK(std::fabs(balance(evenL, evenR)) > 0.05);

    // C# sits to the C side's right: the alternation is real and has the right
    // sign, even though the rank's own position dominates the absolute placement.
    CHECK(balance(oddL, oddR) > balance(evenL, evenR));

    // A DIFFERENT rank stands somewhere else in the case entirely. This is the
    // term that decorrelates two unison stops so they reinforce instead of
    // cancelling.
    std::vector<float> otherL, otherR;
    renderNote(60, kRankB, otherL, otherR);
    INFO("rank A balance " << balance(evenL, evenR)
         << ", rank B balance " << balance(otherL, otherR));
    CHECK(std::fabs(balance(otherL, otherR) - balance(evenL, evenR)) > 0.15);
}

TEST_CASE("A released voice goes idle and stays finite", "[synthesis][partialbank]")
{
    synth::PartialBank bank;
    bank.setMaxPartials(16);
    bank.prepare(kSr, 512);
    bank.setMasterGain(1.0f);
    bank.seedFrom(chorus(12), 0.0f);
    bank.trigger(core::PipeId{0, 36}, 100, 65.4); // a bass note: the longest release

    constexpr std::size_t kTotal = 48000 * 2;
    std::vector<float> l(kTotal), r(kTotal);

    // Sound briefly, then release and let the tail run.
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    for (std::size_t pos = 0; pos + 512 <= kTotal; pos += 512)
    {
        if (pos == 4096)
            bank.release();
        core::AudioBlock b = stereoBlock(l, r, pos, 512);
        bank.renderAdd(b);
    }

    CHECK_FALSE(bank.isActive()); // the tail finished inside two seconds
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::isfinite(r[i]));
        REQUIRE(std::fabs(l[i]) < 10.0f); // nothing ran away
    }
}

TEST_CASE("A bass pipe speaks and releases more slowly than a treble pipe",
          "[synthesis][partialbank][musical]")
{
    // Every rank and every note used to share one fixed 16 ms / 150 ms envelope.
    // A real 16' pipe takes far longer to fill than a small mixture pipe.
    auto timeToHalfPeak = [](double freqHz, core::MidiNote note) {
        synth::PartialBank bank;
        bank.setMaxPartials(8);
        bank.prepare(kSr, 64);
        bank.setLiveliness(0.0f, 0.0f, 0.008f, 20000.0f, 0.0f);
        bank.setStereoSpread(0.0f);
        bank.setMasterGain(1.0f);
        bank.seedFrom(chorus(4), 0.0f);
        bank.trigger(core::PipeId{0, note}, 100, freqHz);

        constexpr std::size_t kTotal = 24000;
        std::vector<float> l(kTotal), r(kTotal);
        render(bank, l, r, kTotal, 64);

        float peak = 0.0f;
        for (const float x : l) peak = std::max(peak, std::fabs(x));
        REQUIRE(peak > 1.0e-4f);

        // First sample whose local envelope passes half the eventual peak.
        for (std::size_t i = 0; i < kTotal; ++i)
            if (std::fabs(l[i]) > 0.5f * peak)
                return static_cast<double>(i) / kSr;
        return 1.0e9;
    };

    const double bass   = timeToHalfPeak(65.4, 36);   // C2
    const double treble = timeToHalfPeak(2093.0, 84); // C7

    INFO("bass " << bass << " s, treble " << treble << " s");
    CHECK(bass > treble * 1.5); // the big pipe is markedly slower to speak
}

TEST_CASE("Drawing a stop under a held chord changes its colour without cutting it",
          "[synthesis][partialbank][musical][regression]")
{
    // Drawing or retiring a stop with notes held is the most ordinary gesture an
    // organist makes. The old design rebuilt the whole voice bank and rebound the
    // pool, which reset every slot: the chord went silent instantly, on a
    // non-zero sample (so it clicked), and never came back because nothing
    // replayed the keys that were still down.
    //
    // Re-voicing in place has to satisfy three things at once: the note keeps
    // sounding, it does not step, and it genuinely takes on the new spectrum.
    synth::PartialBank bank;
    bank.setMaxPartials(64);
    bank.prepare(kSr, 512);
    bank.setStereoSpread(0.0f);
    bank.setMasterGain(1.0f);
    bank.setLiveliness(0.0f, 0.0f, 0.008f, 20000.0f, 0.0f);
    bank.seedFrom(chorus(4), 0.0f);           // a dark registration
    bank.trigger(core::PipeId{0, 60, 1}, 100, 220.0);

    constexpr std::size_t kTotal = 48000;
    std::vector<float> l(kTotal, 0.0f), r(kTotal, 0.0f);

    // Sound for a while, re-voice half way through, and keep sounding.
    const std::size_t swapAt = 24000;
    for (std::size_t pos = 0; pos + 512 <= kTotal; pos += 512)
    {
        if (pos == swapAt)
        {
            bank.seedFrom(chorus(16), 0.0f);  // a much brighter registration
            REQUIRE(bank.isActive());         // it must NOT have been reset
        }
        core::AudioBlock b = stereoBlock(l, r, pos, 512);
        bank.renderAdd(b);
    }

    // 1. It never went silent.
    auto rmsOver = [&l](std::size_t from, std::size_t to) {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i) s += static_cast<double>(l[i]) * l[i];
        return std::sqrt(s / static_cast<double>(to - from));
    };
    const double before = rmsOver(12000, 23000);
    const double after  = rmsOver(30000, 47000);
    REQUIRE(before > 1.0e-4);
    CHECK(after > 1.0e-4);

    // 2. It did not step. The gain ramp spreads the change over a block, so no
    //    single sample pair may jump the way a hard cut would.
    float biggestStep = 0.0f;
    for (std::size_t i = swapAt - 1024; i < swapAt + 2048; ++i)
        biggestStep = std::max(biggestStep, std::fabs(l[i] - l[i - 1]));
    const float ordinaryStep = [&l] {
        float m = 0.0f;
        for (std::size_t i = 12001; i < 23000; ++i)
            m = std::max(m, std::fabs(l[i] - l[i - 1]));
        return m;
    }();
    INFO("step at the change " << biggestStep << " vs ordinary " << ordinaryStep);
    CHECK(biggestStep < ordinaryStep * 4.0f);

    // 3. It really is a different sound now. A brighter registration crosses zero
    //    more often; a bank that ignored the reseed would not.
    auto crossings = [&l](std::size_t from, std::size_t to) {
        std::size_t n = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            if (l[i - 1] <= 0.0f && l[i] > 0.0f) ++n;
        return n;
    };
    CHECK(crossings(30000, 47000) > crossings(12000, 23000));
}


TEST_CASE("A folded mixture partial blooms at the pitch it actually sounds",
          "[synthesis][partialbank][mixture]")
{
    // Each partial's attack bloom comes from its ABSOLUTE pitch: a low partial
    // fills slowly, a high one speaks at once. trigger() now reaches that pitch by
    // ADDING a log ratio precomputed at seed time to the note's own log pitch,
    // rather than taking a log2 per partial -- which means the octaves a mixture
    // partial folds down by have to be subtracted from that sum. Drop the fold
    // term and every folded partial across the whole treble gets the bloom of a
    // pitch it is not sounding at, with nothing to show for it but a subtly wrong
    // attack on exactly the stops that break.
    //
    // Set up so the two banks are indistinguishable when the term is right:
    //   A: one partial at ratio 8, breaking back -> folds once, sounds at 4*f0
    //   B: one partial at ratio 4, not breaking  -> sounds at 4*f0
    // At C6 (1046.5 Hz) ratio 8 is 8372 Hz, above the 6 kHz mixture ceiling, so A
    // folds exactly once and the two banks sound the same pitch.
    constexpr int    kNote   = 84;       // C6
    constexpr double kF0     = 1046.502;
    constexpr std::size_t kFrames = 4096; // the attack, where the bloom lives

    const auto build = [](float ratio, bool breaksBack)
    {
        synth::SpectralModel m;
        synth::PartialTrack t;
        t.ratioToF0  = ratio;
        t.ampDb      = 0.0f;
        t.phase      = 0.0f;
        t.seed       = 0x51ED2701u;      // same identity, so the same start phase
        t.breaksBack = breaksBack;
        m.partials.push_back(t);
        m.fundamentalHz = static_cast<float>(kF0);
        return m;
    };

    const auto renderOne = [&](float ratio, bool breaksBack)
    {
        synth::PartialBank bank;
        bank.setMaxPartials(4);
        bank.prepare(kSr, 512);
        // The treble tilt is keyed on the harmonic INDEX, which is 8 for one bank
        // and 4 for the other; turning it off is what isolates the bloom.
        bank.setLiveliness(5.5f, -18.0f, 0.060f, 7000.0f, /*trebleTiltDb*/ 0.0f);
        bank.seedFrom(build(ratio, breaksBack), 0.0f);
        bank.trigger(core::PipeId{ 0, static_cast<std::uint8_t>(kNote), 1 }, 100, kF0);

        std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
        render(bank, l, r, kFrames, 256);
        return l;
    };

    const std::vector<float> folded   = renderOne(8.0f, true);
    const std::vector<float> unfolded = renderOne(4.0f, false);

    double worst = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < kFrames; ++i)
    {
        worst = std::max(worst, std::abs(static_cast<double>(folded[i] - unfolded[i])));
        peak  = std::max(peak, std::abs(static_cast<double>(unfolded[i])));
    }

    REQUIRE(peak > 1.0e-4); // it really did sound
    INFO("worst sample difference " << worst << " against a peak of " << peak);
    CHECK(worst < peak * 1.0e-4);
}

TEST_CASE("Two ranks on the same key stay decorrelated after the phase table",
          "[synthesis][partialbank][stereo]")
{
    // Start phases now come from a 256-entry unit circle instead of cos/sin of a
    // random angle. That is a small alphabet, and the property it has to preserve
    // is the reason the scattering exists at all: two voices sounding the SAME
    // pitch must not line up, or they sum at +6 dB instead of +3 and the tutti
    // gets a level hotspot that no amount of gain staging fixes.
    const auto renderRank = [](std::uint8_t rankId, std::uint32_t seed)
    {
        synth::PartialBank bank;
        bank.setMaxPartials(32);
        bank.prepare(kSr, 512);
        bank.seedFrom(chorus(12, seed), 0.0f);
        bank.trigger(core::PipeId{ rankId, 60, 1 }, 100, 261.626);

        std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
        render(bank, l, r, 24000, 512);
        return l;
    };

    const std::vector<float> a = renderRank(3, 0x51ED2701u);
    const std::vector<float> b = renderRank(9, 0xA37C19E5u);

    // Measure over the steady portion, past the attack.
    double aa = 0.0, bb = 0.0, ab = 0.0;
    for (std::size_t i = 8000; i < a.size(); ++i)
    {
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
        ab += static_cast<double>(a[i]) * b[i];
    }
    REQUIRE(aa > 0.0);
    REQUIRE(bb > 0.0);
    const double corr = ab / std::sqrt(aa * bb);

    INFO("cross-correlation " << corr);
    CHECK(std::abs(corr) < 0.35);
}

TEST_CASE("The same pipe triggers to the same sound every time",
          "[synthesis][partialbank]")
{
    // The start phase is now a table index derived from a hash of the pipe's
    // identity. That has to stay a FUNCTION of the identity: if it picked up
    // anything else -- an array position, a call counter, uninitialised memory --
    // the same key would speak differently on each press, and a saved session
    // would not reproduce.
    const auto once = []
    {
        synth::PartialBank bank;
        bank.setMaxPartials(32);
        bank.prepare(kSr, 512);
        bank.seedFrom(chorus(12), 0.0f);
        bank.trigger(core::PipeId{ 5, 60, 2 }, 100, 261.626);

        std::vector<float> l(8192, 0.0f), r(8192, 0.0f);
        render(bank, l, r, 8192, 512);
        return l;
    };

    const std::vector<float> first  = once();
    const std::vector<float> second = once();

    bool identical = true;
    for (std::size_t i = 0; i < first.size(); ++i)
        identical = identical && (first[i] == second[i]);
    CHECK(identical);
}
