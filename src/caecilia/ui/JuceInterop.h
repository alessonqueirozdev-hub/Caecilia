/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/ui/Theme.h"
#include "caecilia/ui/UiGeometry.h"

#include <juce_graphics/juce_graphics.h>

/**
 * @file JuceInterop.h
 * @brief Thin, header-only bridges from Caecilia's JUCE-free UI value types to
 *        the corresponding @c juce types.
 *
 * The layout model, theme tokens and geometry are all JUCE-free so they stay
 * testable and audio-thread-safe. This is the single place those types are
 * mapped onto @c juce::Colour / @c juce::Rectangle, so the two skins and the
 * look-and-feel convert them identically. Message thread only.
 */

namespace caecilia::ui
{

/// Convert a straight-alpha @c Rgba token to a @c juce::Colour.
[[nodiscard]] inline juce::Colour toColour(const Rgba& c) noexcept
{
    return juce::Colour::fromRGBA(c.r, c.g, c.b, c.a);
}

/// Convert a logical @c LayoutRect to a float @c juce::Rectangle (pre-transform).
[[nodiscard]] inline juce::Rectangle<float> toRect(const LayoutRect& r) noexcept
{
    return { r.x, r.y, r.width, r.height };
}

/// Convert a logical @c LayoutPoint to a float @c juce::Point.
[[nodiscard]] inline juce::Point<float> toPoint(const LayoutPoint& p) noexcept
{
    return { p.x, p.y };
}

} // namespace caecilia::ui
