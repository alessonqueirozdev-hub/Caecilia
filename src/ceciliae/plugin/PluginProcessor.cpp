/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/plugin/PluginProcessor.h"

#include "ceciliae/plugin/PluginEditor.h"

#include <cmath>

namespace ceciliae::plugin
{

CeciliaeAudioProcessor::CeciliaeAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output",
                                                        juce::AudioChannelSet::stereo(),
                                                        true))
    , parameters_(*this)
{
}

// ---------------------------------------------------------------------------
// Lifecycle (off-thread). prepareToPlay is the ONLY place allocation happens.
// ---------------------------------------------------------------------------

void CeciliaeAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSamples)
{
    const auto frames   = static_cast<std::size_t>(juce::jmax(0, maxBlockSamples));
    const auto channels = static_cast<std::size_t>(juce::jmax(1, getTotalNumOutputChannels()));

    // The single allocation point: size every RT buffer from the host contract.
    // TODO(phase0.1): windchest count comes from the loaded OrganSpec.
    engine_.prepare(sampleRate, frames, channels, /*numWindchests*/ 1);

    // Bind the single-producer command path and the metering read path.
    commandBridge_.connect(engine_.commandQueue());
    commandBridge_.resetChangeTracking();
    meterBridge_.connect(engine_);

    // TODO(phase0.1): bind the synthesis voice arena, wind supply, tuning table
    // and master reverb into the engine here (all pre-allocated off-thread).
    //   engine_.bindVoices(...); engine_.setWindSupply(...);
    //   engine_.setTuning(...);  engine_.setMasterReverb(...);

    masterGain_.reset(sampleRate, 0.02); // 20 ms output-trim ramp
    masterGain_.setCurrentAndTargetValue(1.0f);

    updateLatency();
}

void CeciliaeAudioProcessor::releaseResources()
{
    // Engine buffers persist until the next prepare(); nothing to free here. The
    // no-allocation contract means there is no per-run scratch to release.
}

bool CeciliaeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Instrument: no input bus; mono or stereo output only.
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------------------
// Audio callback (real-time thread).
// ---------------------------------------------------------------------------

void CeciliaeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals; // per-thread FTZ/DAZ for the whole callback

    const auto numChannels = static_cast<std::size_t>(getTotalNumOutputChannels());
    const auto numFrames   = static_cast<std::size_t>(buffer.getNumSamples());

    // Single-producer encode of host intent onto the engine command ring, drained
    // by engine_.processBlock() below. Parameters first (cheap when unchanged),
    // then MIDI note/panic events.
    commandBridge_.pushChangedParameters(parameters_);
    commandBridge_.pushMidi(midi);
    midi.clear(); // this instrument produces no MIDI output

    // Wrap the host buffer as the JUCE-free AudioBlock — the only audio type that
    // crosses the engine seam — and render. The engine overwrites the buffer.
    core::AudioBlock block(buffer.getArrayOfWritePointers(), numChannels, numFrames);
    engine_.processBlock(block);

    // Host-facing output trim (see header): a smoothed gain, not synthesis DSP.
    const float gainDb = [this]
    {
        if (std::atomic<float>* p = parameters_.rawParameter(ParameterLayout::kMasterGainDb))
            return p->load(std::memory_order_relaxed);
        return 0.0f;
    }();
    masterGain_.setTargetValue(juce::Decibels::decibelsToGain(gainDb, -60.0f));
    masterGain_.applyGain(buffer, buffer.getNumSamples());
}

// ---------------------------------------------------------------------------
// Editor.
// ---------------------------------------------------------------------------

juce::AudioProcessorEditor* CeciliaeAudioProcessor::createEditor()
{
    return new CeciliaeEditor(*this);
}

// ---------------------------------------------------------------------------
// Capabilities.
// ---------------------------------------------------------------------------

double CeciliaeAudioProcessor::getTailLengthSeconds() const
{
    // Report reverb decay + pre-delay so the host flushes a long enough tail.
    auto read = [this](const char* id, float fallback)
    {
        if (std::atomic<float>* p = parameters_.rawParameter(id))
            return p->load(std::memory_order_relaxed);
        return fallback;
    };
    const float decay    = read(ParameterLayout::kReverbDecaySec, 2.5f);
    const float preDelay = read(ParameterLayout::kReverbPreDelayMs, 12.0f) * 0.001f;
    return static_cast<double>(decay + preDelay);
}

// ---------------------------------------------------------------------------
// Persistence.
// ---------------------------------------------------------------------------

void CeciliaeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    parameters_.writeState(destData);
}

void CeciliaeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (parameters_.readState(data, sizeInBytes))
    {
        // Force the next block to re-send the restored parameter state to the engine.
        commandBridge_.resetChangeTracking();
        updateLatency();
    }
}

// ---------------------------------------------------------------------------
// Internals.
// ---------------------------------------------------------------------------

void CeciliaeAudioProcessor::updateLatency() noexcept
{
    // Plugin delay compensation = engine DSP group delay (oversampling + reverb
    // pre-delay). // TODO(phase0.3): query the bound master reverb / oversampler
    // for its real latency; the pure engine reports it through IReverb.
    setLatencySamples(0);
}

} // namespace ceciliae::plugin

// ---------------------------------------------------------------------------
// JUCE plugin entry point: the host instantiates the processor through this.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ceciliae::plugin::CeciliaeAudioProcessor();
}
