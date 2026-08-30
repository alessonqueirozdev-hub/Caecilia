// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief The things a host is allowed to do that a plugin has to survive.
 *
 * A host is not a well-behaved caller. It probes a plugin before preparing it, it
 * changes the sample rate and the buffer size while notes are held, and it is
 * permitted to hand over a block larger than the one it announced -- an offline
 * bounce routinely does. Each of those meets code that was sized for something
 * else, and the failure is a crash in a customer's session rather than a red test.
 *
 * The engine handles all four already and says so in its own comments. What was
 * missing is anything that would notice if that stopped being true.
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

/// Prepare the way a host does: the details first, then the callback.
void prepare(CaeciliaAudioProcessor& processor, double rate = kSampleRate, int block = kBlock)
{
    processor.setRateAndBufferSizeDetails(rate, block);
    processor.prepareToPlay(rate, block);
}

/// Render @p blocks of @p frames, opening with @p message; return the loudest.
float render(CaeciliaAudioProcessor& processor, int blocks, int frames = kBlock,
             const juce::MidiMessage* message = nullptr)
{
    juce::AudioBuffer<float> audio(2, frames);
    float peak = 0.0f;

    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi;
        if (i == 0 && message != nullptr)
            midi.addEvent(*message, 0);

        processor.processBlock(audio, midi);
        peak = juce::jmax(peak, audio.getMagnitude(0, frames));
    }
    return peak;
}

/// The magnitude of the LAST block only, which is where a tail is measured.
float tailAfter(CaeciliaAudioProcessor& processor, int blocks,
                const juce::MidiMessage* message = nullptr)
{
    juce::AudioBuffer<float> audio(2, kBlock);

    for (int i = 0; i < blocks; ++i)
    {
        audio.clear();
        juce::MidiBuffer midi;
        if (i == 0 && message != nullptr)
            midi.addEvent(*message, 0);
        processor.processBlock(audio, midi);
    }
    return audio.getMagnitude(0, kBlock);
}

const juce::MidiMessage kMiddleC = juce::MidiMessage::noteOn(1, 60, 0.8f);

} // namespace

TEST_CASE("Rendering before being prepared is silent, not noise", "[plugin][host]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;

    // Hosts probe. A plugin that has not been prepared owns no buffers, and the
    // wrong answer here is not a crash -- it is passing the host's own buffer
    // through untouched, which in a live rig is whatever was in that memory.
    juce::AudioBuffer<float> audio(2, kBlock);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill(audio.getWritePointer(ch), 0.5f, kBlock);

    juce::MidiBuffer midi;
    midi.addEvent(kMiddleC, 0);
    processor.processBlock(audio, midi);

    CHECK(audio.getMagnitude(0, kBlock) == 0.0f);
}

TEST_CASE("A block larger than the one announced is rendered whole", "[plugin][host]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor, kSampleRate, kBlock);

    // JUCE says a host MAY exceed the maximum it declared, and offline bounces do.
    // Every internal buffer here is sized to the declared maximum, so the engine
    // cuts an oversized block into slices of that size rather than writing past
    // them. Four times over, with a note, so every slice carries real work.
    const float peak = render(processor, 8, kBlock * 4, &kMiddleC);
    CHECK(peak > 0.0f);

    // And the whole buffer was written, not only the first slice: silence in the
    // tail of the block would be the shape of a plugin that rendered 256 frames of
    // a 1024-frame request and left the rest alone.
    juce::AudioBuffer<float> audio(2, kBlock * 4);
    audio.clear();
    juce::MidiBuffer none;
    processor.processBlock(audio, none);
    CHECK(audio.getMagnitude(kBlock * 3, kBlock) > 0.0f);
}

TEST_CASE("The sample rate can change under a held note", "[plugin][host]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor, kSampleRate, kBlock);

    REQUIRE(render(processor, 24, kBlock, &kMiddleC) > 0.0f);

    // A user switches audio device mid-session and the host re-prepares us, with a
    // key still down and every buffer in the instrument about to be replaced.
    prepare(processor, 96000.0, kBlock);

    const juce::MidiMessage again = juce::MidiMessage::noteOn(1, 62, 0.8f);
    CHECK(render(processor, 24, kBlock, &again) > 0.0f);
}

TEST_CASE("The buffer size can change under a held note", "[plugin][host]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor, kSampleRate, kBlock);

    REQUIRE(render(processor, 24, kBlock, &kMiddleC) > 0.0f);

    prepare(processor, kSampleRate, 1024);

    const juce::MidiMessage again = juce::MidiMessage::noteOn(1, 64, 0.8f);
    CHECK(render(processor, 8, 1024, &again) > 0.0f);
}

TEST_CASE("All notes off stops the organ", "[plugin][host]")
{
    JuceScope              scope;
    CaeciliaAudioProcessor processor;
    prepare(processor, kSampleRate, kBlock);

    const float sounding = render(processor, 24, kBlock, &kMiddleC);
    REQUIRE(sounding > 0.0f);

    // The panic button, and the message every host sends when transport stops. An
    // organ has no natural decay -- a pipe sounds until the key comes up -- so a
    // note left on by a stopped transport sounds forever.
    const juce::MidiMessage panic = juce::MidiMessage::allNotesOff(1);
    const float             tail  = tailAfter(processor, 400, &panic); // ~2.1 s

    // Measured at the END of two seconds rather than as a peak, because the peak
    // would still find the note itself in the first block. What must be gone is
    // the pipe; the reverb is allowed to be finishing.
    CHECK(tail < sounding * 0.05f);
}
