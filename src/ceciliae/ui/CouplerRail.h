/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/model/Coupler.h"
#include "ceciliae/ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

/**
 * @file CouplerRail.h
 * @brief A horizontal rail of coupler tabs (Swell to Great, octave couplers, ...).
 *
 * Couplers are first-class in registration, so they get their own dedicated rail
 * rather than being buried among the drawstops. Each tab reflects its engaged
 * state and toggles through @ref onToggle. The engaged truth is supplied by the
 * host via @ref setCouplers so the rail never owns registration state.
 */

namespace ceciliae::ui
{

class CouplerRail final : public juce::Component
{
public:
    /// One tab's static description; the engaged flag is refreshed by the host.
    struct Entry
    {
        model::CouplerId id{};
        std::string      label;
        bool             engaged = false;
    };

    CouplerRail() = default;

    /// Replace the set of couplers shown. Triggers a relayout + repaint.
    void setCouplers(std::vector<Entry> entries);

    /// Update just the engaged flags (host polls registration and pushes here).
    void setEngaged(model::CouplerId id, bool engaged);

    void setThemeTokens(const ThemeTokens& tokens) { tokens_ = tokens; repaint(); }

    /// Fired when a tab is clicked (toggle request).
    std::function<void(model::CouplerId)> onToggle;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    [[nodiscard]] int indexAt(juce::Point<int> p) const noexcept;

    std::vector<Entry> entries_;
    ThemeTokens        tokens_{};
    float              tabWidth_ = 96.0f;
    float              gap_      = 8.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CouplerRail)
};

} // namespace ceciliae::ui
