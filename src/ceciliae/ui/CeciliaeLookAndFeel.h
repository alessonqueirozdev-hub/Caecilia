/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file CeciliaeLookAndFeel.h
 * @brief The custom look-and-feel for STANDARD JUCE dialogs only.
 *
 * The console itself is painted entirely by the skins (@c ISkin), not by a
 * look-and-feel, so the resolution-independent vector look is never at the mercy
 * of JUCE's default widget drawing. This class exists only to give ordinary
 * chrome — menus, alert windows, combo boxes, the settings panel — typography
 * and colours consistent with the active @c Theme.
 */

namespace ceciliae::ui
{

class CeciliaeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CeciliaeLookAndFeel();
    ~CeciliaeLookAndFeel() override = default;

    /// Re-derive every JUCE colour id from @p theme's tokens. Call on theme change.
    void refreshFromTheme(const Theme& theme);

    // --- Selected overrides for a clean, crisp default look -----------------
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

private:
    float cornerRadius_ = 4.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CeciliaeLookAndFeel)
};

} // namespace ceciliae::ui
