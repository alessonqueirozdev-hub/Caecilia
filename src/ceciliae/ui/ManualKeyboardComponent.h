/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/ui/StateMirror.h"
#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
 * @file ManualKeyboardComponent.h
 * @brief A standalone, reusable keyboard whose keys light up LIVE as MIDI plays.
 *
 * Keys are tinted by why they sound — blue when played on this manual, red when
 * coupled in, purple when driven by a combination/sequencer — exactly as a real
 * illuminated console does. This is the reference widget for the keyboard visual;
 * @c ConsoleView renders the same illumination inline without sub-components for
 * the high-performance unified path, but this component is handy for panels,
 * previews and tests.
 *
 * It polls its bound @c StateMirror on a timer and repaints only when the lit set
 * changes. Mouse clicks emit note on/off through @ref onKeyEvent.
 */

namespace ceciliae::ui
{

class ManualKeyboardComponent final : public juce::Component,
                                      private juce::Timer
{
public:
    ManualKeyboardComponent();
    ~ManualKeyboardComponent() override;

    /// Which division's lit state (slot) this keyboard mirrors.
    void setDivision(core::DivisionId division, std::size_t mirrorSlot) noexcept;

    /// Set the playable compass (inclusive MIDI notes).
    void setCompass(core::MidiNote low, core::MidiNote high) noexcept;

    /// Bind the live audio->UI state mirror (not owned).
    void setStateMirror(const StateMirror* mirror) noexcept { mirror_ = mirror; }

    /// Adopt theme tokens for key colours.
    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    /// Fired for mouse-played notes: (note, isDown).
    std::function<void(core::MidiNote, bool)> onKeyEvent;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    [[nodiscard]] core::MidiNote noteAt(juce::Point<int> p) const noexcept;

    const StateMirror* mirror_ = nullptr;
    ThemeTokens        tokens_{};
    core::DivisionId   division_{};
    std::size_t        slot_    = 0;
    core::MidiNote     low_     = 36;
    core::MidiNote     high_    = 96;

    KeyStateSnapshot lastKeys_{};   ///< Last polled lit set (change detection).
    core::MidiNote   heldNote_ = 0;
    bool             holding_  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ManualKeyboardComponent)
};

} // namespace ceciliae::ui
