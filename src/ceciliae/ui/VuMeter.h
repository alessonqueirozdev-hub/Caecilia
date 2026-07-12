/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/engine/MeterSnapshot.h"
#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file VuMeter.h
 * @brief A peak + RMS output level meter driven by the engine's MeterFrame.
 *
 * The bar shows an instantaneous peak with a slowly-falling peak-hold marker and
 * an RMS body, colour-graded green/amber/red from the theme. It reads a
 * @c core::engine::MeterFrame the host pushes each frame (already polled from the
 * lock-free @c StateMirror), so the meter itself does no cross-thread work.
 */

namespace ceciliae::ui
{

class VuMeter final : public juce::Component
{
public:
    /// Orientation of the bar.
    enum class Orientation { Horizontal, Vertical };

    explicit VuMeter(Orientation orientation = Orientation::Horizontal);

    /// Push a new frame (linear peak/rms). Applies peak-hold decay + repaints.
    void setFrame(const core::engine::MeterFrame& frame);

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    void paint(juce::Graphics& g) override;

private:
    [[nodiscard]] juce::Colour gradeColour(float level) const noexcept;

    Orientation orientation_;
    ThemeTokens tokens_{};
    float       peak_     = 0.0f;
    float       rms_      = 0.0f;
    float       peakHold_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeter)
};

} // namespace ceciliae::ui
