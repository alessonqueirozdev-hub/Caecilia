/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/ViewportController.h"

#include <algorithm>

namespace ceciliae::ui
{

void ViewportController::setZoomLimits(float minScale, float maxScale) noexcept
{
    if (minScale > 0.0f)
        minScale_ = minScale;
    if (maxScale >= minScale_)
        maxScale_ = maxScale;
}

void ViewportController::fitToView() noexcept
{
    const float sx = canvas_.width  > 0.0f ? view_.width  / canvas_.width  : 1.0f;
    const float sy = canvas_.height > 0.0f ? view_.height / canvas_.height : 1.0f;
    scale_ = std::clamp(std::min(sx, sy), minScale_, maxScale_);

    // Centre the scaled canvas in the view.
    tx_ = (view_.width  - canvas_.width  * scale_) * 0.5f;
    ty_ = (view_.height - canvas_.height * scale_) * 0.5f;
}

void ViewportController::zoomAt(LayoutPoint anchor, float factor) noexcept
{
    const float target = std::clamp(scale_ * factor, minScale_, maxScale_);
    const float actual = scale_ != 0.0f ? target / scale_ : 1.0f;

    // Keep the logical point under the cursor fixed: adjust translation so that
    // logicalToScreen(screenToLogical(anchor)) == anchor after scaling.
    tx_ = anchor.x - (anchor.x - tx_) * actual;
    ty_ = anchor.y - (anchor.y - ty_) * actual;
    scale_ = target;
}

void ViewportController::setZoom(float scale) noexcept
{
    const LayoutPoint centre{ view_.width * 0.5f, view_.height * 0.5f };
    const float clamped = std::clamp(scale, minScale_, maxScale_);
    const float factor  = scale_ != 0.0f ? clamped / scale_ : 1.0f;
    zoomAt(centre, factor);
}

} // namespace ceciliae::ui
