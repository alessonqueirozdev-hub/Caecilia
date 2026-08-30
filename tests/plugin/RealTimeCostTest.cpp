// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief What the whole instrument costs, playing the way an organist plays.
 *
 * caecilia-bench measures voices in isolation -- one composite voice per note, up
 * to thirty-two of them -- which is the right shape for tracking the synthesis
 * cost but is not what the instrument does. A full organ with both hands and the
 * feet draws every rank against every key: twenty-six stops under a ten-note
 * chord and three pedal notes is some three hundred voices, through the real
 * scheduler, the real wind, the real reverb and the real limiter.
 *
 * Hidden by default (the leading dot in the tag), because the number is a
 * property of the machine and a CI runner is not a machine anyone plays on. Run
 * it on purpose:
 *
 *     caecilia_plugin_tests "[.realtime]" -s
 *
 * The assertion is deliberately loose. It is not a performance target -- it is a
 * tripwire for the day something makes this ten times slower.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <chrono>

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

void prepare(CaeciliaAudioProcessor& processor)
{
    processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    processor.prepareToPlay(kSampleRate, kBlock);
}

/// @return the fraction of one core this took, at real time.
double realTimeFactor(CaeciliaAudioProcessor& processor, const juce::MidiBuffer& opening,
                      int blocks)
{
    juce::AudioBuffer<float> audio(2, kBlock);

    // Let the notes speak and the wind settle before the clock starts: the attack
    // and the pool's first fill are a one-off, and what an organist holds is the
    // steady state.
    for (int i = 0; i < 64; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi = (i == 0) ? opening : juce::MidiBuffer{};
        processor.processBlock(audio, midi);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer none;
        processor.processBlock(audio, none);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double audioSeconds = (blocks * static_cast<double>(kBlock)) / kSampleRate;
    return seconds / audioSeconds;
}

} // namespace

TEST_CASE("What the full organ costs under both hands and the feet", "[.realtime]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // Everything the organ has. Not a plenum, not a tutti piston -- every stop,
    // which is the worst case an organist can actually reach.
    for (const model::Stop& s : processor.organ().stops())
        if (! processor.isStopEngaged(s.id()))
            processor.toggleStop(s.id());

    juce::MidiBuffer midi;
    // A ten-note chord across two hands, on the manual.
    for (const int note : { 48, 55, 60, 64, 67, 72, 76, 79, 84, 88 })
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
    // And the feet.
    for (const int note : { 36, 43, 48 })
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);

    const double factor = realTimeFactor(processor, midi, 2000); // ~10.7 s of audio

    // The voice count with it, and not as decoration: the scheduler is allowed to
    // shed voices it cannot afford, so a cost figure without the voices it bought
    // could be measuring an instrument that quietly stopped playing most of the
    // registration.
    const std::size_t voices = processor.meters().snapshot().activeVoices;

    WARN("full organ, 13 keys, " << processor.organ().stops().size()
         << " stops -> " << voices << " voices at " << factor
         << "x of one core, real time");

    // If shedding were doing the work, this would be a fraction of the ranks the
    // registration asked for rather than a multiple of the keys held.
    CHECK(voices > 100);

    // Not a target. A tripwire: if this ever needs more than one core to hold a
    // chord, the instrument has stopped being playable and the number that says
    // so should be in a test rather than in a user's report.
    CHECK(factor < 1.0);
}

TEST_CASE("What one stop costs under one hand", "[.realtime]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // The other end of the range, for scale: the registration a practising
    // organist spends most of their time in.
    for (const model::Stop& s : processor.organ().stops())
        if (processor.isStopEngaged(s.id()))
            processor.toggleStop(s.id());

    // On the division these keys reach. The first stop of the organ belongs to the
    // Pédale, and a manual key does not sound it -- which measured the cost of
    // silence and read like a very fast instrument.
    for (const model::Stop& s : processor.organ().stops())
        if (s.division() == processor.playDivision())
        {
            processor.toggleStop(s.id());
            break;
        }

    juce::MidiBuffer midi;
    for (const int note : { 60, 64, 67, 72 })
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);

    const double factor = realTimeFactor(processor, midi, 2000);
    const std::size_t voices = processor.meters().snapshot().activeVoices;

    WARN("one stop, 4 keys -> " << voices << " voices at " << factor
         << "x of one core, real time");

    // Sounding at all. Without this the number below is the cost of silence.
    CHECK(voices > 0);
    CHECK(factor < 1.0);
}
