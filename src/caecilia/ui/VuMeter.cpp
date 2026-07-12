/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/VuMeter.h"

#include "caecilia/ui/JuceInterop.h"

namespace caecilia::ui
{

VuMeter::VuMeter(Orientation orientation)
    : orientation_(orientation)
{
}

void VuMeter::setFrame(const core::engine::MeterFrame& frame)
{
    peak_ = juce::jlimit(0.0f, 1.0f, frame.peak);
    rms_  = juce::jlimit(0.0f, 1.0f, frame.rms);
    // Peak-hold slowly falls; jumps up instantly to a new peak.
    peakHold_ = juce::jmax(peak_, peakHold_ * 0.92f); // TODO(v0.2): time-based decay.
    repaint();
}

juce::Colour VuMeter::gradeColour(float level) const noexcept
{
    return level > 0.85f ? toColour(tokens_.meterHigh)
         : level > 0.6f  ? toColour(tokens_.meterMid)
                         : toColour(tokens_.meterLow);
}

void VuMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);

    auto body = bounds.reduced(2.0f);
    if (orientation_ == Orientation::Horizontal)
    {
        g.setColour(gradeColour(rms_));
        g.fillRoundedRectangle(body.withWidth(body.getWidth() * rms_), tokens_.cornerRadius);

        const float hx = body.getX() + body.getWidth() * peakHold_;
        g.setColour(gradeColour(peakHold_));
        g.fillRect(juce::Rectangle<float>(hx - 1.0f, body.getY(), 2.0f, body.getHeight()));
    }
    else
    {
        auto fill = body.withTop(body.getBottom() - body.getHeight() * rms_);
        g.setColour(gradeColour(rms_));
        g.fillRoundedRectangle(fill, tokens_.cornerRadius);

        const float hy = body.getBottom() - body.getHeight() * peakHold_;
        g.setColour(gradeColour(peakHold_));
        g.fillRect(juce::Rectangle<float>(body.getX(), hy - 1.0f, body.getWidth(), 2.0f));
    }
}

} // namespace caecilia::ui
