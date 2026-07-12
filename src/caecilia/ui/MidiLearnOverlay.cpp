/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/MidiLearnOverlay.h"

#include "caecilia/ui/JuceInterop.h"

namespace caecilia::ui
{

MidiLearnOverlay::MidiLearnOverlay()
{
    setInterceptsMouseClicks(false, false); // click-through until activated
}

void MidiLearnOverlay::bind(const ConsoleLayoutModel* model, const ViewportController* viewport) noexcept
{
    model_    = model;
    viewport_ = viewport;
    repaint();
}

void MidiLearnOverlay::setActive(bool active)
{
    active_ = active;
    setInterceptsMouseClicks(active, active);
    repaint();
}

void MidiLearnOverlay::setArmed(const SemanticId* armed)
{
    hasArmed_ = armed != nullptr;
    if (hasArmed_)
        armed_ = *armed;
    repaint();
}

bool MidiLearnOverlay::hitTest(int, int)
{
    // Only intercept when in learn mode; otherwise the console gets the click.
    return active_;
}

void MidiLearnOverlay::paint(juce::Graphics& g)
{
    if (!active_ || model_ == nullptr || viewport_ == nullptr)
        return;

    // Dim the console and badge each learnable element.
    g.fillAll(toColour(tokens_.surfaceSunken).withAlpha(0.45f));

    for (const auto& e : model_->elements())
    {
        if (!e.learnable)
            continue;

        const auto tl = viewport_->logicalToScreen({ e.bounds.x, e.bounds.y });
        const auto br = viewport_->logicalToScreen({ e.bounds.right(), e.bounds.bottom() });
        juce::Rectangle<float> screen(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

        const bool isArmed = hasArmed_ && armed_.oscAddress == e.semantic.oscAddress;
        g.setColour(isArmed ? toColour(tokens_.accentActive) : toColour(tokens_.accent).withAlpha(0.85f));
        g.drawRoundedRectangle(screen.reduced(1.0f), tokens_.cornerRadius, 2.0f);
        g.setColour(toColour(tokens_.textPrimary));
        g.setFont(juce::Font(juce::FontOptions{ 10.0f }));
        g.drawText(isArmed ? "learning..." : "learn", screen, juce::Justification::centred, false);
    }
}

void MidiLearnOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (!active_ || model_ == nullptr || viewport_ == nullptr)
        return;

    const auto logical = viewport_->screenToLogical({ static_cast<float>(e.x), static_cast<float>(e.y) });
    const ConsoleElement* hit = model_->hitTest(logical);
    if (hit != nullptr && hit->learnable && onElementChosen)
    {
        setArmed(&hit->semantic);
        onElementChosen(hit->semantic);
    }
}

} // namespace caecilia::ui
