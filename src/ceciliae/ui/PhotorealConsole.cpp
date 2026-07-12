/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/ui/PhotorealConsole.h"

#include "ceciliae/model/Stop.h"
#include "ceciliae/ui/JuceInterop.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace ceciliae::ui
{
namespace
{
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

bool isWhiteKey(int note) noexcept
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return false;
        default:                                 return true;
    }
}
} // namespace

void PhotorealConsole::setTheme(const Theme& theme)
{
    tokens_ = theme.tokens();
    rebuildWoodBrush();
}

void PhotorealConsole::rebuildWoodBrush()
{
    // TODO(v0.9): synthesise a seamless wood-grain tile (layered value noise +
    // directional streaks) into woodTexture_ for a TiledImageBrush. For the
    // scaffold we leave it empty and fall back to a vertical wood gradient.
    woodTexture_ = juce::Image();
}

void PhotorealConsole::paintBackground(juce::Graphics& g, const LayoutRect& canvas)
{
    const auto bounds = toRect(canvas);
    juce::ColourGradient grad(toColour(tokens_.surface).brighter(0.05f), bounds.getTopLeft(),
                              toColour(tokens_.surface).darker(0.25f),  bounds.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRect(bounds);
}

void PhotorealConsole::paintJamb(juce::Graphics& g, const ConsoleElement& element)
{
    const auto bounds = toRect(element.bounds);
    // Walnut face: vertical grain gradient plus a lit bevel. // TODO(v0.9): brush.
    juce::ColourGradient grad(toColour(tokens_.surfaceRaised).brighter(0.10f), bounds.getTopLeft(),
                              toColour(tokens_.surfaceRaised).darker(0.20f),  bounds.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);
    g.setColour(toColour(tokens_.outline).withAlpha(0.6f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), tokens_.cornerRadius, 1.5f);
}

void PhotorealConsole::paintLabel(juce::Graphics& g, const ConsoleElement& element)
{
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 15.0f }).italicised());
    g.drawText(element.semantic.label, toRect(element.bounds),
               juce::Justification::centredLeft, true);
}

void PhotorealConsole::paintDrawstop(juce::Graphics&        g,
                                     const ConsoleElement&  element,
                                     const DrawstopVisual&  visual)
{
    // A turned drawknob: a shaded disc that appears pulled forward when engaged.
    auto bounds = toRect(element.bounds).reduced(3.0f);
    const float pull = visual.engaged ? 1.0f : visual.pullNorm;
    bounds = bounds.translated(0.0f, -4.0f * pull); // knob eases outward when drawn

    const auto tint  = toColour(tokens_.tintFor(element.family));
    const auto face  = visual.engaged ? tint.brighter(0.05f) : toColour(tokens_.surfaceRaised);

    juce::ColourGradient knob(face.brighter(0.35f), bounds.getCentreX(), bounds.getY(),
                              face.darker(0.35f),    bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(knob);
    g.fillEllipse(bounds);
    g.setColour(toColour(tokens_.outline));
    g.drawEllipse(bounds, 1.5f);

    // Engraved footage label on the knob face.
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 12.0f }).boldened());
    g.drawText(model::footageLabel(element.footage), bounds,
               juce::Justification::centred, false);
    juce::ignoreUnused(visual.hovered);
}

void PhotorealConsole::paintKeyboard(juce::Graphics&          g,
                                     const ConsoleElement&    element,
                                     const KeyStateSnapshot&  keys,
                                     std::size_t              divisionSlot)
{
    const auto bounds = toRect(element.bounds);
    const int  low = element.lowNote, high = element.highNote;
    int whiteCount = 0;
    for (int k = low; k <= high; ++k)
        whiteCount += isWhiteKey(k) ? 1 : 0;
    whiteCount = juce::jmax(1, whiteCount);
    const float whiteW = bounds.getWidth() / static_cast<float>(whiteCount);

    // Ivory naturals with a soft top-lit gradient.
    float x = bounds.getX();
    for (int k = low; k <= high; ++k)
    {
        if (!isWhiteKey(k))
            continue;
        juce::Rectangle<float> key(x, bounds.getY(), whiteW, bounds.getHeight());
        const auto src = keys.get(divisionSlot, static_cast<core::MidiNote>(k));
        if (src != KeySource::Off)
        {
            g.setColour(litColour(tokens_, src));
            g.fillRect(key.reduced(0.5f));
        }
        else
        {
            juce::ColourGradient ivory(juce::Colours::white, key.getTopLeft(),
                                       juce::Colour(0xffe9e4d8), key.getBottomLeft(), false);
            g.setGradientFill(ivory);
            g.fillRect(key.reduced(0.5f));
        }
        g.setColour(toColour(tokens_.outline).withAlpha(0.5f));
        g.drawRect(key, 1.0f);
        x += whiteW;
    }

    // Ebony sharps.
    x = bounds.getX();
    const float blackW = whiteW * 0.6f, blackH = bounds.getHeight() * 0.62f;
    for (int k = low; k <= high; ++k)
    {
        if (isWhiteKey(k)) { x += whiteW; continue; }
        juce::Rectangle<float> key(x - blackW * 0.5f, bounds.getY(), blackW, blackH);
        const auto src = keys.get(divisionSlot, static_cast<core::MidiNote>(k));
        g.setColour(src != KeySource::Off ? litColour(tokens_, src) : juce::Colour(0xff141414));
        g.fillRoundedRectangle(key, 1.5f);
    }
}

void PhotorealConsole::paintCoupler(juce::Graphics&        g,
                                    const ConsoleElement&  element,
                                    const CouplerVisual&   visual)
{
    // A rocker tab that tilts when engaged.
    const auto bounds = toRect(element.bounds).reduced(1.0f);
    auto face = visual.engaged ? toColour(tokens_.accentActive) : toColour(tokens_.surfaceRaised);
    juce::ColourGradient grad(face.brighter(0.2f), bounds.getTopLeft(),
                              face.darker(0.2f),   bounds.getBottomLeft(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(juce::Font(juce::FontOptions{ 11.0f }));
    g.drawText(element.semantic.label, bounds, juce::Justification::centred, true);
    juce::ignoreUnused(visual.hovered);
}

void PhotorealConsole::paintMeter(juce::Graphics&                 g,
                                  const ConsoleElement&           element,
                                  const core::engine::MeterFrame& frame)
{
    // A recessed illuminated meter strip.
    const auto bounds = toRect(element.bounds);
    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillRoundedRectangle(bounds, tokens_.cornerRadius);
    const float level = juce::jlimit(0.0f, 1.0f, frame.peak);
    const auto colour = level > 0.85f ? toColour(tokens_.meterHigh)
                      : level > 0.6f  ? toColour(tokens_.meterMid)
                                      : toColour(tokens_.meterLow);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds.reduced(2.0f).withWidth((bounds.getWidth() - 4.0f) * level),
                           tokens_.cornerRadius);
}

void PhotorealConsole::paintWindGauge(juce::Graphics&        g,
                                      const ConsoleElement&  element,
                                      float                  pressurePa,
                                      float                  sagNorm)
{
    // A round pressure dial whose needle droops as the reservoir sags.
    const auto bounds = toRect(element.bounds);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.45f;

    g.setColour(toColour(tokens_.surfaceSunken));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    const float fillNorm = juce::jlimit(0.0f, 1.0f, 1.0f + sagNorm);
    const float angle = juce::MathConstants<float>::pi * (0.75f + 1.5f * fillNorm);
    juce::Path needle;
    needle.startNewSubPath(centre.x, centre.y);
    needle.lineTo(centre.x + radius * 0.85f * std::cos(angle),
                  centre.y + radius * 0.85f * std::sin(angle));
    g.setColour(toColour(tokens_.windGauge));
    g.strokePath(needle, juce::PathStrokeType(2.0f));
    juce::ignoreUnused(pressurePa);
}

} // namespace ceciliae::ui
