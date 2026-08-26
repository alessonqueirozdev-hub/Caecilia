// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

/**
 * @file Bandlimited.h
 * @brief PolyBLEP-style band-limited step / ramp residuals.
 *
 * The nonlinear synthesis island (jet startup, reed beating) generates sharp
 * discontinuities that would otherwise alias. Adding a small band-limited
 * residual around each discontinuity cancels most of the aliased energy cheaply,
 * without the latency of a full band-limited-impulse-train table. These are the
 * standard closed-form polynomial approximations (public-domain), written fresh.
 * Nothing calls them yet: the nonlinear voices that will are not wired into the
 * plugin.
 */

namespace caecilia::dsp
{

/**
 * @brief Band-limited step residual (polyBLEP) to correct a unit discontinuity.
 * @param t  Normalised phase in [0, 1) of the oscillator at the current sample.
 * @param dt Phase increment per sample (= frequency / sampleRate).
 * @return The correction to ADD to a naive sawtooth/square near its jump.
 *
 * Two-sample parabolic correction: nonzero only within @p dt of a wrap. RT-safe.
 */
[[nodiscard]] inline float polyBlep(float t, float dt) noexcept
{
    if (dt <= 0.0f)
        return 0.0f;

    if (t < dt)
    {
        const float x = t / dt;
        return x + x - x * x - 1.0f; // 2x - x^2 - 1
    }
    if (t > 1.0f - dt)
    {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f; // x^2 + 2x + 1
    }
    return 0.0f;
}

/**
 * @brief Band-limited ramp residual (polyBLAMP) for slope discontinuities.
 * @param t  Normalised phase in [0, 1).
 * @param dt Phase increment per sample.
 * @return The correction to ADD near a triangle/reed slope change. RT-safe.
 */
[[nodiscard]] inline float polyBlamp(float t, float dt) noexcept
{
    if (dt <= 0.0f)
        return 0.0f;

    if (t < dt)
    {
        const float x = t / dt - 1.0f;
        return -1.0f / 3.0f * x * x * x;
    }
    if (t > 1.0f - dt)
    {
        const float x = (t - 1.0f) / dt + 1.0f;
        return 1.0f / 3.0f * x * x * x;
    }
    return 0.0f;
}

/**
 * @brief Convenience alias: the band-limited residual for a hard step.
 *
 * Named to match the module contract; forwards to @ref polyBlep. RT-safe.
 */
[[nodiscard]] inline float bandlimitedResidual(float t, float dt) noexcept
{
    return polyBlep(t, dt);
}

} // namespace caecilia::dsp
