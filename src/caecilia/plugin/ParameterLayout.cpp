/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/ParameterLayout.h"

#include <cstdio>
#include <memory>

namespace caecilia::plugin
{

namespace
{

/// A small helper to keep the float-parameter construction readable.
std::unique_ptr<juce::AudioParameterFloat> makeFloat(const char*  id,
                                                     const char*  name,
                                                     juce::NormalisableRange<float> range,
                                                     float        defaultValue,
                                                     const char*  unitSuffix)
{
    juce::AudioParameterFloatAttributes attrs;
    attrs = attrs.withLabel(unitSuffix);
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, ParameterLayout::kStateVersion}, name, range, defaultValue, attrs);
}

} // namespace

std::string ParameterLayout::stopParamId(std::size_t index)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "stop_%03zu", index);
    return std::string{buffer};
}

juce::StringArray ParameterLayout::temperamentChoices()
{
    // Index-aligned with core::TemperamentId so a stored choice maps 1:1.
    return juce::StringArray{"Equal",
                             "Quarter-comma Meantone",
                             "Werckmeister III",
                             "Kirnberger III",
                             "Pythagorean",
                             "Young",
                             "Custom"};
}

core::TemperamentId ParameterLayout::temperamentFromChoice(int choiceIndex) noexcept
{
    constexpr int count = static_cast<int>(core::TemperamentId::Custom) + 1;
    if (choiceIndex < 0)
        choiceIndex = 0;
    if (choiceIndex >= count)
        choiceIndex = count - 1;
    return static_cast<core::TemperamentId>(choiceIndex);
}

juce::AudioProcessorValueTreeState::ParameterLayout ParameterLayout::create()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- global master / reverb / tremulant / tuning ------------------------

    layout.add(makeFloat(kMasterGainDb, "Master Gain",
                         juce::NormalisableRange<float>{-60.0f, 6.0f, 0.1f}, 0.0f, "dB"));

    layout.add(makeFloat(kReverbMix, "Reverb Mix",
                         juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.25f, ""));

    layout.add(makeFloat(kReverbDecaySec, "Reverb Decay",
                         juce::NormalisableRange<float>{0.2f, 12.0f, 0.01f, 0.4f}, 2.5f, "s"));

    layout.add(makeFloat(kReverbPreDelayMs, "Reverb Pre-Delay",
                         juce::NormalisableRange<float>{0.0f, 120.0f, 0.1f}, 12.0f, "ms"));

    layout.add(makeFloat(kReverbDampingHz, "Reverb Damping",
                         juce::NormalisableRange<float>{500.0f, 20000.0f, 1.0f, 0.3f}, 6000.0f, "Hz"));

    layout.add(makeFloat(kReverbWidth, "Reverb Width",
                         juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 1.0f, ""));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kTremulantOn, kStateVersion}, "Tremulant", false));

    layout.add(makeFloat(kTremulantRateHz, "Tremulant Rate",
                         juce::NormalisableRange<float>{2.0f, 10.0f, 0.01f}, 5.5f, "Hz"));

    layout.add(makeFloat(kTremulantDepth, "Tremulant Depth",
                         juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.0f, ""));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kTemperament, kStateVersion}, "Temperament",
        temperamentChoices(), 0));

    layout.add(makeFloat(kTuningA4Hz, "Tuning A4",
                         juce::NormalisableRange<float>{400.0f, 470.0f, 0.1f}, 440.0f, "Hz"));

    // --- reserved boolean stop pool (bound to concrete stops at organ load) --
    //
    // JUCE forbids adding parameters after construction, so we pre-declare a
    // fixed pool. Slots are inert until CaeciliaParameterMirror::bindOrgan maps
    // them onto a loaded organ's stops. Names get overwritten at bind time.
    for (std::size_t i = 0; i < kMaxStopParameters; ++i)
    {
        const std::string id   = stopParamId(i);
        const juce::String name = "Stop " + juce::String(static_cast<int>(i) + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{id, kStateVersion}, name, false));
    }

    return layout;
}

} // namespace caecilia::plugin
