/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ExpressionPedal.h"

#include "caecilia/ui/JuceInterop.h"

namespace caecilia::ui
{

void ExpressionPedal::setValue(float normalised, bool notify)
{
    value_ = juce::jlimit(0.0f, 1.0f, normalised);
    if (notify && onValueChanged)
        onValueChanged(value_);
    repaint();
}

void ExpressionPedal::setFromMouse(juce::Point<int> p)
{
    const auto h = static_cast<float>(getHeight());
    if (h <= 0.0f)
        return;
    // Top of the shoe = fully open (1.0), bottom = closed (0.0).
    setValue(1.0f - static_cast<float>(p.y) / h, true);
}

void ExpressionPedal::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);

    // The shoe: a tilted plate whose fill height tracks the opening.
    const float open = value_;
    auto shoe = bounds.reduced(4.0f);
    shoe = shoe.withTop(shoe.getBottom() - shoe.getHeight() * open);
    juce::ColourGradient grad(toColour(tokens_.accent).brighter(0.2f), shoe.getTopLeft(),
                              toColour(tokens_.accent).darker(0.2f),   shoe.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(shoe, tokens_.cornerRadius);

    g.setColour(toColour(tokens_.textSecondary));
    g.setFont(juce::Font(juce::FontOptions{ 10.0f }));
    g.drawText(juce::String(juce::roundToInt(open * 100.0f)) + "%",
               bounds, juce::Justification::centredBottom, false);
}

void ExpressionPedal::mouseDown(const juce::MouseEvent& e)
{
    setFromMouse(e.getPosition());
}

void ExpressionPedal::mouseDrag(const juce::MouseEvent& e)
{
    setFromMouse(e.getPosition());
}

} // namespace caecilia::ui
