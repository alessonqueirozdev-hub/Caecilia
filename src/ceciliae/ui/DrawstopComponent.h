/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

/**
 * @file DrawstopComponent.h
 * @brief A standalone, animated drawstop/drawknob showing its TonalFamily +
 *        Footage and toggling with a smooth pull/press.
 *
 * The knob eases outward when engaged and back when released (an animated
 * @c pullNorm) so the console feels tactile. It is tinted by tonal family from
 * the theme and labels its footage using the shared @c model::footageLabel so
 * spelling matches everywhere. Reusable in panels/tests; the unified
 * @c ConsoleView paints the same look inline via the skin.
 */

namespace ceciliae::ui
{

class DrawstopComponent final : public juce::Component,
                                private juce::Timer
{
public:
    DrawstopComponent();
    ~DrawstopComponent() override;

    /// Bind the stop's semantic identity for drawing + accessibility.
    void setStop(core::StopId id, core::TonalFamily family, core::Footage footage, std::string label);

    /// Set the engaged state; animates the pull. @p animate=false snaps instantly.
    void setEngaged(bool engaged, bool animate = true);
    [[nodiscard]] bool isEngaged() const noexcept { return engaged_; }

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    /// Fired when the user clicks the knob (a toggle request).
    std::function<void(core::StopId)> onToggle;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    core::StopId      id_{};
    core::TonalFamily family_ = core::TonalFamily::Undefined;
    core::Footage     footage_{};
    std::string       label_;
    ThemeTokens       tokens_{};

    bool  engaged_  = false;
    float pullNorm_ = 0.0f; ///< Animated 0..1 toward @c engaged_.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawstopComponent)
};

} // namespace ceciliae::ui
