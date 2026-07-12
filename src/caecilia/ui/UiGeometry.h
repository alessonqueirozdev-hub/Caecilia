/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <cstddef>

/**
 * @file UiGeometry.h
 * @brief Tiny, JUCE-free logical geometry primitives shared by the layout model,
 *        the viewport controller and the skins.
 *
 * The console is authored in a resolution-independent LOGICAL coordinate space
 * (a virtual canvas). Only @c ConsoleView converts these to @c juce types at
 * paint time, so the layout model and hit-testing stay unit-testable without a
 * JUCE host and independent of the display's pixel density.
 */

namespace caecilia::ui
{

/// A logical point (x right-positive, y down-positive), in canvas units.
struct LayoutPoint
{
    float x = 0.0f;
    float y = 0.0f;

    friend constexpr bool operator==(LayoutPoint, LayoutPoint) noexcept = default;
};

/// A logical size in canvas units.
struct LayoutSize
{
    float width  = 0.0f;
    float height = 0.0f;

    friend constexpr bool operator==(LayoutSize, LayoutSize) noexcept = default;
};

/**
 * @brief An axis-aligned rectangle in logical canvas units.
 *
 * Stored as top-left origin plus extent. All helpers are @c constexpr and
 * allocation-free so geometry can be manipulated freely on any thread.
 */
struct LayoutRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    [[nodiscard]] constexpr float right()  const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr LayoutPoint centre() const noexcept
    {
        return { x + width * 0.5f, y + height * 0.5f };
    }

    /// @return true if @p p lies inside this rectangle (inclusive of the edges).
    [[nodiscard]] constexpr bool contains(LayoutPoint p) const noexcept
    {
        return p.x >= x && p.x <= right() && p.y >= y && p.y <= bottom();
    }

    /// @return a copy inset uniformly by @p amount on every edge.
    [[nodiscard]] constexpr LayoutRect reduced(float amount) const noexcept
    {
        return { x + amount, y + amount, width - 2.0f * amount, height - 2.0f * amount };
    }

    /// @return a copy translated by (@p dx, @p dy).
    [[nodiscard]] constexpr LayoutRect translated(float dx, float dy) const noexcept
    {
        return { x + dx, y + dy, width, height };
    }

    friend constexpr bool operator==(LayoutRect, LayoutRect) noexcept = default;
};

} // namespace caecilia::ui
