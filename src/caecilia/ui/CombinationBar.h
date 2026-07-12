/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

/**
 * @file CombinationBar.h
 * @brief The combination / sequencer bar: general pistons plus the stepper that
 *        walks the combination sequence (Set / Previous / Next).
 *
 * It shows the currently-selected combination and the pending one (so the player
 * can see where a Next will land), mirroring the user's real workflow where
 * si5/do6 are mapped to sequencer Previous/Next. Piston presses and stepper
 * moves are emitted as callbacks; the bar owns no registration state.
 */

namespace caecilia::ui
{

class CombinationBar final : public juce::Component
{
public:
    CombinationBar() = default;

    /// Number of general pistons to show (1..N).
    void setPistonCount(int count);

    /// Set the sequencer position: current step and total steps, plus whether a
    /// pending (armed-but-not-recalled) step is shown highlighted.
    void setSequencerState(int currentStep, int totalSteps, int pendingStep = -1);

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    // --- Callbacks ----------------------------------------------------------
    std::function<void(std::uint16_t /*1-based piston*/)> onPiston;
    std::function<void(int /*+1 next, -1 previous*/)>     onSequencerStep;
    std::function<void()>                                 onSet; ///< Capture into current step.

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    enum class Hit { None, Piston, Prev, Next, Set };

    struct HitResult { Hit kind = Hit::None; std::uint16_t piston = 0; };
    [[nodiscard]] HitResult hitAt(juce::Point<int> p) const noexcept;

    ThemeTokens tokens_{};
    int         pistonCount_ = 8;
    int         currentStep_ = 0;
    int         totalSteps_  = 0;
    int         pendingStep_ = -1;

    juce::Rectangle<int> pistonArea_;
    juce::Rectangle<int> prevButton_;
    juce::Rectangle<int> setButton_;
    juce::Rectangle<int> nextButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombinationBar)
};

} // namespace caecilia::ui
