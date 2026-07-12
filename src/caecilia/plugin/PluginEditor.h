/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/plugin/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace caecilia::plugin
{

/**
 * @brief The plugin's @c juce::AudioProcessorEditor shell.
 *
 * The editor is deliberately thin: it hosts the console UI and polls the engine's
 * lock-free @ref MeterBridge at frame rate to animate live feedback (key levels,
 * wind sag, tremulant phase, voice count). The audio thread NEVER touches a
 * @c juce::Component — the editor only READS published snapshots on the message
 * thread.
 *
 * In this scaffold the editor draws a minimal status placeholder. The first-class
 * console pillar — @c ui::ConsoleView with its data-driven layout, pluggable
 * skins and registration palette — is embedded here in a later phase.
 * // TODO(phase0.2): mount ui::ConsoleView + StateMirror.
 * // TODO(phase0.9): PhotorealSkin / FlatVectorSkin + ThemeManager + zoom/pan.
 */
class CaeciliaEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CaeciliaEditor(CaeciliaAudioProcessor& processor);
    ~CaeciliaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override; ///< Frame-rate poll of the meter bridge.

    CaeciliaAudioProcessor& processor_;

    // Cached values pulled from the last poll, painted on the message thread.
    float        masterPeakDb_ = -100.0f;
    unsigned     activeVoices_ = 0;
    float        windSagNorm_  = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaeciliaEditor)
};

} // namespace caecilia::plugin
