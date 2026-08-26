// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/dsp/Kernels.h"

// ---------------------------------------------------------------------------
// Portable scalar REFERENCE implementations.
//
// These four are not on the critical path -- the instrument's cost is the partial
// oscillator, which has its backends in KernelsPartial.cpp and KernelsAvx2.cpp.
// Measure before vectorising these too: the bus mixer moves a few hundred floats
// per block where the oscillator runs hundreds of banks.
// ---------------------------------------------------------------------------

namespace caecilia::dsp::kernels
{

void accumulate(float* dst, const float* src, std::size_t count, float gain) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        dst[i] += src[i] * gain;
}

float dotProduct(const float* a, const float* b, std::size_t count) noexcept
{
    float acc = 0.0f;
    for (std::size_t i = 0; i < count; ++i)
        acc += a[i] * b[i];
    return acc;
}

void biquadBlock(float*      data,
                 std::size_t count,
                 float       b0,
                 float       b1,
                 float       b2,
                 float       a1,
                 float       a2,
                 float&      z1,
                 float&      z2) noexcept
{
    float s1 = z1;
    float s2 = z2;
    for (std::size_t i = 0; i < count; ++i)
    {
        const float x = data[i];
        const float y = b0 * x + s1;
        s1            = b1 * x - a1 * y + s2;
        s2            = b2 * x - a2 * y;
        data[i]       = y;
    }
    z1 = s1;
    z2 = s2;
}

void hadamard16(float* v) noexcept
{
    // Radix-2 in-place fast Walsh-Hadamard transform over 16 elements
    // (4 butterfly stages), then normalise by 1/sqrt(16) to keep it unitary.
    for (std::size_t stride = 1; stride < 16; stride <<= 1)
    {
        for (std::size_t base = 0; base < 16; base += (stride << 1))
        {
            for (std::size_t i = base; i < base + stride; ++i)
            {
                const float a = v[i];
                const float b = v[i + stride];
                v[i]          = a + b;
                v[i + stride] = a - b;
            }
        }
    }
    constexpr float kNorm = 0.25f; // 1 / sqrt(16)
    for (std::size_t i = 0; i < 16; ++i)
        v[i] *= kNorm;
}

} // namespace caecilia::dsp::kernels
