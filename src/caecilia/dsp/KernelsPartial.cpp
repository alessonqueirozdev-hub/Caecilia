// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/Kernels.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
  #define CAECILIA_X86  1
  #define CAECILIA_NEON 0
  #include <emmintrin.h> // SSE2 -- baseline on x86-64, no probe and no compile flag
  #if defined(_MSC_VER)
    #include <intrin.h>  // __cpuidex / _xgetbv, for the AVX2 probe
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define CAECILIA_X86  0
  #define CAECILIA_NEON 1
  #include <arm_neon.h>  // baseline on aarch64: no probe, no flag, no AVX2 hazard
#else
  #define CAECILIA_X86  0
  #define CAECILIA_NEON 0
#endif

// ---------------------------------------------------------------------------
// The partial oscillator, scalar reference and SSE2.
//
// This is the loop the whole instrument's cost lives in: one recursive quadrature
// oscillator per partial per voice, and a ten-note Tutti is 260 voices.
//
// The vectorisation is over FRAMES, not over partials. Vectorising across partials
// looks natural -- they are independent -- but every frame would then need a
// horizontal reduction to sum them, and a horizontal add is the one thing SIMD is
// bad at. Across frames there is no reduction at all: four lanes hold the same
// partial at four consecutive sample offsets, each steps by the FOURTH power of the
// per-sample rotation, and they accumulate straight into four adjacent output
// slots.
// ---------------------------------------------------------------------------

namespace caecilia::dsp::kernels
{
#if CAECILIA_X86
/// Defined in KernelsAvx2.cpp, which is the only unit compiled with /arch:AVX2.
void partialAccumulateAvx2(float* dstL, float* dstR, std::size_t count,
                           PhasorState& osc, float cosInc, float sinInc,
                           float& gain, float gainInc,
                           float panL, float panR) noexcept;
#endif

namespace
{
#if CAECILIA_X86
/// Does this CPU -- and this OS -- actually support AVX2?
///
/// Both halves matter. A Haswell running an OS that never enabled YMM state saving
/// faults on the first AVX instruction, so the CPU feature bit alone is not enough:
/// OSXSAVE has to be set AND XCR0 has to say the OS is preserving the upper halves.
///
/// This lives HERE, in the translation unit compiled WITHOUT /arch:AVX2. A probe
/// compiled with the flag it is probing for is a probe that can fault before it
/// answers.
bool cpuHasAvx2() noexcept
{
  #if defined(_MSC_VER)
    int regs[4] = { 0, 0, 0, 0 };

    __cpuid(regs, 0);
    if (regs[0] < 7)
        return false;

    __cpuid(regs, 1);
    const bool osxsave = (regs[2] & (1 << 27)) != 0;
    const bool avx     = (regs[2] & (1 << 28)) != 0;
    if (! osxsave || ! avx)
        return false;

    // XCR0 bits 1 (SSE) and 2 (AVX): the OS saves and restores the YMM state.
    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6ull) != 0x6ull)
        return false;

    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0; // EBX bit 5 == AVX2
  #elif defined(__GNUC__) || defined(__clang__)
    // GCC and Clang's builtin checks the OS support as well as the CPU bits.
    return __builtin_cpu_supports("avx2");
  #else
    return false;
  #endif
}

Backend detectBest() noexcept
{
    return cpuHasAvx2() ? Backend::Avx2 : Backend::Sse2;
}

/// The four-wide path on this architecture, whatever it is called here.
constexpr Backend kFourWide = Backend::Sse2;

#elif CAECILIA_NEON

Backend detectBest() noexcept { return Backend::Neon; }
constexpr Backend kFourWide = Backend::Neon;

#else

Backend detectBest() noexcept { return Backend::Scalar; }
constexpr Backend kFourWide = Backend::Scalar;

#endif

/// Probed once, at static-init time, off any audio thread.
///
/// Initialised to the best available, NOT to Scalar: a dispatch that has to be
/// switched on before it does anything is a dispatch that ships switched off.
const Backend kBest = detectBest();
Backend g_backend = kBest;

/// Advance a phasor by one sample.
inline void rotate(float& x, float& y, float c, float s) noexcept
{
    const float nx = x * c - y * s;
    const float ny = x * s + y * c;
    x = nx;
    y = ny;
}

void partialAccumulateScalar(float* dstL, float* dstR, std::size_t count,
                             PhasorState& osc, float cosInc, float sinInc,
                             float& gain, float gainInc,
                             float panL, float panR) noexcept
{
    float x = osc.x;
    float y = osc.y;
    float g = gain;

    for (std::size_t n = 0; n < count; ++n)
    {
        g += gainInc;
        const float v = g * y;   // y BEFORE the advance: that is the n-th sample
        dstL[n] += v * panL;
        dstR[n] += v * panR;
        rotate(x, y, cosInc, sinInc);
    }

    osc.x = x;
    osc.y = y;
    gain  = g;
}

#if CAECILIA_X86
void partialAccumulateSse2(float* dstL, float* dstR, std::size_t count,
                           PhasorState& osc, float cosInc, float sinInc,
                           float& gain, float gainInc,
                           float panL, float panR) noexcept
{
    float x = osc.x;
    float y = osc.y;
    float g = gain;

    const std::size_t vec = count & ~std::size_t{ 3 };
    if (vec > 0)
    {
        // Four lanes at consecutive sample offsets. Seeded by rotating, not by
        // cos/sin of k*w: the scalar path's phasor is a chain of FLOAT rotations,
        // and reproducing it means rotating the same way rather than computing what
        // the angle should have been.
        alignas(16) float sx[4];
        alignas(16) float sy[4];
        float lx = x;
        float ly = y;
        for (int k = 0; k < 4; ++k)
        {
            sx[k] = lx;
            sy[k] = ly;
            rotate(lx, ly, cosInc, sinInc);
        }

        // The four-sample rotation, by two complex squarings of the one-sample
        // rotation. NOT cosf(4*w): three float squarings stay consistent with the
        // float-rounded (c, s) the lanes were actually seeded from, and measured 13
        // dB closer over a four-second render.
        const float c2 = cosInc * cosInc - sinInc * sinInc;
        const float s2 = 2.0f * cosInc * sinInc;
        const float c4 = c2 * c2 - s2 * s2;
        const float s4 = 2.0f * c2 * s2;

        __m128 vx = _mm_load_ps(sx);
        __m128 vy = _mm_load_ps(sy);

        // Lane k carries gain g + (k+1)*inc, and every iteration adds 4*inc.
        alignas(16) float sg[4] = { g + gainInc, g + 2.0f * gainInc,
                                    g + 3.0f * gainInc, g + 4.0f * gainInc };
        __m128       vg     = _mm_load_ps(sg);
        const __m128 vgStep = _mm_set1_ps(4.0f * gainInc);

        const __m128 vc4 = _mm_set1_ps(c4);
        const __m128 vs4 = _mm_set1_ps(s4);
        const __m128 vpl = _mm_set1_ps(panL);
        const __m128 vpr = _mm_set1_ps(panR);

        for (std::size_t n = 0; n < vec; n += 4)
        {
            const __m128 v = _mm_mul_ps(vg, vy);

            _mm_storeu_ps(dstL + n, _mm_add_ps(_mm_loadu_ps(dstL + n), _mm_mul_ps(v, vpl)));
            _mm_storeu_ps(dstR + n, _mm_add_ps(_mm_loadu_ps(dstR + n), _mm_mul_ps(v, vpr)));

            const __m128 nx = _mm_sub_ps(_mm_mul_ps(vx, vc4), _mm_mul_ps(vy, vs4));
            const __m128 ny = _mm_add_ps(_mm_mul_ps(vx, vs4), _mm_mul_ps(vy, vc4));
            vx = nx;
            vy = ny;
            vg = _mm_add_ps(vg, vgStep);
        }

        // Lane 0 now holds the phasor advanced by exactly `vec` samples, which is
        // where the scalar tail has to pick up.
        _mm_store_ps(sx, vx);
        _mm_store_ps(sy, vy);
        x = sx[0];
        y = sy[0];
        g += static_cast<float>(vec) * gainInc;
    }

    for (std::size_t n = vec; n < count; ++n)
    {
        g += gainInc;
        const float v = g * y;
        dstL[n] += v * panL;
        dstR[n] += v * panR;
        rotate(x, y, cosInc, sinInc);
    }

    osc.x = x;
    osc.y = y;
    gain  = g;
}
#endif // CAECILIA_X86

#if CAECILIA_NEON
/// The four-wide path on aarch64.
///
/// NEON is architectural baseline there, exactly as SSE2 is on x86-64: no CPU
/// probe, no per-file compile flag, and therefore none of the /arch:AVX2 hazard
/// that KernelsAvx2.cpp has to defend against.
///
/// Written without a machine to run it on, which is worth saying rather than
/// hiding: it is the four-wide algorithm already proven here, translated intrinsic
/// for intrinsic, and CI runs the whole Catch2 suite on macos-latest -- which is
/// Apple Silicon. PartialOscillatorTest's equivalence cases are what actually
/// check it, and they run on the first push. Without this path an M-series Mac
/// renders through the scalar reference at about a third of the speed the machine
/// is capable of, and nothing anywhere says so.
void partialAccumulateNeon(float* dstL, float* dstR, std::size_t count,
                           PhasorState& osc, float cosInc, float sinInc,
                           float& gain, float gainInc,
                           float panL, float panR) noexcept
{
    float x = osc.x;
    float y = osc.y;
    float g = gain;

    const std::size_t vec = count & ~std::size_t{ 3 };
    if (vec > 0)
    {
        // Four lanes at consecutive sample offsets, seeded by ROTATING rather than
        // by cos/sin of k*w: the scalar reference's phasor is a chain of float
        // rotations, and matching it means rotating the same way.
        float sx[4];
        float sy[4];
        float sg[4];
        {
            float lx = x;
            float ly = y;
            for (int k = 0; k < 4; ++k)
            {
                sx[k] = lx;
                sy[k] = ly;
                sg[k] = g + static_cast<float>(k + 1) * gainInc;
                rotate(lx, ly, cosInc, sinInc);
            }
        }

        // The four-sample rotation, by two complex squarings of the one-sample one.
        // Not cosf(4*w): the squarings stay consistent with the float-rounded (c, s)
        // the lanes were seeded from.
        const float c2 = cosInc * cosInc - sinInc * sinInc;
        const float s2 = 2.0f * cosInc * sinInc;
        const float c4 = c2 * c2 - s2 * s2;
        const float s4 = 2.0f * c2 * s2;

        float32x4_t vx = vld1q_f32(sx);
        float32x4_t vy = vld1q_f32(sy);
        float32x4_t vg = vld1q_f32(sg);

        const float32x4_t vc4    = vdupq_n_f32(c4);
        const float32x4_t vs4    = vdupq_n_f32(s4);
        const float32x4_t vgStep = vdupq_n_f32(4.0f * gainInc);
        const float32x4_t vpl    = vdupq_n_f32(panL);
        const float32x4_t vpr    = vdupq_n_f32(panR);

        for (std::size_t n = 0; n < vec; n += 4)
        {
            const float32x4_t v = vmulq_f32(vg, vy);

            // Multiply and add as separate steps rather than vmlaq_f32, which on
            // aarch64 is FMLA -- a single rounding where the scalar reference and
            // the SSE2 path both round twice. The difference is far below the
            // equivalence tolerance either way, and a compiler is still free to
            // contract these; writing them apart just keeps the source saying what
            // the reference does.
            vst1q_f32(dstL + n, vaddq_f32(vld1q_f32(dstL + n), vmulq_f32(v, vpl)));
            vst1q_f32(dstR + n, vaddq_f32(vld1q_f32(dstR + n), vmulq_f32(v, vpr)));

            const float32x4_t nx = vsubq_f32(vmulq_f32(vx, vc4), vmulq_f32(vy, vs4));
            const float32x4_t ny = vaddq_f32(vmulq_f32(vx, vs4), vmulq_f32(vy, vc4));
            vx = nx;
            vy = ny;
            vg = vaddq_f32(vg, vgStep);
        }

        // Lane 0 holds the phasor advanced by exactly `vec` samples: where the
        // scalar tail picks up.
        x = vgetq_lane_f32(vx, 0);
        y = vgetq_lane_f32(vy, 0);
        g += static_cast<float>(vec) * gainInc;
    }

    for (std::size_t n = vec; n < count; ++n)
    {
        g += gainInc;
        const float v = g * y;
        dstL[n] += v * panL;
        dstR[n] += v * panR;
        rotate(x, y, cosInc, sinInc);
    }

    osc.x = x;
    osc.y = y;
    gain  = g;
}
#endif // CAECILIA_NEON
} // namespace

Backend bestAvailableBackend() noexcept
{
    return kBest;
}

Backend selectBackend(Backend backend) noexcept
{
    // Asking for something this build or this CPU does not have gives the best
    // available rather than a crash -- a --backend flag is a measurement tool, not a
    // promise, and an AVX2 instruction on a machine without it is not a slow answer
    // but an illegal one.
    switch (backend)
    {
        case Backend::Scalar:
            break; // always there, by definition

        case Backend::Sse2:
        case Backend::Neon:
            // The same four-wide algorithm under two names. Asking for one on the
            // architecture that has the other gives the one that exists, which is
            // what makes `--backend sse2` a portable way to say "the four-wide path".
            backend = kFourWide;
            break;

        case Backend::Avx2:
            backend = (kBest == Backend::Avx2) ? Backend::Avx2 : kFourWide;
            break;
    }

    g_backend = backend;
    return g_backend;
}

Backend activeBackend() noexcept
{
    return g_backend;
}

void partialAccumulate(float* dstL, float* dstR, std::size_t count,
                       PhasorState& osc, float cosInc, float sinInc,
                       float& gain, float gainInc,
                       float panL, float panR) noexcept
{
#if CAECILIA_X86
    // Below about six dozen frames the eight-lane seed prologue -- eight rotations
    // and three complex squarings before a single sample is written -- stops
    // amortising, and the four-lane path wins. A host running 32-frame buffers is
    // unusual but real, and it is exactly the host with the least headroom to spare.
    if (g_backend == Backend::Avx2 && count >= 48)
    {
        partialAccumulateAvx2(dstL, dstR, count, osc, cosInc, sinInc,
                              gain, gainInc, panL, panR);
        return;
    }
    if (g_backend != Backend::Scalar)
    {
        partialAccumulateSse2(dstL, dstR, count, osc, cosInc, sinInc,
                              gain, gainInc, panL, panR);
        return;
    }
#elif CAECILIA_NEON
    if (g_backend != Backend::Scalar)
    {
        partialAccumulateNeon(dstL, dstR, count, osc, cosInc, sinInc,
                              gain, gainInc, panL, panR);
        return;
    }
#endif
    partialAccumulateScalar(dstL, dstR, count, osc, cosInc, sinInc,
                            gain, gainInc, panL, panR);
}

} // namespace caecilia::dsp::kernels
