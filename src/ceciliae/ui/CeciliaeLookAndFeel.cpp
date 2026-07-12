/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/CeciliaeLookAndFeel.h"

#include "ceciliae/ui/JuceInterop.h"

namespace ceciliae::ui
{

CeciliaeLookAndFeel::CeciliaeLookAndFeel()
{
    // Start from the built-in default theme so the object is usable before any
    // ThemeManager wiring exists.
    refreshFromTheme(Theme{ "flat-dark", SkinId::Flat, ThemeTokens{} });
}

void CeciliaeLookAndFeel::refreshFromTheme(const Theme& theme)
{
    const ThemeTokens& t = theme.tokens();
    cornerRadius_ = t.cornerRadius;

    const auto surface  = toColour(t.surface);
    const auto raised   = toColour(t.surfaceRaised);
    const auto text     = toColour(t.textPrimary);
    const auto accent   = toColour(t.accent);

    setColour(juce::ResizableWindow::backgroundColourId, surface);
    setColour(juce::PopupMenu::backgroundColourId,       raised);
    setColour(juce::PopupMenu::textColourId,             text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::ComboBox::backgroundColourId,        raised);
    setColour(juce::ComboBox::textColourId,              text);
    setColour(juce::Label::textColourId,                 text);
    setColour(juce::TextButton::buttonColourId,          raised);
    setColour(juce::TextButton::textColourOnId,          text);
    setColour(juce::TextButton::textColourOffId,         toColour(t.textSecondary));
    setColour(juce::AlertWindow::backgroundColourId,     surface);
    setColour(juce::AlertWindow::textColourId,           text);
}

juce::Font CeciliaeLookAndFeel::getPopupMenuFont()
{
    // Clean, legible default; the console proper uses its own vector typography.
    return juce::Font(juce::FontOptions{ 15.0f });
}

void CeciliaeLookAndFeel::drawButtonBackground(juce::Graphics&      g,
                                               juce::Button&        button,
                                               const juce::Colour&  backgroundColour,
                                               bool                 shouldDrawButtonAsHighlighted,
                                               bool                 shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill   = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.1f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, cornerRadius_);
    g.setColour(findColour(juce::TextButton::textColourOffId).withAlpha(0.4f));
    g.drawRoundedRectangle(bounds, cornerRadius_, 1.0f);
}

} // namespace ceciliae::ui
