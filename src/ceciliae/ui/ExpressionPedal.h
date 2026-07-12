/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
 * @file ExpressionPedal.h
 * @brief A swell/expression shoe the player drags (or MIDI drives) to open and
 *        close the swell box.
 *
 * The value is a normalised 0..1 opening (0 = closed/quietest, 1 = fully open).
 * It emits @ref onValueChanged on drag; the host maps that to the division's
 * enclosure gain and, in a wind-aware engine, to subtle brightness. It can also
 * be driven from MIDI by calling @ref setValue with @c notify=false.
 */

namespace ceciliae::ui
{

class ExpressionPedal final : public juce::Component
{
public:
    ExpressionPedal() = default;

    /// Set the opening in [0,1]. @p notify controls whether @ref onValueChanged
    /// fires (false for MIDI-driven updates to avoid feedback loops).
    void setValue(float normalised, bool notify = true);
    [[nodiscard]] float value() const noexcept { return value_; }

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    /// Fired when the user drags the shoe.
    std::function<void(float)> onValueChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    void setFromMouse(juce::Point<int> p);

    ThemeTokens tokens_{};
    float       value_ = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressionPedal)
};

} // namespace ceciliae::ui
