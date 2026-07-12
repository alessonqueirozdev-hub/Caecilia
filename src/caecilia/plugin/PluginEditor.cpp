/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginEditor.h"

#include "caecilia/core/Version.h"

namespace caecilia::plugin
{

namespace
{
constexpr int   kDefaultWidth  = 720;
constexpr int   kDefaultHeight = 480;
constexpr int   kRefreshHz     = 30;
} // namespace

CaeciliaEditor::CaeciliaEditor(CaeciliaAudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , processor_(processor)
{
    setResizable(true, true);
    setResizeLimits(480, 320, 4096, 2560);
    setSize(kDefaultWidth, kDefaultHeight);
    startTimerHz(kRefreshHz);
}

CaeciliaEditor::~CaeciliaEditor()
{
    stopTimer();
}

void CaeciliaEditor::timerCallback()
{
    // Poll the lock-free metering bridge on the message thread; never touch the
    // audio thread. Cache into painted fields and request a repaint.
    const auto snap = processor_.meters().snapshot();
    masterPeakDb_ = MeterBridge::linearToDb(snap.master.peak, -100.0f);
    activeVoices_ = snap.activeVoices;
    windSagNorm_  = snap.windSagNorm;
    repaint();
}

void CaeciliaEditor::paint(juce::Graphics& g)
{
    // Placeholder chrome. // TODO(phase0.2): replace with ui::ConsoleView driven by
    // the OrganSpec + StateMirror; this shell only proves the meter/UI seam.
    g.fillAll(juce::Colour(0xff1b1b20));

    auto bounds = getLocalBounds().reduced(24);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("Caecilia", bounds.removeFromTop(48), juce::Justification::topLeft);

    g.setColour(juce::Colour(0xff8a8a99));
    g.setFont(juce::Font(14.0f));
    g.drawText(juce::String("core ") + core::versionString(),
               bounds.removeFromTop(24), juce::Justification::topLeft);

    bounds.removeFromTop(16);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f));
    const juce::String status =
        juce::String::formatted("Voices: %u    Master: %.1f dB    Wind sag: %+.3f",
                                activeVoices_, masterPeakDb_, windSagNorm_);
    g.drawText(status, bounds.removeFromTop(28), juce::Justification::topLeft);
}

void CaeciliaEditor::resized()
{
    // TODO(phase0.2): lay out the embedded ConsoleView + registration palette.
}

} // namespace caecilia::plugin
