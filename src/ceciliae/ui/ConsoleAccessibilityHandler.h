/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/ui/ConsoleLayoutModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * @file ConsoleAccessibilityHandler.h
 * @brief Turns a console element's ONE semantic identity into screen-reader
 *        descriptions, so accessibility is derived from the same fact as the
 *        tooltip, the OSC address and the MIDI-learn target.
 *
 * Because the @c ConsoleView paints every element itself (there are no per-key
 * sub-components), accessibility cannot come "for free" from child components;
 * this helper is the bridge. It maps an @c ElementRole to the appropriate
 * @c juce::AccessibilityRole and composes a spoken title/value from the
 * @c SemanticId. A later phase wires these into a virtual accessibility tree the
 * @c ConsoleView exposes to assistive technology.
 */

namespace ceciliae::ui
{

/// A resolved accessible description for one element.
struct AccessibleDescription
{
    juce::String            title;                                   ///< Spoken name.
    juce::String            value;                                   ///< Spoken state ("drawn"/"off").
    juce::AccessibilityRole role = juce::AccessibilityRole::button;  ///< AT role.
};

class ConsoleAccessibilityHandler
{
public:
    ConsoleAccessibilityHandler() = default;

    /// Map an element role to the closest assistive-technology role.
    [[nodiscard]] static juce::AccessibilityRole roleFor(ElementRole role) noexcept;

    /// Compose the spoken description for @p element given its live @p engaged
    /// state (ignored for non-toggle elements).
    [[nodiscard]] static AccessibleDescription describe(const ConsoleElement& element, bool engaged);

    // TODO(v0.9): build a juce::AccessibilityHandler virtual tree over the whole
    // ConsoleView so each element becomes an addressable AT node, and keep the
    // focused-node value in sync with the StateMirror.
};

} // namespace ceciliae::ui
