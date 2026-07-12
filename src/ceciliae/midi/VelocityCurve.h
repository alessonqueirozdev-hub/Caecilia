/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ceciliae::midi
{

/// Shape of the velocity response applied to incoming note-on velocities.
enum class VelocityCurveShape : std::uint8_t
{
    Linear, ///< Identity (out == in).
    Soft,   ///< Concave: emphasises quiet playing (gamma < 1 feel).
    Hard,   ///< Convex: emphasises loud playing (gamma > 1 feel).
    Fixed   ///< Ignore velocity; always emit @c fixedLevel (organ-classic).
};

/**
 * @brief Maps raw MIDI velocity to the velocity the engine receives.
 *
 * A pipe organ is not touch-sensitive, so @ref VelocityCurveShape::Fixed is the
 * musically correct default for most stops; the other shapes exist for hybrid
 * voices, expressive divisions, and players who prefer a responsive action.
 *
 * The 128-entry lookup table is baked once by @ref rebuild (off the audio
 * thread). @ref map is a @c noexcept, allocation-free table lookup safe to call
 * from the router on the audio thread. The type is trivially copyable so a
 * configured curve can be relocated by value.
 */
class VelocityCurve
{
public:
    /// Constructs a Fixed curve emitting velocity 100 (a sensible organ default).
    VelocityCurve() noexcept { rebuild(); }

    /// Constructs and bakes a curve of the given shape.
    VelocityCurve(VelocityCurveShape shape, float gamma, core::Velocity fixedLevel) noexcept
        : shape_(shape), gamma_(gamma), fixedLevel_(fixedLevel)
    {
        rebuild();
    }

    /**
     * @brief Reconfigure the curve and rebuild its table. NOT real-time safe.
     * @param shape      Response shape.
     * @param gamma      Curvature for Soft/Hard (> 0; 1.0 == linear).
     * @param fixedLevel Output level used when @p shape is Fixed.
     */
    void configure(VelocityCurveShape shape, float gamma, core::Velocity fixedLevel) noexcept
    {
        shape_      = shape;
        gamma_      = gamma > 0.0f ? gamma : 1.0f;
        fixedLevel_ = fixedLevel;
        rebuild();
    }

    /// @return The shaped velocity for a raw input velocity [0, 127]. RT-safe.
    [[nodiscard]] core::Velocity map(core::Velocity raw) const noexcept
    {
        return table_[raw & 0x7F];
    }

    [[nodiscard]] VelocityCurveShape shape() const noexcept { return shape_; }
    [[nodiscard]] float              gamma() const noexcept { return gamma_; }
    [[nodiscard]] core::Velocity     fixedLevel() const noexcept { return fixedLevel_; }

private:
    /// Recompute the 128-entry table from the current parameters. NOT RT-safe.
    void rebuild() noexcept
    {
        for (std::size_t v = 0; v < 128; ++v)
        {
            core::Velocity out = 0;
            switch (shape_)
            {
                case VelocityCurveShape::Fixed:
                    out = (v == 0) ? 0 : fixedLevel_; // preserve note-off (v==0).
                    break;
                case VelocityCurveShape::Linear:
                    out = static_cast<core::Velocity>(v);
                    break;
                case VelocityCurveShape::Soft:
                case VelocityCurveShape::Hard:
                {
                    const double norm = static_cast<double>(v) / 127.0;
                    const double g    = shape_ == VelocityCurveShape::Soft
                                            ? (gamma_ < 1.0f ? gamma_ : 1.0f / gamma_)
                                            : (gamma_ > 1.0f ? gamma_ : 1.0f / gamma_);
                    const double y    = std::pow(norm, g);
                    out = static_cast<core::Velocity>(y * 127.0 + 0.5);
                    break;
                }
            }
            table_[v] = out;
        }
    }

    VelocityCurveShape shape_      = VelocityCurveShape::Fixed;
    float              gamma_      = 1.0f;
    core::Velocity     fixedLevel_ = 100;

    /// Precomputed raw-velocity -> shaped-velocity table.
    std::array<core::Velocity, 128> table_{};
};

} // namespace ceciliae::midi
