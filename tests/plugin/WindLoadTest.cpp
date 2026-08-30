// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Does the wind respond to what is actually being played?
 *
 * The wind model is tested thoroughly as a model: the reservoir sags toward a
 * demand-dependent equilibrium, sag rises monotonically with demand, it recovers
 * when the load goes, it rings at the frequency it was given, and the tremulant
 * reaches the pipes as a measured modulation of the rendered envelope.
 *
 * All of that is measured on the wind model with demand handed to it directly.
 * What none of it asks is whether a KEY reaches the wind -- whether pressing more
 * of them, through MIDI, through the channel map, through the registration and the
 * scheduler and the per-chest flow booking, actually loads the reservoir. This
 * session has found that question worth asking more than once.
 *
 * --- and what is NOT asserted here, deliberately
 *
 * The thing an organist notices first is the PITCH dipping under a full chord: a
 * pipe on less pressure speaks flatter. That is not asserted, because it cannot
 * honestly be measured here yet. The sag this organ produces under eight extra
 * keys is 0.14% of its pressure, and the pitch change that follows is smaller than
 * the noise floor of a measurement taken through the master limiter and the
 * reverb. An attempt to assert it measured +0.25 cents -- the wrong sign, and
 * almost certainly not the wind at all.
 *
 * Whether 0.14% is the RIGHT sag for eight added keys is an open question about
 * the reservoir's parameters. It wants a reference figure from a real instrument,
 * not a number chosen to make an assertion pass.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <vector>

using namespace caecilia;
using caecilia::plugin::CaeciliaAudioProcessor;

namespace
{

struct JuceScope
{
    juce::ScopedJuceInitialiser_GUI gui;
};

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 256;
constexpr int    kHeld       = 60;

void prepare(CaeciliaAudioProcessor& processor)
{
    processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    processor.prepareToPlay(kSampleRate, kBlock);
}

void render(CaeciliaAudioProcessor& processor, int blocks,
            const juce::MidiBuffer& opening = {})
{
    juce::AudioBuffer<float> audio(2, kBlock);
    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi = (i == 0) ? opening : juce::MidiBuffer{};
        processor.processBlock(audio, midi);
    }
}

juce::MidiBuffer notesOn(const std::vector<int>& notes)
{
    juce::MidiBuffer midi;
    for (const int n : notes)
        midi.addEvent(juce::MidiMessage::noteOn(1, n, 0.9f), 0);
    return midi;
}

/// Draw the whole organ, so every key costs the instrument's worth of wind.
void drawEverything(CaeciliaAudioProcessor& processor)
{
    for (const model::Stop& s : processor.organ().stops())
        if (! processor.isStopEngaged(s.id()))
            processor.toggleStop(s.id());
}

struct Reading
{
    std::size_t voices = 0;
    float       sag    = 0.0f;
};

/// Hold middle C, add @p load, let the reservoir settle, and read the wind.
///
/// One processor per call, driven identically, so two calls differ only in their
/// load and everything that is a function of time alone cancels between them.
Reading windUnder(const std::vector<int>& load)
{
    CaeciliaAudioProcessor processor;
    prepare(processor);
    drawEverything(processor);

    render(processor, 8, notesOn({ kHeld }));
    render(processor, 40);
    render(processor, 8, load.empty() ? juce::MidiBuffer{} : notesOn(load));
    render(processor, 60);   // the reservoir reaches its new level

    const auto m = processor.meters().snapshot();
    return { m.activeVoices, m.windSagNorm };
}

const std::vector<int> kEightMore = { 72, 76, 79, 83, 86, 89, 91, 93 };

} // namespace

TEST_CASE("Pressing keys loads the wind", "[plugin][wind]")
{
    JuceScope scope;

    const Reading alone  = windUnder({});
    const Reading loaded = windUnder(kEightMore);

    INFO("alone: " << alone.voices << " voices, sag " << alone.sag
         << "   loaded: " << loaded.voices << " voices, sag " << loaded.sag);

    // The keys reached the pipes and the pipes reached the wind. Every join in
    // that chain is exercised by this one comparison, and any of them going
    // missing leaves the two readings equal.
    REQUIRE(loaded.voices > alone.voices * 4);
    CHECK(alone.sag  < 0.0f);
    CHECK(loaded.sag < alone.sag);
}

TEST_CASE("More keys is more load", "[plugin][wind]")
{
    JuceScope scope;

    // Monotone, not merely different. A wind that answered the FIRST extra key and
    // then saturated would pass the test above and would not be a wind model.
    const float none = windUnder({}).sag;
    const float some = windUnder({ 72, 76 }).sag;
    const float more = windUnder(kEightMore).sag;

    INFO("sag with 0 / 2 / 8 extra keys: " << none << " / " << some << " / " << more);

    CHECK(some < none);
    CHECK(more < some);
}

TEST_CASE("Letting the keys go gives the wind back", "[plugin][wind]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);
    drawEverything(processor);

    render(processor, 8, notesOn({ kHeld }));
    render(processor, 40);
    render(processor, 8, notesOn(kEightMore));
    render(processor, 60);
    const float underLoad = processor.meters().snapshot().windSagNorm;

    juce::MidiBuffer off;
    for (const int n : kEightMore)
        off.addEvent(juce::MidiMessage::noteOff(1, n), 0);

    // The reservoir is a mass on a spring, so it comes back with a settling time
    // rather than as a step -- which is what makes the recovery audible as a swell
    // rather than a click. Two seconds is comfortably past it.
    render(processor, 380, off);
    const float after = processor.meters().snapshot().windSagNorm;

    INFO("sag under load " << underLoad << ", after release " << after);

    // A wind that sagged and never recovered would leave an organ going
    // progressively flat through a piece.
    CHECK(after > underLoad);
}

TEST_CASE("PROBE: how far the wind actually falls", "[.windmagnitude]")
{
    JuceScope scope;

    struct Case { const char* what; std::vector<int> keys; };
    for (const Case& c : std::vector<Case>{
             { "1 key",   {} },
             { "3 keys",  { 64, 67 } },
             { "9 keys",  { 64, 67, 72, 76, 79, 83, 86, 89 } },
             { "13 keys", { 48, 55, 64, 67, 72, 76, 79, 83, 86, 89, 91, 93 } } })
    {
        CaeciliaAudioProcessor processor;
        prepare(processor);
        drawEverything(processor);
        render(processor, 8, notesOn({ kHeld }));
        render(processor, 40);
        render(processor, 8, c.keys.empty() ? juce::MidiBuffer{} : notesOn(c.keys));
        render(processor, 90);

        const auto m = processor.meters().snapshot();
        WARN(std::string(c.what) + "  voices " + std::to_string(m.activeVoices)
             + "  pressure " + std::to_string(m.windPressurePa)
             + "  sag " + std::to_string(100.0f * m.windSagNorm) + "%");
    }
}
