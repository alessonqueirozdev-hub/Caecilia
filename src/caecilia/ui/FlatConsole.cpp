/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/FlatConsole.h"

#include "caecilia/model/Stop.h"
#include "caecilia/ui/JuceInterop.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace caecilia::ui
{
namespace
{
/// Map a KeySource byte to its theme illumination colour.
juce::Colour litColour(const ThemeTokens& t, KeySource src)
{
    switch (src)
    {
        case KeySource::PlayedDirect: return toColour(t.keyLitDirect);
        case KeySource::Coupled:      return toColour(t.keyLitCoupled);
        case KeySource::Combination:  return toColour(t.keyLitCombination);
        case KeySource::Off:
        default:                      return juce::Colours::transparentBlack;
    }
}

/// @return true for the seven white keys of the chromatic octave.
bool isWhiteKey(int note) noexcept
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return false; // C#, D#, F#, G#, A#
        default:                                 return true;
    }
}
} // namespace

void FlatConsole::paintBackground(juce::Graphics& g, const LayoutRect& canvas)
{
    g.setColour(toColour(tokens_.surface));
    g.fillRect(toRect(canvas));
}

void FlatConsole::paintJamb(juce::Graphics& g, const ConsoleElement& element)
{
    g.setColour(toColour(tokens_.surfaceRaised));
    g.fillRoundedRectangle(toRect(element.bounds), tokens_.cornerRadius);
}

void FlatConsole::paintLabel(juce::Graphics& g, const ConsoleElement& element)
{
    g.setColour(toColour(tokens_.textSecondary));
    g.setFont(juce::Font(juce::FontOptions{ 14.0f }).boldened());
    g.drawText(element.semantic.label, toRect(element.bounds),
               juce::Justification::centredLeft, true);
}

void FlatConsole::paintDrawstop(juce::Graphics&        g,
                                const ConsoleElement&  element,
                                const DrawstopVisual&  visual)
{
    const auto bounds = toRect(element.bounds).reduced(2.0f);
    const auto tint   = toColour(tokens_.tintFor(element.family));

    // A flat rounded chip; engaged = tinted fill, idle = sunken surface.
    auto fill = visual.engaged ? tint : toColour(tokens_.surfaceSunken);
    if (visual.hovered)
        fill = fill.brighter(0.12f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);
    g.setColour(toColour(tokens_.outline));
    g.drawRoundedRectangle(bounds, tokens_.cornerRadius, tokens_.hairline);

    // Footage label (e.g. "8'"), from the shared model helper so spelling matches.
    g.setColour(visual.engaged ? toColour(tokens_.surface) : toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 13.0f }));
    g.drawText(model::footageLabel(element.footage), bounds,
               juce::Justification::centred, false);
}

void FlatConsole::paintKeyboard(juce::Graphics&          g,
                                const ConsoleElement&    element,
                                const KeyStateSnapshot&  keys,
                                std::size_t              divisionSlot)
{
    const auto bounds = toRect(element.bounds);
    const int  low    = element.lowNote;
    const int  high   = element.highNote;
    const int  whiteCount = [&]
    {
        int n = 0;
        for (int k = low; k <= high; ++k)
            n += isWhiteKey(k) ? 1 : 0;
        return juce::jmax(1, n);
    }();

    const float whiteW = bounds.getWidth() / static_cast<float>(whiteCount);

    // White keys first.
    float x = bounds.getX();
    for (int k = low; k <= high; ++k)
    {
        if (!isWhiteKey(k))
            continue;

        juce::Rectangle<float> key(x, bounds.getY(), whiteW, bounds.getHeight());
        const auto src = keys.get(divisionSlot, static_cast<core::MidiNote>(k));
        g.setColour(src != KeySource::Off ? litColour(tokens_, src)
                                          : juce::Colours::white);
        g.fillRect(key.reduced(0.5f));
        g.setColour(toColour(tokens_.outline));
        g.drawRect(key, tokens_.hairline);
        x += whiteW;
    }

    // Black keys on top (drawn at the boundary between the flanking white keys).
    x = bounds.getX();
    const float blackW = whiteW * 0.6f;
    const float blackH = bounds.getHeight() * 0.62f;
    for (int k = low; k <= high; ++k)
    {
        if (isWhiteKey(k))
        {
            x += whiteW;
            continue;
        }
        juce::Rectangle<float> key(x - blackW * 0.5f, bounds.getY(), blackW, blackH);
        const auto src = keys.get(divisionSlot, static_cast<core::MidiNote>(k));
        g.setColour(src != KeySource::Off ? litColour(tokens_, src) : juce::Colours::black);
        g.fillRect(key);
    }
}

void FlatConsole::paintCoupler(juce::Graphics&        g,
                               const ConsoleElement&  element,
                               const CouplerVisual&   visual)
{
    const auto bounds = toRect(element.bounds).reduced(1.0f);
    auto fill = visual.engaged ? toColour(tokens_.accentActive) : toColour(tokens_.surfaceRaised);
    if (visual.hovered)
        fill = fill.brighter(0.1f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
    g.drawText(element.semantic.label, bounds, juce::Justification::centred, true);
}

void FlatConsole::paintMeter(juce::Graphics&                 g,
                             const ConsoleElement&           element,
                             const core::engine::MeterFrame& frame)
{
    const auto bounds = toRect(element.bounds);
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);

    const float level = juce::jlimit(0.0f, 1.0f, frame.peak);
    auto fill = bounds.withWidth(bounds.getWidth() * level);
    const auto colour = level > 0.85f ? toColour(tokens_.meterHigh)
                      : level > 0.6f  ? toColour(tokens_.meterMid)
                                      : toColour(tokens_.meterLow);
    g.setColour(colour);
    g.fillRoundedRectangle(fill.reduced(1.0f), tokens_.cornerRadius);
}

void FlatConsole::paintWindGauge(juce::Graphics&        g,
                                 const ConsoleElement&  element,
                                 float                  pressurePa,
                                 float                  sagNorm)
{
    const auto bounds = toRect(element.bounds);
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);

    // Fill drops as the reservoir sags under polyphony load. sagNorm <= 0.
    const float fillNorm = juce::jlimit(0.0f, 1.0f, 1.0f + sagNorm);
    auto fill = bounds.reduced(3.0f);
    fill = fill.withTop(fill.getBottom() - fill.getHeight() * fillNorm);
    g.setColour(toColour(tokens_.windGauge));
    g.fillRect(fill);

    g.setColour(toColour(tokens_.textSecondary));
    g.setFont(juce::Font(juce::FontOptions{ 10.0f }));
    g.drawText(juce::String(juce::roundToInt(pressurePa)) + " Pa", bounds,
               juce::Justification::centredBottom, false);
    juce::ignoreUnused(sagNorm);
}

} // namespace caecilia::ui
