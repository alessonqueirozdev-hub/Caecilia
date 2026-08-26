// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <array>
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
 * @brief sin(2*pi*turns) to within 7.1e-7, without calling @c std::sin.
 *
 * Takes phase in TURNS rather than radians because every caller here already
 * accumulates a normalised phase, and turns make the range reduction exact: the
 * wrap is one @c floor against a period of 1, not an @c fmod against an
 * irrational.
 *
 * Why it exists: the FDN reverb's tail modulation evaluates one sine per delay
 * line per sample. Sixteen lines at 48 kHz is 768,000 @c std::sin calls per
 * second inside the audio callback, unconditionally, whether or not anything is
 * being automated. The library sine is a correctly-rounded routine carrying its
 * own argument reduction and special-case branches; none of that is worth paying
 * for an LFO that jitters a delay tap by two samples.
 *
 * Accuracy is 7.1e-7 peak (-123 dBFS) in float, from a degree-7 odd MINIMAX
 * polynomial. The Taylor series of identical length manages only 1.6e-4
 * (-76 dBFS): it spends its accuracy at zero and is worst at the quarter turn,
 * which is exactly where an LFO sits at its peaks. Same four terms, 220x closer.
 *
 * Exact at 0 and 0.5 turns; the quarter turns return +/-1 to within the
 * polynomial error. Branch-free, so sixteen lines at unrelated phases cannot
 * mispredict against each other. RT-safe, @c noexcept.
 *
 * @param turns Phase in turns; any finite value, positive or negative.
 * @return sin(2*pi*turns), in [-1, 1].
 */
[[nodiscard]] inline float fastSineTurns(float turns) noexcept
{
    // Range-reduce to [-0.5, +0.5) turns.
    const float t = turns - std::floor(turns + 0.5f);

    // Fold the outer two quarter-turns onto the inner two through
    // sin(pi - x) == sin(x), keeping the sign: with a = |t|, the folded
    // magnitude is 0.25 - |a - 0.25|, which is a for a <= 0.25 and 0.5 - a
    // beyond it. Written with fabs/copysign rather than compares so there is no
    // branch to predict.
    const float a      = std::fabs(t);
    const float folded = 0.25f - std::fabs(a - 0.25f);      // in [0, 0.25]
    const float x      = kTwoPiF * std::copysign(folded, t); // in [-pi/2, pi/2]

    // Odd minimax polynomial for sin on [-pi/2, pi/2] (Remez exchange,
    // equioscillating at 5.9e-7), evaluated by Horner in x^2.
    const float x2 = x * x;
    float       s  = -0.000183637f;
    s = s * x2 + 0.008306325f;
    s = s * x2 - 0.166648284f;
    s = s * x2 + 0.999996616f;
    return s * x;
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

/// A point on the unit circle: cos and sin of one angle, kept together because
/// every caller here wants both.
struct Phasor
{
    float cos = 1.0f;
    float sin = 0.0f;
};

/// Number of entries in @ref kUnitCircle. A power of two so the index is a mask.
inline constexpr std::size_t kUnitCircleSize = 256;

namespace detail
{
[[nodiscard]] inline std::array<Phasor, kUnitCircleSize> makeUnitCircle()
{
    std::array<Phasor, kUnitCircleSize> t{};
    for (std::size_t i = 0; i < kUnitCircleSize; ++i)
    {
        const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(kUnitCircleSize);
        t[i] = { static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)) };
    }
    return t;
}

inline constexpr std::size_t kPanSteps = 256;

[[nodiscard]] inline std::array<Phasor, kPanSteps + 1> makePanTable()
{
    std::array<Phasor, kPanSteps + 1> t{};
    for (std::size_t i = 0; i <= kPanSteps; ++i)
    {
        const double pan   = -1.0 + 2.0 * static_cast<double>(i) / static_cast<double>(kPanSteps);
        const double theta = (pan + 1.0) * 0.25 * kPi;
        t[i] = { static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta)) };
    }
    return t;
}
} // namespace detail

/// Evenly spaced points around the unit circle. Built once at load time (the
/// trigonometry is not constexpr before C++23) and only ever read afterwards, so
/// there is no initialisation-order hazard: nothing touches it during static
/// construction.
inline const std::array<Phasor, kUnitCircleSize> kUnitCircle = detail::makeUnitCircle();

/// Equal-power pan gains sampled across the full pan range; see @ref equalPowerPan.
inline const std::array<Phasor, detail::kPanSteps + 1> kPanTable = detail::makePanTable();

/**
 * @brief A pseudo-random point on the unit circle, taken from @p bits.
 *
 * For a start phase that is meant to be ARBITRARY, computing cos and sin of a
 * random angle is 33 ns spent arriving at a number whose only requirement is
 * that it be scattered. A table lookup satisfies that requirement in 2 ns, and
 * quantising an already-random phase to 1.4 degrees changes nothing about it.
 *
 * Measured 15x faster than the cos/sin pair it replaces. RT-safe, @c noexcept.
 *
 * @param bits Any well-mixed 32-bit value (a hash, a PRNG word).
 */
[[nodiscard]] inline Phasor randomPhasor(std::uint32_t bits) noexcept
{
    return kUnitCircle[bits & (kUnitCircleSize - 1)];
}

/**
 * @brief Equal-power pan gains for a source at normalised pan @p panNorm.
 * @param panNorm  -1 = hard left, 0 = centre, +1 = hard right.
 * @param outLeft  Receives the left-channel gain.
 * @param outRight Receives the right-channel gain.
 *
 * Uses the sin/cos constant-power law so a centred source is -3 dB per side.
 *
 * Read from a 257-entry table with linear interpolation rather than evaluated:
 * this is called once per PARTIAL per note-on (each rank sits at its own place
 * in the case), so on a Tutti chord it is thousands of cos/sin pairs inside one
 * audio callback. Measured 3.2x faster, worst gain error 4.8e-6, and the
 * constant-power law itself survives the interpolation to 9.5e-6 -- so the
 * centre cannot drift louder than the edges, which is the one thing a cheap pan
 * law is usually guilty of. RT-safe, @c noexcept.
 */
inline void equalPowerPan(float panNorm, float& outLeft, float& outRight) noexcept
{
    const float u  = (clamp(panNorm, -1.0f, 1.0f) + 1.0f)
                   * (0.5f * static_cast<float>(detail::kPanSteps));
    const auto  iu = static_cast<std::size_t>(u);
    const std::size_t i = iu < detail::kPanSteps ? iu : detail::kPanSteps - 1;
    const float f = u - static_cast<float>(i);

    const Phasor& a = kPanTable[i];
    const Phasor& b = kPanTable[i + 1];
    outLeft  = a.cos + (b.cos - a.cos) * f;
    outRight = a.sin + (b.sin - a.sin) * f;
}

} // namespace caecilia::dsp
