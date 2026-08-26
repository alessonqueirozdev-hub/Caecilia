// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

// ---------------------------------------------------------------------------
// AVX2 partial oscillator. ONE function, and nothing else in this file.
//
// This translation unit is compiled with /arch:AVX2 (or -mavx2), and that flag
// applies to the WHOLE unit, not to the function that wants it. So anything else
// that lands here -- an inline function from a header, a std::vector's growth path,
// a <cmath> helper -- is compiled with AVX2 instructions too, and the linker's
// COMDAT folding is free to splice that copy into a caller that never ran the CPU
// check. The result is an illegal-instruction crash on a pre-Haswell machine, in
// code that looks entirely unrelated, and it does not reproduce on the developer's
// machine.
//
// So: only <immintrin.h> and the kernel header, which declares functions and a
// two-float POD and instantiates nothing. No std container. No <cmath>. No
// <intrin.h> -- the CPUID probe lives in KernelsPartial.cpp, which is compiled
// without the flag.
//
// Verify with:  dumpbin /disasm caecilia_core.lib | findstr "ymm"
// Every ymm reference must be inside partialAccumulateAvx2.
// ---------------------------------------------------------------------------

#include "caecilia/dsp/Kernels.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)

#include <immintrin.h>

namespace caecilia::dsp::kernels
{

void partialAccumulateAvx2(float* dstL, float* dstR, std::size_t count,
                           PhasorState& osc, float cosInc, float sinInc,
                           float& gain, float gainInc,
                           float panL, float panR) noexcept
{
    float x = osc.x;
    float y = osc.y;
    float g = gain;

    const std::size_t vec = count & ~std::size_t{ 7 };
    if (vec > 0)
    {
        // Eight lanes at consecutive sample offsets, seeded by rotating rather than
        // by cos/sin of k*w: the scalar reference's phasor is a chain of FLOAT
        // rotations, and matching it means rotating the same way.
        alignas(32) float sx[8];
        alignas(32) float sy[8];
        alignas(32) float sg[8];
        {
            float lx = x;
            float ly = y;
            for (int k = 0; k < 8; ++k)
            {
                sx[k] = lx;
                sy[k] = ly;
                sg[k] = g + static_cast<float>(k + 1) * gainInc;
                const float nx = lx * cosInc - ly * sinInc;
                const float ny = lx * sinInc + ly * cosInc;
                lx = nx;
                ly = ny;
            }
        }

        // The eight-sample rotation, by THREE complex squarings of the one-sample
        // rotation. Not cosf(8*w): three float squarings stay consistent with the
        // float-rounded (c, s) the lanes were seeded from, and measured 13 dB closer
        // over a four-second render.
        const float c2 = cosInc * cosInc - sinInc * sinInc;
        const float s2 = 2.0f * cosInc * sinInc;
        const float c4 = c2 * c2 - s2 * s2;
        const float s4 = 2.0f * c2 * s2;
        const float c8 = c4 * c4 - s4 * s4;
        const float s8 = 2.0f * c4 * s4;

        __m256 vx = _mm256_load_ps(sx);
        __m256 vy = _mm256_load_ps(sy);
        __m256 vg = _mm256_load_ps(sg);

        const __m256 vc8    = _mm256_set1_ps(c8);
        const __m256 vs8    = _mm256_set1_ps(s8);
        const __m256 vgStep = _mm256_set1_ps(8.0f * gainInc);
        const __m256 vpl    = _mm256_set1_ps(panL);
        const __m256 vpr    = _mm256_set1_ps(panR);

        for (std::size_t n = 0; n < vec; n += 8)
        {
            const __m256 v = _mm256_mul_ps(vg, vy);

            _mm256_storeu_ps(dstL + n,
                             _mm256_add_ps(_mm256_loadu_ps(dstL + n), _mm256_mul_ps(v, vpl)));
            _mm256_storeu_ps(dstR + n,
                             _mm256_add_ps(_mm256_loadu_ps(dstR + n), _mm256_mul_ps(v, vpr)));

            const __m256 nx = _mm256_sub_ps(_mm256_mul_ps(vx, vc8), _mm256_mul_ps(vy, vs8));
            const __m256 ny = _mm256_add_ps(_mm256_mul_ps(vx, vs8), _mm256_mul_ps(vy, vc8));
            vx = nx;
            vy = ny;
            vg = _mm256_add_ps(vg, vgStep);
        }

        // Lane 0 holds the phasor advanced by exactly `vec` samples: where the
        // scalar tail picks up.
        _mm256_store_ps(sx, vx);
        _mm256_store_ps(sy, vy);
        x = sx[0];
        y = sy[0];
        g += static_cast<float>(vec) * gainInc;

        // AVX and SSE code in the same call chain costs a state-transition penalty
        // on some microarchitectures unless the upper halves are explicitly cleared.
        // The scalar tail below is SSE.
        _mm256_zeroupper();
    }

    for (std::size_t n = vec; n < count; ++n)
    {
        g += gainInc;
        const float v = g * y;
        dstL[n] += v * panL;
        dstR[n] += v * panR;
        const float nx = x * cosInc - y * sinInc;
        const float ny = x * sinInc + y * cosInc;
        x = nx;
        y = ny;
    }

    osc.x = x;
    osc.y = y;
    gain  = g;
}

} // namespace caecilia::dsp::kernels

#endif // x86
