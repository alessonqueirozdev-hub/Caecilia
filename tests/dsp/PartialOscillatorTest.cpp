// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The SIMD partial oscillator against its scalar reference.
//
// Written and passing BEFORE PartialBank is restructured to use it, so the kernel
// is known good on its own: if the two land in one commit and the instrument comes
// out wrong, there is no way to tell the kernel from the plumbing.
//
// What it does NOT assert is sample equality. The vector path advances the phasor
// in strides of four, so the rounding trajectories separate — and they must, since
// four rotations by the fourth power of a float rotation is not the same arithmetic
// as four successive rotations by it. The realised FREQUENCY is identical, which is
// the thing a listener could hear; the difference is a very small level error that
// grows logarithmically, and a REQUIRE(a[n] == b[n]) over a long render would fail
// for no musical reason and be blamed on a bug.
//

#include "caecilia/dsp/Kernels.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace kernels = caecilia::dsp::kernels;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

struct Render
{
    std::vector<float>    l, r;
    kernels::PhasorState  osc;
    float                 gain = 0.0f;
};

/// Run one partial for @p count samples through whichever backend is active.
Render run(kernels::Backend backend, double freqHz, double sampleRate,
           std::size_t count, float gain0, float gainInc)
{
    kernels::selectBackend(backend);

    Render out;
    out.l.assign(count, 0.0f);
    out.r.assign(count, 0.0f);
    out.osc  = kernels::PhasorState{ 1.0f, 0.0f };
    out.gain = gain0;

    const auto w = static_cast<float>(kTwoPi * freqHz / sampleRate);
    kernels::partialAccumulate(out.l.data(), out.r.data(), count, out.osc,
                               std::cos(w), std::sin(w), out.gain, gainInc,
                               0.7071068f, 0.7071068f);
    return out;
}

/// The vector backends worth comparing on THIS machine.
///
/// Derived from what the CPU reports rather than assumed: a fixed list would either
/// skip AVX2 everywhere it exists, or run it on a machine where it faults.
std::vector<kernels::Backend> vectorBackends()
{
    // Ask for each by name and keep the ones that answer to it. A request the build
    // cannot honour comes back as whatever DOES exist, so this lists exactly the
    // distinct implementations on this machine -- Sse2 and Avx2 on x86, Neon on
    // aarch64 -- without the test needing to know which architecture it is on.
    std::vector<kernels::Backend> out;
    for (const kernels::Backend candidate : { kernels::Backend::Sse2,
                                              kernels::Backend::Neon,
                                              kernels::Backend::Avx2 })
        if (kernels::selectBackend(candidate) == candidate)
            out.push_back(candidate);

    kernels::selectBackend(kernels::bestAvailableBackend());
    return out;
}

const char* nameOf(kernels::Backend b)
{
    switch (b)
    {
        case kernels::Backend::Scalar: return "scalar";
        case kernels::Backend::Sse2:   return "sse2";
        case kernels::Backend::Neon:   return "neon";
        case kernels::Backend::Avx2:   return "avx2";
    }
    return "?";
}

/// Peak absolute difference, in dB relative to the reference's own peak.
double differenceDb(const std::vector<float>& a, const std::vector<float>& b)
{
    double peak = 0.0;
    double diff = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        peak = std::max(peak, std::abs(static_cast<double>(a[i])));
        diff = std::max(diff, std::abs(static_cast<double>(a[i] - b[i])));
    }
    if (peak <= 0.0)
        return -1000.0;
    return 20.0 * std::log10(std::max(diff, 1.0e-30) / peak);
}
} // namespace

TEST_CASE("A backend that is not there falls back rather than crashing",
          "[dsp][kernels][simd]")
{
    // --backend is a measurement tool. Asking for AVX2 on a machine without it, or
    // in a build without it, has to give the best available -- not undefined
    // instructions.
    const kernels::Backend best = kernels::bestAvailableBackend();

    CHECK(kernels::selectBackend(kernels::Backend::Scalar) == kernels::Backend::Scalar);
    CHECK(kernels::selectBackend(kernels::Backend::Avx2) == best);

    // Sse2 and Neon are the same four-wide algorithm on two architectures, so
    // asking for either has to give whichever one this build actually has. That is
    // what lets `--backend sse2` mean "the four-wide path" on an Apple Silicon Mac.
    const kernels::Backend four = kernels::selectBackend(kernels::Backend::Sse2);
    CHECK(four == kernels::selectBackend(kernels::Backend::Neon));
    CHECK((four != kernels::Backend::Scalar || best == kernels::Backend::Scalar));

    kernels::selectBackend(best);
    CHECK(kernels::activeBackend() == best);

    // On x86 the best available must NOT be the scalar reference. SSE2 is
    // architectural baseline there, so scalar means the probe broke or the vector
    // units were never compiled -- and either one ships an instrument running at a
    // third of its speed with nothing failing anywhere.
    //
    // (That the DEFAULT equals the best is true by construction -- one initialiser,
    // one constant -- and is only observable at process start, before Catch2 runs.
    // It is not a thing a test in this process can see, so it is not asserted here
    // and pretending otherwise would be a decorative check.)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    INFO("best available backend on this x86 machine");
    CHECK(best != kernels::Backend::Scalar);
#endif
}

TEST_CASE("The vector oscillator matches the scalar reference", "[dsp][kernels][simd]")
{
    constexpr double kSr    = 48000.0;
    constexpr std::size_t kN = 512;

    for (const kernels::Backend backend : vectorBackends())
    for (const double freq : { 55.0, 261.63, 1000.0, 4186.0, 12000.0 })
    {
        const Render a = run(kernels::Backend::Scalar, freq, kSr, kN, 0.0f, 1.0f / kN);
        const Render b = run(backend,                  freq, kSr, kN, 0.0f, 1.0f / kN);

        const double db = differenceDb(a.l, b.l);
        INFO(nameOf(backend) << " at " << freq << " Hz: worst difference " << db
             << " dB below peak");
        CHECK(db < -96.0); // inaudible by two orders of magnitude

        // Both channels, and the pan really is applied.
        CHECK(differenceDb(a.r, b.r) < -96.0);

        // The state handed back has to agree too, or the NEXT block starts from a
        // different place and the error compounds block over block.
        INFO("phasor scalar (" << a.osc.x << ", " << a.osc.y << ") vs vector ("
                               << b.osc.x << ", " << b.osc.y << ")");
        CHECK(static_cast<double>(b.osc.x) == Approx(a.osc.x).margin(1.0e-4));
        CHECK(static_cast<double>(b.osc.y) == Approx(a.osc.y).margin(1.0e-4));
        CHECK(static_cast<double>(b.gain)  == Approx(a.gain).margin(1.0e-5));
    }
}

TEST_CASE("The realised frequency is identical, not merely close",
          "[dsp][kernels][simd]")
{
    // The one invariant that has to be exact. A level error of -96 dB is nothing; a
    // frequency error is a detuned rank, and it would grow with every block.
    constexpr double kSr = 48000.0;
    constexpr std::size_t kN = 4096;
    constexpr double kFreq = 1000.0;

    for (const kernels::Backend backend : vectorBackends())
    {
    const Render a = run(kernels::Backend::Scalar, kFreq, kSr, kN, 1.0f, 0.0f);
    const Render b = run(backend,                  kFreq, kSr, kN, 1.0f, 0.0f);

    const auto phaseOf = [](const kernels::PhasorState& p)
    {
        return std::atan2(static_cast<double>(p.y), static_cast<double>(p.x));
    };

    const double pa = phaseOf(a.osc);
    const double pb = phaseOf(b.osc);

    // Stated in CENTS, not in radians. A radian tolerance is a number somebody
    // picked; cents is the claim -- how far out of tune the vector path would put a
    // rank against the scalar one.
    const double seconds   = static_cast<double>(kN) / kSr;
    const double deltaHz   = std::abs(pa - pb) / (kTwoPi * seconds);
    const double deltaCents = 1200.0 * std::log2((kFreq + deltaHz) / kFreq);

    INFO(nameOf(backend) << ": phase after " << kN << " samples: " << pa << " vs "
         << pb << " -> " << deltaHz << " Hz, " << deltaCents << " cents");

    // The instrument's own "living pipe" drift is tens of cents by design, and the
    // rank detune is a few. A thousandth of a cent is four orders below the
    // smallest thing anyone voiced on purpose.
    CHECK(deltaCents < 0.001);
    }
}

TEST_CASE("The two paths stay together over a long render", "[dsp][kernels][simd]")
{
    // The short-block agreement above would survive a subtly wrong four-sample
    // rotation: over 512 samples almost anything looks close. The error from a
    // wrong (c4, s4) grows with the render, so this is the case that catches it --
    // and it is why the four-sample rotation is three float squarings of (c, s)
    // rather than cosf(4*w), which measured 13 dB worse here.
    constexpr double kSr = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr int         kBlocks = 400; // ~4.3 s

    kernels::PhasorState oscA{ 1.0f, 0.0f }, oscB{ 1.0f, 0.0f };
    float gainA = 1.0f, gainB = 1.0f;
    const auto w = static_cast<float>(kTwoPi * 1000.0 / kSr);
    const float c = std::cos(w), s = std::sin(w);

    double worstDb = -1000.0;
    std::vector<float> la(kBlock), ra(kBlock), lb(kBlock), rb(kBlock);

    for (int b = 0; b < kBlocks; ++b)
    {
        std::fill(la.begin(), la.end(), 0.0f);
        std::fill(ra.begin(), ra.end(), 0.0f);
        std::fill(lb.begin(), lb.end(), 0.0f);
        std::fill(rb.begin(), rb.end(), 0.0f);

        kernels::selectBackend(kernels::Backend::Scalar);
        kernels::partialAccumulate(la.data(), ra.data(), kBlock, oscA, c, s,
                                   gainA, 0.0f, 1.0f, 1.0f);

        kernels::selectBackend(kernels::Backend::Sse2);
        kernels::partialAccumulate(lb.data(), rb.data(), kBlock, oscB, c, s,
                                   gainB, 0.0f, 1.0f, 1.0f);

        // Renormalise both, exactly as PartialBank does once per block. Without it
        // this measures float magnitude drift rather than the two paths diverging.
        for (kernels::PhasorState* p : { &oscA, &oscB })
        {
            const float mag = p->x * p->x + p->y * p->y;
            const float renorm = 1.5f - 0.5f * mag;
            p->x *= renorm;
            p->y *= renorm;
        }

        worstDb = std::max(worstDb, differenceDb(la, lb));
    }

    INFO("worst difference over " << (kBlocks * kBlock / kSr) << " s: " << worstDb << " dB");
    CHECK(worstDb < -60.0);

    kernels::selectBackend(kernels::bestAvailableBackend());
}

TEST_CASE("Counts that are not a multiple of four are still right",
          "[dsp][kernels][simd]")
{
    // The vector loop handles whole groups of four and a scalar tail. An off-by-one
    // there is silent: the block still sounds, one sample of it is wrong, and the
    // phasor handed back is a sample out -- which then compounds forever.
    constexpr double kSr = 48000.0;

    for (const kernels::Backend backend : vectorBackends())
        for (const std::size_t count : { std::size_t{ 1 }, std::size_t{ 4 }, std::size_t{ 7 },
                                         std::size_t{ 47 }, std::size_t{ 48 }, std::size_t{ 55 },
                                         std::size_t{ 63 }, std::size_t{ 65 } })
    {
        const Render a = run(kernels::Backend::Scalar, 440.0, kSr, count, 0.5f, 0.001f);
        const Render b = run(backend,                  440.0, kSr, count, 0.5f, 0.001f);

        INFO(nameOf(backend) << ", count " << count);
        for (std::size_t i = 0; i < count; ++i)
            CHECK(static_cast<double>(b.l[i]) == Approx(a.l[i]).margin(1.0e-6));

        CHECK(static_cast<double>(b.gain) == Approx(a.gain).margin(1.0e-6));
        CHECK(static_cast<double>(b.osc.y) == Approx(a.osc.y).margin(1.0e-5));
    }
}

TEST_CASE("The kernel accumulates rather than overwriting", "[dsp][kernels][simd]")
{
    // Every voice of the instrument adds into the same buffer. A kernel that stored
    // instead of accumulating would leave only the last partial of the last voice
    // audible -- and a single-voice test would never notice.
    constexpr std::size_t kN = 64;

    std::vector<kernels::Backend> all{ kernels::Backend::Scalar };
    for (const kernels::Backend b : vectorBackends())
        all.push_back(b);

    for (const kernels::Backend backend : all)
    {
        kernels::selectBackend(backend);
        INFO(nameOf(backend));

        std::vector<float> l(kN, 1.0f), r(kN, 1.0f);
        kernels::PhasorState osc{ 1.0f, 0.0f };
        float gain = 1.0f;
        const auto w = static_cast<float>(kTwoPi * 440.0 / 48000.0);

        kernels::partialAccumulate(l.data(), r.data(), kN, osc, std::cos(w), std::sin(w),
                                   gain, 0.0f, 1.0f, 1.0f);

        // Sample 0 is sin(0) == 0, so the pre-existing 1.0 must have survived intact.
        INFO("first sample after accumulating onto 1.0: " << l[0]);
        CHECK(static_cast<double>(l[0]) == Approx(1.0).margin(1.0e-6));

        bool moved = false;
        for (std::size_t i = 1; i < kN; ++i)
            moved = moved || std::abs(l[i] - 1.0f) > 1.0e-4f;
        CHECK(moved);
    }

    kernels::selectBackend(kernels::bestAvailableBackend());
}
