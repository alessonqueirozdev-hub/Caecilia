/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ManualKeyboardComponent.h"

#include "caecilia/ui/JuceInterop.h"

#include <cstring>

namespace caecilia::ui
{
namespace
{
bool isWhiteKey(int note) noexcept
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return false;
        default:                                 return true;
    }
}

juce::Colour litColour(const ThemeTokens& t, KeySource src)
{
    switch (src)
    {
        case KeySource::PlayedDirect: return toColour(t.keyLitDirect);
        case KeySource::Coupled:      return toColour(t.keyLitCoupled);
        case KeySource::Combination:  return toColour(t.keyLitCombination);
        default:                      return juce::Colours::transparentBlack;
    }
}
} // namespace

ManualKeyboardComponent::ManualKeyboardComponent()
{
    startTimerHz(60);
}

ManualKeyboardComponent::~ManualKeyboardComponent()
{
    stopTimer();
}

void ManualKeyboardComponent::setDivision(core::DivisionId division, std::size_t mirrorSlot) noexcept
{
    division_ = division;
    slot_     = mirrorSlot;
}

void ManualKeyboardComponent::setCompass(core::MidiNote low, core::MidiNote high) noexcept
{
    low_  = low;
    high_ = high > low ? high : static_cast<core::MidiNote>(low + 1);
    repaint();
}

void ManualKeyboardComponent::timerCallback()
{
    if (mirror_ == nullptr)
        return;

    const ConsoleFrame frame = mirror_->read();
    // Repaint only if this division's row changed since the last poll.
    if (std::memcmp(frame.keys.source[slot_ < KeyStateSnapshot::kMaxDivisions ? slot_ : 0].data(),
                    lastKeys_.source[slot_ < KeyStateSnapshot::kMaxDivisions ? slot_ : 0].data(),
                    KeyStateSnapshot::kNotes) != 0)
    {
        lastKeys_ = frame.keys;
        repaint();
    }
}

core::MidiNote ManualKeyboardComponent::noteAt(juce::Point<int> p) const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() <= 0.0f)
        return low_;
    const float frac = juce::jlimit(0.0f, 1.0f, (static_cast<float>(p.x) - bounds.getX()) / bounds.getWidth());
    const int span = static_cast<int>(high_) - static_cast<int>(low_);
    // TODO(v0.2): proper white/black key hit geometry; proportional for now.
    return static_cast<core::MidiNote>(juce::jlimit(0, 127, low_ + juce::roundToInt(frac * static_cast<float>(span))));
}

void ManualKeyboardComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(toColour(tokens_.surfaceSunken));

    int whiteCount = 0;
    for (int k = low_; k <= high_; ++k)
        whiteCount += isWhiteKey(k) ? 1 : 0;
    whiteCount = juce::jmax(1, whiteCount);
    const float whiteW = bounds.getWidth() / static_cast<float>(whiteCount);

    float x = bounds.getX();
    for (int k = low_; k <= high_; ++k)
    {
        if (!isWhiteKey(k))
            continue;
        juce::Rectangle<float> key(x, bounds.getY(), whiteW, bounds.getHeight());
        const auto src = lastKeys_.get(slot_, static_cast<core::MidiNote>(k));
        g.setColour(src != KeySource::Off ? litColour(tokens_, src) : juce::Colours::white);
        g.fillRect(key.reduced(0.5f));
        g.setColour(toColour(tokens_.outline).withAlpha(0.5f));
        g.drawRect(key, 1.0f);
        x += whiteW;
    }

    x = bounds.getX();
    const float blackW = whiteW * 0.6f, blackH = bounds.getHeight() * 0.62f;
    for (int k = low_; k <= high_; ++k)
    {
        if (isWhiteKey(k)) { x += whiteW; continue; }
        juce::Rectangle<float> key(x - blackW * 0.5f, bounds.getY(), blackW, blackH);
        const auto src = lastKeys_.get(slot_, static_cast<core::MidiNote>(k));
        g.setColour(src != KeySource::Off ? litColour(tokens_, src) : juce::Colours::black);
        g.fillRect(key);
    }
}

void ManualKeyboardComponent::mouseDown(const juce::MouseEvent& e)
{
    heldNote_ = noteAt(e.getPosition());
    holding_  = true;
    if (onKeyEvent)
        onKeyEvent(heldNote_, true);
}

void ManualKeyboardComponent::mouseUp(const juce::MouseEvent&)
{
    if (holding_)
    {
        holding_ = false;
        if (onKeyEvent)
            onKeyEvent(heldNote_, false);
    }
}

} // namespace caecilia::ui
