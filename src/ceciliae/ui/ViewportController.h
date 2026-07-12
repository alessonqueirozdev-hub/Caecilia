/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/UiGeometry.h"

/**
 * @file ViewportController.h
 * @brief The zoom / pan transform between logical canvas space and on-screen
 *        pixels for the console.
 *
 * Keeping this JUCE-free means the console's coordinate math is unit-testable and
 * that a single uniform-scale-plus-translate transform is the one source of truth
 * for BOTH painting (logical -> screen) and hit-testing (screen -> logical).
 * @c ConsoleView converts the exposed factors into a @c juce::AffineTransform at
 * paint time; nothing else needs JUCE.
 */

namespace ceciliae::ui
{

class ViewportController
{
public:
    ViewportController() = default;

    /// Set the logical canvas size the console was laid out in.
    void setCanvas(LayoutSize canvas) noexcept { canvas_ = canvas; }

    /// Set the on-screen view size in pixels (the Component's bounds).
    void setViewSize(LayoutSize view) noexcept { view_ = view; }

    /**
     * @brief Reset zoom/pan so the whole canvas fits centred in the view.
     * Chooses the largest uniform scale that fits and centres the result.
     */
    void fitToView() noexcept;

    // --- Transform ----------------------------------------------------------
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] LayoutPoint translation() const noexcept { return { tx_, ty_ }; }

    /// Map a logical canvas point to a screen pixel.
    [[nodiscard]] LayoutPoint logicalToScreen(LayoutPoint p) const noexcept
    {
        return { p.x * scale_ + tx_, p.y * scale_ + ty_ };
    }

    /// Map a screen pixel back to a logical canvas point.
    [[nodiscard]] LayoutPoint screenToLogical(LayoutPoint p) const noexcept
    {
        const float inv = scale_ != 0.0f ? 1.0f / scale_ : 0.0f;
        return { (p.x - tx_) * inv, (p.y - ty_) * inv };
    }

    // --- Interaction --------------------------------------------------------
    /**
     * @brief Multiply the zoom by @p factor about the screen point @p anchor, so
     *        the logical point under the cursor stays put. Clamps to sane limits.
     */
    void zoomAt(LayoutPoint anchor, float factor) noexcept;

    /// Translate the view by a screen-space delta.
    void panBy(float dxPixels, float dyPixels) noexcept { tx_ += dxPixels; ty_ += dyPixels; }

    /// Set an absolute zoom about the view centre (clamped).
    void setZoom(float scale) noexcept;

    // --- Limits -------------------------------------------------------------
    void setZoomLimits(float minScale, float maxScale) noexcept;

private:
    LayoutSize canvas_{ 1280.0f, 720.0f };
    LayoutSize view_{ 1280.0f, 720.0f };
    float      scale_ = 1.0f;
    float      tx_    = 0.0f;
    float      ty_    = 0.0f;
    float      minScale_ = 0.25f;
    float      maxScale_ = 4.0f;
};

} // namespace ceciliae::ui
