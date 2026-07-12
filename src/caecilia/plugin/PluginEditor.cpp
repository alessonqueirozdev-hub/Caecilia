/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginEditor.h"

#include "caecilia/ui/ThemeManager.h"

namespace caecilia::plugin
{

namespace
{
constexpr int   kDefaultWidth  = 1280;
constexpr int   kDefaultHeight = 800;

// The console is laid out in a fixed logical canvas; the ViewportController then
// scales it to fill whatever size the host gives the editor (16:10 like the
// approved mockup).
constexpr float kCanvasWidth  = 1600.0f;
constexpr float kCanvasHeight = 1000.0f;
} // namespace

CaeciliaEditor::CaeciliaEditor(CaeciliaAudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , processor_(processor)
{
    // Dress the console in the photoreal (wood/ivory) skin and feed it the live
    // audio->UI mirror so meters animate and keys light while playing.
    console_.applyTheme(ui::makeDefaultPhotoreal());
    console_.setConsoleMode(ui::SkinId::Photoreal);
    console_.setStateMirror(&processor_.stateMirror());

    // Semantic interactions route straight back into the processor.
    console_.stopEngagedQuery = [this](core::StopId s) { return processor_.isStopEngaged(s); };
    console_.couplerEngagedQuery = [](model::CouplerId) { return false; }; // TODO(v0.5): couplers
    console_.onDrawstopClicked = [this](core::StopId s) { processor_.toggleStop(s); };
    console_.onKeyEvent = [this](core::DivisionId d, core::MidiNote n, bool down)
    {
        processor_.uiNote(d, n, down);
    };

    addAndMakeVisible(console_);

    relayout();

    setResizable(true, true);
    setResizeLimits(720, 450, 4096, 2560);
    setSize(kDefaultWidth, kDefaultHeight);
}

CaeciliaEditor::~CaeciliaEditor() = default;

void CaeciliaEditor::relayout()
{
    layout_.layoutFrom(processor_.organ(),
                       { 0.0f, 0.0f, kCanvasWidth, kCanvasHeight });
    console_.setLayoutModel(&layout_);
}

void CaeciliaEditor::paint(juce::Graphics& g)
{
    // The console paints edge to edge; this only shows through before it is sized.
    g.fillAll(juce::Colour(0xff17130d));
}

void CaeciliaEditor::resized()
{
    console_.setBounds(getLocalBounds());
}

} // namespace caecilia::plugin
