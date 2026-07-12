/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/CombinationBar.h"

#include "caecilia/ui/JuceInterop.h"

namespace caecilia::ui
{

void CombinationBar::setPistonCount(int count)
{
    pistonCount_ = juce::jmax(0, count);
    resized();
    repaint();
}

void CombinationBar::setSequencerState(int currentStep, int totalSteps, int pendingStep)
{
    currentStep_ = currentStep;
    totalSteps_  = totalSteps;
    pendingStep_ = pendingStep;
    repaint();
}

void CombinationBar::resized()
{
    auto r = getLocalBounds();
    const int stepperW = 150;
    auto stepper = r.removeFromRight(stepperW);
    prevButton_ = stepper.removeFromLeft(stepperW / 3);
    setButton_  = stepper.removeFromLeft(stepperW / 3);
    nextButton_ = stepper;
    pistonArea_ = r;
}

CombinationBar::HitResult CombinationBar::hitAt(juce::Point<int> p) const noexcept
{
    if (prevButton_.contains(p)) return { Hit::Prev, 0 };
    if (setButton_.contains(p))  return { Hit::Set, 0 };
    if (nextButton_.contains(p)) return { Hit::Next, 0 };
    if (pistonArea_.contains(p) && pistonCount_ > 0)
    {
        const int w = pistonArea_.getWidth() / pistonCount_;
        if (w > 0)
        {
            const int idx = (p.x - pistonArea_.getX()) / w;
            if (idx >= 0 && idx < pistonCount_)
                return { Hit::Piston, static_cast<std::uint16_t>(idx + 1) };
        }
    }
    return { Hit::None, 0 };
}

void CombinationBar::paint(juce::Graphics& g)
{
    g.fillAll(toColour(tokens_.surface));

    // Pistons.
    if (pistonCount_ > 0)
    {
        const int w = pistonArea_.getWidth() / pistonCount_;
        for (int i = 0; i < pistonCount_; ++i)
        {
            juce::Rectangle<int> cell(pistonArea_.getX() + i * w, pistonArea_.getY(), w, pistonArea_.getHeight());
            g.setColour(toColour(tokens_.surfaceRaised));
            g.fillRoundedRectangle(cell.toFloat().reduced(3.0f), tokens_.cornerRadius);
            g.setColour(toColour(tokens_.textPrimary));
            g.setFont(juce::Font(juce::FontOptions{ 12.0f }));
            g.drawText(juce::String(i + 1), cell, juce::Justification::centred, false);
        }
    }

    // Stepper buttons.
    auto drawBtn = [&](juce::Rectangle<int> r, const juce::String& text, bool highlight)
    {
        g.setColour(highlight ? toColour(tokens_.accentActive) : toColour(tokens_.surfaceRaised));
        g.fillRoundedRectangle(r.toFloat().reduced(3.0f), tokens_.cornerRadius);
        g.setColour(toColour(tokens_.textPrimary));
        g.setFont(juce::Font(juce::FontOptions{ 12.0f }));
        g.drawText(text, r, juce::Justification::centred, false);
    };
    drawBtn(prevButton_, "<", false);
    drawBtn(setButton_,  "Set", false);
    drawBtn(nextButton_, ">", pendingStep_ >= 0);

    // Sequencer position readout (current / pending / total).
    g.setColour(toColour(tokens_.textSecondary));
    g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
    juce::String pos = juce::String(currentStep_);
    if (totalSteps_ > 0)
        pos << " / " << totalSteps_;
    if (pendingStep_ >= 0)
        pos << "  (next " << pendingStep_ << ")";
    g.drawText(pos, setButton_.withY(setButton_.getBottom()), juce::Justification::centredTop, false);
}

void CombinationBar::mouseDown(const juce::MouseEvent& e)
{
    const auto hit = hitAt(e.getPosition());
    switch (hit.kind)
    {
        case Hit::Piston: if (onPiston)         onPiston(hit.piston); break;
        case Hit::Prev:   if (onSequencerStep)  onSequencerStep(-1);  break;
        case Hit::Next:   if (onSequencerStep)  onSequencerStep(+1);  break;
        case Hit::Set:    if (onSet)            onSet();              break;
        case Hit::None:
        default:                                                       break;
    }
}

} // namespace caecilia::ui
