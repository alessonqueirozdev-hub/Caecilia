/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/WindPressureMeter.h"

#include "caecilia/ui/JuceInterop.h"

#include <cmath>

namespace caecilia::ui
{

WindPressureMeter::WindPressureMeter()
{
    startTimerHz(60);
}

WindPressureMeter::~WindPressureMeter()
{
    stopTimer();
}

void WindPressureMeter::setWindState(float pressurePa, float nominalPa, float sagNorm, float tremulantPhase)
{
    pressurePa_     = pressurePa;
    nominalPa_      = nominalPa > 0.0f ? nominalPa : 1.0f;
    sagNorm_        = sagNorm;
    tremulantPhase_ = tremulantPhase;
}

void WindPressureMeter::timerCallback()
{
    // Smooth the displayed fill toward the true (sag-reduced) level so the plate
    // eases rather than snapping — a more physical feel.
    const float target = juce::jlimit(0.0f, 1.0f, 1.0f + sagNorm_);
    displayedFill_ += (target - displayedFill_) * 0.2f;
    repaint();
}

void WindPressureMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);

    // Bellows plate: droops as the reservoir sags; a small tremulant wobble is
    // overlaid so the breathing is visible even at steady load.
    const float wobble = 0.015f * std::sin(tremulantPhase_ * juce::MathConstants<float>::twoPi);
    const float fill = juce::jlimit(0.0f, 1.0f, displayedFill_ + wobble);

    auto plate = bounds.reduced(4.0f);
    plate = plate.withTop(plate.getBottom() - plate.getHeight() * fill);
    juce::ColourGradient grad(toColour(tokens_.windGauge).brighter(0.15f), plate.getTopLeft(),
                              toColour(tokens_.windGauge).darker(0.15f),   plate.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRect(plate);

    // Nominal reference line.
    g.setColour(toColour(tokens_.textSecondary).withAlpha(0.5f));
    const float nominalY = bounds.getBottom() - bounds.getHeight() * 0.9f;
    g.drawHorizontalLine(juce::roundToInt(nominalY), bounds.getX(), bounds.getRight());

    // Readout.
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
    g.drawText(juce::String(juce::roundToInt(pressurePa_)) + " / "
                   + juce::String(juce::roundToInt(nominalPa_)) + " Pa",
               bounds, juce::Justification::centredTop, false);
}

} // namespace caecilia::ui
