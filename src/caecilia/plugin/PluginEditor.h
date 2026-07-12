/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/plugin/PluginProcessor.h"
#include "caecilia/ui/ConsoleLayoutModel.h"
#include "caecilia/ui/ConsoleView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace caecilia::plugin
{

/**
 * @brief The plugin's @c juce::AudioProcessorEditor — the first-class console.
 *
 * The editor owns the data-driven @c ui::ConsoleView, lays it out from the
 * processor's compiled @c model::Organ, dresses it in the photoreal theme and
 * feeds it the processor's lock-free @c ui::StateMirror so meters animate and
 * keys light while playing. Every interaction is a semantic callback back into
 * the processor: drawstops toggle the registration, on-screen keys inject notes.
 *
 * The audio thread NEVER touches a @c juce::Component; the console only READS
 * published snapshots on the message thread (via the ConsoleView's own vblank).
 */
class CaeciliaEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CaeciliaEditor(CaeciliaAudioProcessor& processor);
    ~CaeciliaEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    /// (Re)build the console layout for the current component size and re-fit.
    void relayout();

    CaeciliaAudioProcessor& processor_;

    ui::ConsoleLayoutModel  layout_;
    ui::ConsoleView         console_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaeciliaEditor)
};

} // namespace caecilia::plugin
