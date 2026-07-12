/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

/**
 * @file DspMath.h
 * @brief Header-only, allocation-free scalar math helpers shared across the DSP
 *        module: constants, a normalised sinc, the modified Bessel function used
 *        by the Kaiser window, decibel conversions and a denormal flusher.
 *
 * Everything here is written fresh from standard public-domain DSP mathematics
 * (Kaiser-window / windowed-sinc theory). No GPL source is referenced.
 *
 * Real-time contract: all functions are @c noexcept, branch-light and never
 * allocate, so they are safe to call from process()/render() hot paths. Where a
 * routine is only used at prepare()-time (e.g. @ref besselI0 while building a
 * coefficient table) it is documented as such.
 */

namespace caecilia::dsp
{

/// Mathematical constants at float and double precision.
inline constexpr double kPi     = 3.14159265358979323846;
inline constexpr double kTwoPi  = 2.0 * kPi;
inline constexpr double kHalfPi = 0.5 * kPi;
inline constexpr float  kPiF    = static_cast<float>(kPi);
inline constexpr float  kTwoPiF = static_cast<float>(kTwoPi);

/// Smallest magnitude a feedback sample may keep before we treat it as zero.
inline constexpr float kDenormalFloor = 1.0e-20f;

/**
 * @brief Flush a subnormal float to exactly zero.
 *
 * Feedback paths (FDN lines, reverb tails, IIR memory) can decay into denormal
 * numbers that stall the FPU on some CPUs. Combined with per-thread FTZ/DAZ this
 * keeps the tail cheap and deterministic. RT-safe, @c noexcept.
 */
[[nodiscard]] inline float flushDenormal(float x) noexcept
{
    return (x < kDenormalFloor && x > -kDenormalFloor) ? 0.0f : x;
}

/// Convert a decibel value to a linear gain. RT-safe.
[[nodiscard]] inline float dbToGain(float dB) noexcept
{
    return std::pow(10.0f, dB * 0.05f);
}

/// Convert a linear gain to decibels (floored for silence). RT-safe.
[[nodiscard]] inline float gainToDb(float gain) noexcept
{
    return 20.0f * std::log10(gain > 1.0e-9f ? gain : 1.0e-9f);
}

/// Clamp @p v to [@p lo, @p hi]. RT-safe.
template <typename T>
[[nodiscard]] constexpr T clamp(T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * @brief Normalised cardinal sine, sinc(x) = sin(pi x) / (pi x), with sinc(0)=1.
 *
 * This is the ideal (brick-wall) interpolation kernel that the Kaiser window
 * tapers into a finite 16-tap filter. RT-safe, @c noexcept.
 */
[[nodiscard]] inline double sinc(double x) noexcept
{
    if (x == 0.0)
        return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

/**
 * @brief Modified Bessel function of the first kind, order zero, I0(x).
 *
 * Evaluated by its convergent power series
 *   I0(x) = sum_{k>=0} ( (x/2)^k / k! )^2.
 * Used to build the Kaiser window at prepare()-time; the series terminates once
 * the incremental term is negligible, so it is bounded and allocation-free.
 *
 * @note Intended for table construction (prepare()), not the per-sample hot path.
 */
[[nodiscard]] inline double besselI0(double x) noexcept
{
    const double halfX = x * 0.5;
    double       term  = 1.0; // k = 0 term of (x/2)^k / k!
    double       sum   = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= halfX / static_cast<double>(k);
        const double termSq = term * term;
        sum += termSq;
        if (termSq < 1.0e-18 * sum)
            break;
    }
    return sum;
}

/**
 * @brief Kaiser window weight at normalised position @p n in [-1, 1].
 * @param n    Normalised tap position, -1 and +1 at the window edges.
 * @param beta Kaiser shape parameter (larger = more sidelobe suppression).
 * @return Window value in [0, 1]; 0 outside |n| > 1.
 *
 * @note prepare()-time helper (calls @ref besselI0).
 */
[[nodiscard]] inline double kaiserWindow(double n, double beta) noexcept
{
    if (n < -1.0 || n > 1.0)
        return 0.0;
    const double arg = beta * std::sqrt(1.0 - n * n);
    return besselI0(arg) / besselI0(beta);
}

/**
 * @brief Equal-power pan gains for a source at normalised pan @p panNorm.
 * @param panNorm  -1 = hard left, 0 = centre, +1 = hard right.
 * @param outLeft  Receives the left-channel gain.
 * @param outRight Receives the right-channel gain.
 *
 * Uses the sin/cos constant-power law so a centred source is -3 dB per side.
 * RT-safe, @c noexcept.
 */
inline void equalPowerPan(float panNorm, float& outLeft, float& outRight) noexcept
{
    const double theta = (static_cast<double>(clamp(panNorm, -1.0f, 1.0f)) + 1.0) * 0.25 * kPi;
    outLeft  = static_cast<float>(std::cos(theta));
    outRight = static_cast<float>(std::sin(theta));
}

} // namespace caecilia::dsp
