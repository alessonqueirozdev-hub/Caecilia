/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/CouplerRail.h"

#include "ceciliae/ui/JuceInterop.h"

namespace ceciliae::ui
{

void CouplerRail::setCouplers(std::vector<Entry> entries)
{
    entries_ = std::move(entries);
    repaint();
}

void CouplerRail::setEngaged(model::CouplerId id, bool engaged)
{
    for (auto& e : entries_)
    {
        if (e.id == id)
        {
            e.engaged = engaged;
            repaint();
            return;
        }
    }
}

int CouplerRail::indexAt(juce::Point<int> p) const noexcept
{
    const float stride = tabWidth_ + gap_;
    if (stride <= 0.0f)
        return -1;
    const int idx = static_cast<int>(static_cast<float>(p.x) / stride);
    return (idx >= 0 && idx < static_cast<int>(entries_.size())) ? idx : -1;
}

void CouplerRail::paint(juce::Graphics& g)
{
    g.fillAll(toColour(tokens_.surface));

    const auto height = static_cast<float>(getHeight());
    float x = 0.0f;
    for (const auto& e : entries_)
    {
        juce::Rectangle<float> tab(x, 0.0f, tabWidth_, height);
        auto fill = e.engaged ? toColour(tokens_.accentActive) : toColour(tokens_.surfaceRaised);
        g.setColour(fill);
        g.fillRoundedRectangle(tab.reduced(2.0f), tokens_.cornerRadius);
        g.setColour(toColour(tokens_.outline));
        g.drawRoundedRectangle(tab.reduced(2.0f), tokens_.cornerRadius, tokens_.hairline);
        g.setColour(toColour(tokens_.textPrimary));
        g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
        g.drawText(e.label, tab.reduced(4.0f), juce::Justification::centred, true);
        x += tabWidth_ + gap_;
    }
}

void CouplerRail::mouseDown(const juce::MouseEvent& e)
{
    const int idx = indexAt(e.getPosition());
    if (idx >= 0 && onToggle)
        onToggle(entries_[static_cast<std::size_t>(idx)].id);
}

} // namespace ceciliae::ui
