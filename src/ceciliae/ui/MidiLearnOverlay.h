/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/ConsoleLayoutModel.h"
#include "ceciliae/ui/Theme.h"
#include "ceciliae/ui/ViewportController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/**
 * @file MidiLearnOverlay.h
 * @brief A transparent overlay that, in learn mode, badges every learnable
 *        console element and lets the user pick one as a MIDI-learn target.
 *
 * The overlay sits above the @c ConsoleView and shares its layout model and
 * viewport, so a badge lands exactly on its element under any zoom/pan. Choosing
 * a badge emits the element's SEMANTIC identity — the same fact the tooltip, the
 * OSC address and the screen reader use — so a learned control carries identical
 * meaning to a scripted or clicked one. When inactive it is fully transparent and
 * passes clicks straight through to the console.
 */

namespace ceciliae::ui
{

class MidiLearnOverlay final : public juce::Component
{
public:
    MidiLearnOverlay();

    /// Share the console's layout (not owned) and viewport (not owned).
    void bind(const ConsoleLayoutModel* model, const ViewportController* viewport) noexcept;

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    /// Enter/leave learn mode. When inactive the overlay is click-through.
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    /// The element currently armed to receive the next MIDI event (if any).
    void setArmed(const SemanticId* armed);

    /// Fired when the user picks an element to learn.
    std::function<void(const SemanticId&)> onElementChosen;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool hitTest(int x, int y) override;

private:
    const ConsoleLayoutModel* model_    = nullptr;
    const ViewportController*  viewport_ = nullptr;
    ThemeTokens                tokens_{};
    bool                       active_   = false;
    SemanticId                 armed_{};
    bool                       hasArmed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnOverlay)
};

} // namespace ceciliae::ui
