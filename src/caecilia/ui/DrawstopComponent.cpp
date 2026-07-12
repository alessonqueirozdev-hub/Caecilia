/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/DrawstopComponent.h"

#include "caecilia/model/Stop.h"
#include "caecilia/ui/JuceInterop.h"

#include <cmath>

namespace caecilia::ui
{

DrawstopComponent::DrawstopComponent()
{
    setInterceptsMouseClicks(true, false);
}

DrawstopComponent::~DrawstopComponent()
{
    stopTimer();
}

void DrawstopComponent::setStop(core::StopId id, core::TonalFamily family,
                                core::Footage footage, std::string label)
{
    id_      = id;
    family_  = family;
    footage_ = footage;
    label_   = std::move(label);
    repaint();
}

void DrawstopComponent::setEngaged(bool engaged, bool animate)
{
    engaged_ = engaged;
    if (animate)
        startTimerHz(60);
    else
    {
        pullNorm_ = engaged ? 1.0f : 0.0f;
        repaint();
    }
}

void DrawstopComponent::timerCallback()
{
    const float target = engaged_ ? 1.0f : 0.0f;
    // Critically-damped-ish ease; stop when settled.
    pullNorm_ += (target - pullNorm_) * 0.35f;
    if (std::abs(target - pullNorm_) < 0.01f)
    {
        pullNorm_ = target;
        stopTimer();
    }
    repaint();
}

void DrawstopComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(3.0f);
    bounds = bounds.translated(0.0f, -4.0f * pullNorm_);

    const auto tint = toColour(tokens_.tintFor(family_));
    const auto face = engaged_ ? tint : toColour(tokens_.surfaceRaised);

    juce::ColourGradient grad(face.brighter(0.3f), bounds.getCentreX(), bounds.getY(),
                              face.darker(0.3f),   bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillEllipse(bounds);
    g.setColour(toColour(tokens_.outline));
    g.drawEllipse(bounds, 1.5f);

    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 12.0f }).boldened());
    g.drawText(model::footageLabel(footage_), bounds, juce::Justification::centred, false);

    // Stop name beneath the knob.
    if (!label_.empty())
    {
        g.setColour(toColour(tokens_.textSecondary));
        g.setFont(juce::Font(juce::FontOptions{ 10.0f }));
        g.drawText(label_, getLocalBounds().removeFromBottom(12), juce::Justification::centred, true);
    }
}

void DrawstopComponent::mouseDown(const juce::MouseEvent&)
{
    if (onToggle)
        onToggle(id_);
}

} // namespace caecilia::ui
