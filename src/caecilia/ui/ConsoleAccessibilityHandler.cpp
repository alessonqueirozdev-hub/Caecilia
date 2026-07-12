/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/ui/ConsoleAccessibilityHandler.h"

namespace caecilia::ui
{

juce::AccessibilityRole ConsoleAccessibilityHandler::roleFor(ElementRole role) noexcept
{
    switch (role)
    {
        case ElementRole::Drawstop:
        case ElementRole::Coupler:
            return juce::AccessibilityRole::toggleButton;
        case ElementRole::Piston:
        case ElementRole::SequencerButton:
            return juce::AccessibilityRole::button;
        case ElementRole::ExpressionPedal:
            return juce::AccessibilityRole::slider;
        case ElementRole::WindGauge:
        case ElementRole::VuMeter:
            return juce::AccessibilityRole::label;
        case ElementRole::Manual:
        case ElementRole::Pedalboard:
            return juce::AccessibilityRole::group;
        case ElementRole::Jamb:
        case ElementRole::Label:
        case ElementRole::Unknown:
        default:
            return juce::AccessibilityRole::ignored;
    }
}

AccessibleDescription ConsoleAccessibilityHandler::describe(const ConsoleElement& element, bool engaged)
{
    AccessibleDescription d;
    d.role  = roleFor(element.role);
    d.title = juce::String(element.semantic.label);

    switch (element.role)
    {
        case ElementRole::Drawstop:
        case ElementRole::Coupler:
            d.value = engaged ? "drawn" : "off";
            break;
        default:
            d.value = {};
            break;
    }
    return d;
}

} // namespace caecilia::ui
