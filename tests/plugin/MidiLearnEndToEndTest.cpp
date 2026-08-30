// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief Teaching a physical control to draw a stop, the whole way through.
 *
 * midi::LearnedControls and midi::MidiMap have had their own suites since they
 * were written, and they test the decision: given this event and this armed
 * state, capture, fire, swallow or play. What no test reached was the path -- the
 * audio thread pushing a packed event into a ring, an async wake, the message
 * thread installing the binding, and the registration that comes out the far end.
 * Every join in that chain was verified by reading it.
 *
 * These drive the real processor: arm it, push MIDI through processBlock, run the
 * work that queued, and ask whether the stop moved.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

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

/// Prepare the processor the way a host does.
///
/// prepareToPlay is the CALLBACK; a host calls setRateAndBufferSizeDetails first,
/// and that is what makes getSampleRate() answer. Calling only the callback leaves
/// the processor believing it has no sample rate -- which is not academic here,
/// because adoptOrgan asks getSampleRate() whether it is worth rebuilding the
/// engine, and silently did not.
void prepare(CaeciliaAudioProcessor& processor)
{
    processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    processor.prepareToPlay(kSampleRate, kBlock);
}


/// One block of audio carrying @p message, then the work it queued.
///
/// The learn and the fire both happen on the message thread, reached from the
/// audio thread through a ring and one async wake -- so a test that only calls
/// processBlock observes nothing at all. A host pumps its loop here; with modal
/// loops off there is no portable way to do that, so the processor's flush stands
/// in for it.
void playBlock(CaeciliaAudioProcessor& processor, const juce::MidiMessage& message)
{
    juce::AudioBuffer<float> audio(2, kBlock);
    audio.clear();

    juce::MidiBuffer midi;
    midi.addEvent(message, 0);

    processor.processBlock(audio, midi);
    processor.flushQueuedMidiActions();
}

juce::MidiMessage cc(int controller, int value, int channel = 1)
{
    return juce::MidiMessage::controllerEvent(channel, controller, value);
}

} // namespace

TEST_CASE("A control taught to draw a stop draws it", "[plugin][midi]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    const core::StopId stop{ 3 };

    processor.armMidiLearnStop(stop);
    REQUIRE(processor.midiLearnArmed());

    // The organist moves a tab on their console. That one gesture is the whole
    // binding: which control, and what it now does.
    playBlock(processor, cc(21, 127));

    CHECK_FALSE(processor.midiLearnArmed());
    REQUIRE(processor.midiMap().bindingCount() == 1);

    // And now it is a drawstop. The gesture that taught it must not also have
    // drawn it -- learning is not playing -- so the state before this is the
    // state to compare against.
    const bool before = processor.isStopEngaged(stop);
    playBlock(processor, cc(21, 0));
    playBlock(processor, cc(21, 127));
    CHECK(processor.isStopEngaged(stop) != before);
}

TEST_CASE("A learned control survives the session that saved it", "[plugin][midi]")
{
    JuceScope scope;

    juce::MemoryBlock session;
    {
        CaeciliaAudioProcessor first;
        prepare(first);
        first.armMidiLearnStop(core::StopId{ 3 });
        playBlock(first, cc(21, 127));
        REQUIRE(first.midiMap().bindingCount() == 1);

        juce::MemoryBlock block;
        first.getStateInformation(block);
        session = block;
    }

    // A console the organist has bound is part of the instrument, not part of the
    // performance: reopening the project must not ask them to teach it again.
    CaeciliaAudioProcessor second;
    second.setStateInformation(session.getData(), static_cast<int>(session.getSize()));

    REQUIRE(second.midiMap().bindingCount() == 1);

    const midi::MidiLearnBinding& b = second.midiMap().bindingAt(0);
    CHECK(b.isValid());
    CHECK(static_cast<int>(b.source.data1) == 21);

    // And it still works, which is the part a count cannot tell you.
    prepare(second);
    const bool before = second.isStopEngaged(core::StopId{ 3 });
    playBlock(second, cc(21, 0));
    playBlock(second, cc(21, 127));
    CHECK(second.isStopEngaged(core::StopId{ 3 }) != before);
}

TEST_CASE("Teaching a control twice leaves one binding", "[plugin][midi]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    processor.armMidiLearnStop(core::StopId{ 3 });
    playBlock(processor, cc(21, 127));
    REQUIRE(processor.midiMap().bindingCount() == 1);

    // The same tab, taught a different stop. One control does one thing: an
    // organist who re-teaches a tab expects it replaced, not doubled -- a console
    // where one tab draws two stops is a console nobody can reason about.
    processor.armMidiLearnStop(core::StopId{ 7 });
    playBlock(processor, cc(21, 0));
    playBlock(processor, cc(21, 127));

    CHECK(processor.midiMap().bindingCount() == 1);
}

TEST_CASE("A stop answers to one control", "[plugin][midi]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    processor.armMidiLearnStop(core::StopId{ 3 });
    playBlock(processor, cc(21, 127));
    REQUIRE(processor.midiMap().bindingCount() == 1);

    // The other direction from re-teaching a tab: the same STOP, taught to a
    // second tab. installBinding matches on the control, so it would happily leave
    // both -- and the organist would have a stop answering to a tab they had
    // forgotten they bound, which is worse than one that answers to none.
    processor.armMidiLearnStop(core::StopId{ 3 });
    playBlock(processor, cc(22, 127));

    CHECK(processor.midiMap().bindingCount() == 1);
    CHECK(static_cast<int>(processor.midiMap().bindingAt(0).source.data1) == 22);
}

/// Peak magnitude over @p blocks of rendering, starting with @p message.
///
/// A pipe does not speak instantly -- there is an attack, and a single 256-frame
/// block at 48 kHz is five milliseconds, which is inside it. Rendering on lets the
/// voice actually arrive before the silence is believed.
float peakAfter(CaeciliaAudioProcessor& processor, const juce::MidiMessage& message,
                int blocks = 24)
{
    juce::AudioBuffer<float> audio(2, kBlock);
    float peak = 0.0f;

    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi;
        if (i == 0)
            midi.addEvent(message, 0);

        processor.processBlock(audio, midi);
        processor.flushQueuedMidiActions();
        peak = juce::jmax(peak, audio.getMagnitude(0, kBlock));
    }
    return peak;
}

TEST_CASE("A tab bound to a key does not also sound its pipe", "[plugin][midi]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor);

    // Many consoles send NOTES for their stop tabs, from a keyboard the organist
    // has set aside for the job. That is the case that matters here: a controller
    // number nothing else listens to is indistinguishable whether it is swallowed
    // or not, and a test that cannot tell the difference is not a test.

    // First, that this instrument speaks at all when asked, so the silence below
    // means something. Without this the test would pass on a processor that never
    // makes a sound.
    REQUIRE(peakAfter(processor, juce::MidiMessage::noteOn(1, 60, 0.8f)) > 0.0f);

    CaeciliaAudioProcessor bound;
    prepare(bound);
    bound.armMidiLearnStop(core::StopId{ 3 });
    playBlock(bound, juce::MidiMessage::noteOn(1, 41, 0.8f));
    REQUIRE(bound.midiMap().bindingCount() == 1);

    // And now that key is a drawstop. It draws, and it is silent: the console
    // swallowed it, so the pipe it would otherwise have sounded stays shut.
    const bool before = bound.isStopEngaged(core::StopId{ 3 });
    const float peak  = peakAfter(bound, juce::MidiMessage::noteOn(1, 41, 0.8f));

    CHECK(bound.isStopEngaged(core::StopId{ 3 }) != before);
    CHECK(peak == 0.0f);
}
