// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/plugin/ParameterLayout.h"

#include "caecilia/dsp/MasterEq.h"
#include "caecilia/model/Stop.h"

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

std::string ParameterLayout::couplerParamId(std::size_t index)
{
    return std::string{core::params::couplerParamId(index).c_str()};
}

std::string ParameterLayout::stopParamId(std::size_t index)
{
    // The formatting is core's, and constexpr, so the ID set can be enumerated at
    // compile time. This is the std::string the JUCE call sites want.
    return std::string{core::params::stopParamId(index).c_str()};
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

juce::AudioProcessorValueTreeState::ParameterLayout ParameterLayout::create(
    const model::Organ& organ, std::span<const core::StopId> defaultDrawn)
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

    // Default at half, not at zero. It was zero, so drawing the tremulant asked for
    // a modulation of nothing: the switch worked, the knob worked, and the
    // instrument did not move. See wind::kMaxTremulantDepthFraction for what 1.0
    // means in pascals.
    layout.add(makeFloat(kTremulantDepth, "Tremulant Depth",
                         juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.5f, ""));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kTemperament, kStateVersion}, "Temperament",
        temperamentChoices(), 0));

    layout.add(makeFloat(kTuningA4Hz, "Tuning A4",
                         juce::NormalisableRange<float>{400.0f, 470.0f, 0.1f}, 440.0f, "Hz"));

    // --- master EQ ----------------------------------------------------------
    //
    // The defaults are the FACTORY VOICING, not flat, and that is load-bearing in
    // two directions. Flat defaults would retune the instrument out of the box --
    // the organ has always shipped with this curve. And a document written before
    // these parameters existed carries no values for them, so restoring one leaves
    // the APVTS at exactly these defaults: flat would silently strip the voicing
    // from every session anyone has already saved.
    //
    // The range matches what dsp::MasterEq clamps to, so a host can never ask for
    // a gain the EQ will quietly refuse. The DEFAULTS are taken from the EQ's own
    // definition of the organ voicing rather than written out again here: two
    // copies of the same curve can drift, and the failure would be silent -- an
    // instrument that sounds one way out of the box and another after a reload.
    const dsp::MasterEq::Params voicing = dsp::MasterEq::organDefaults();

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kEqOn, kStateVersion}, "EQ", voicing.enabled));

    const juce::NormalisableRange<float> eqRange{-18.0f, 18.0f, 0.1f};
    static constexpr std::array<const char*, 5> kEqNames{
        "EQ Warmth", "EQ Boxiness", "EQ Body", "EQ Presence", "EQ Air"
    };
    for (std::size_t b = 0; b < kEqBandIds.size(); ++b)
        layout.add(makeFloat(kEqBandIds[b], kEqNames[b], eqRange,
                             voicing.bands[b].gainDb, "dB"));

    // --- boolean stop pool, slot index == StopId::value ---------------------
    //
    // JUCE forbids adding parameters after construction, so the pool is fixed at
    // kMaxStopParameters and the organ supplies only what it can: the display
    // names, and the defaults.
    //
    // Slot i IS the stop whose id is i. No binding step, no lookup table, nothing
    // to fall out of step -- which is why this loop indexes the organ by id rather
    // than walking its stop list in order.
    std::array<const model::Stop*, kMaxStopParameters> byId{};
    for (const model::Stop& s : organ.stops())
        if (s.id().value < kMaxStopParameters)
            byId[s.id().value] = &s;

    // The opening plenum becomes each slot's DEFAULT rather than something applied
    // after the fact. A host's "reset to default" then gives back a sounding
    // instrument, and a document that predates these parameters restores to a real
    // registration instead of to silence.
    std::array<bool, kMaxStopParameters> drawnByDefault{};
    for (const core::StopId id : defaultDrawn)
        if (id.value < kMaxStopParameters)
            drawnByDefault[id.value] = true;

    for (std::size_t i = 0; i < kMaxStopParameters; ++i)
    {
        const std::string id = stopParamId(i);

        // Slots past the end of the instrument are named for what they are. A host
        // lists every parameter it is given; "(unused)" is at least honest about
        // the ones that cannot be made audible.
        const juce::String name = byId[i] != nullptr
            ? juce::String(byId[i]->displayName())
            : juce::String("Stop ") + juce::String(static_cast<int>(i)) + " (unused)";

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{id, kStateVersion}, name, drawnByDefault[i]));
    }

    // --- couplers, AFTER the stop pool -------------------------------------
    //
    // Appended rather than inserted, so no stop ordinal moves and automation
    // written against this instrument keeps working. Safe to append here where it
    // was not safe for the EQ, because the stop pool's size is architectural now --
    // it is the width of the registration mask -- and cannot be resized again.
    //
    // Named from the organ, so a host shows "Récit/Grand-Orgue" rather than
    // "Coupler 0", and off by default: an organ opens uncoupled.
    for (std::size_t i = 0; i < kMaxCouplerParameters; ++i)
    {
        const std::string id = couplerParamId(i);

        const juce::String name = i < organ.couplers().size()
            ? juce::String(organ.couplers()[i].name())
            : juce::String("Coupler ") + juce::String(static_cast<int>(i)) + " (unused)";

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{id, kStateVersion}, name, false));
    }

    return layout;
}

} // namespace caecilia::plugin
