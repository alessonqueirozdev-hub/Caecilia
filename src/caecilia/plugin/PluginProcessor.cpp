/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/plugin/PluginProcessor.h"

#include "caecilia/plugin/PluginEditor.h"

#include <cmath>
#include <span>

namespace caecilia::plugin
{

namespace
{

/// Build the default "grand plenum" composite spectrum, referenced to 8' unison,
/// by summing several principal ranks (8'/4'/2') and a mixture with each rank's
/// footage folded into its partial ratios. A voice seeded with this plays the
/// whole registration at the key pitch, so mutations/mixtures land at their true
/// intervals instead of an arbitrary per-key stop.
synth::SpectralModel buildPlenumSpectrum()
{
    synth::SpectralModel plenum;

    const auto addStop = [&plenum](const synth::SpectralModel& stop, double feet, float gainDb)
    {
        const double ratioShift = feet > 0.0 ? 8.0 / feet : 1.0; // fold footage into ratio
        for (const synth::PartialTrack& p : stop.partials)
        {
            synth::PartialTrack t = p;
            t.ratioToF0 = static_cast<float>(static_cast<double>(p.ratioToF0) * ratioShift);
            t.ampDb     = p.ampDb + gainDb;
            plenum.partials.push_back(t);
        }
    };

    addStop(model::makeSpectralPrincipal(core::footage::kEight, 1.0f, 12), 8.0,  0.0f);
    addStop(model::makeSpectralPrincipal(core::footage::kFour,  1.0f, 10), 4.0, -3.5f);
    addStop(model::makeSpectralPrincipal(core::footage::kTwo,   1.1f,  8), 2.0, -7.0f);

    const core::Footage mixRanks[] = { core::footage::kTwo, core::footage::kOneAndThird, core::footage::kOne };
    addStop(model::makeSpectralMixture(std::span<const core::Footage>(mixRanks, 3), 1.2f), 8.0, -11.0f);

    plenum.fundamentalHz = 0.0f; // set per note by the voice at noteOn
    return plenum;
}

} // namespace

void CaeciliaAudioProcessor::buildInstrument(double sampleRate, std::size_t maxBlockFrames)
{
    const synth::SpectralModel plenum = buildPlenumSpectrum();

    synth::VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight; // composite is already referenced to 8'

    constexpr std::size_t kPolyphony = 64;
    voices_.clear();
    voicePtrs_.clear();
    voices_.reserve(kPolyphony);
    voicePtrs_.reserve(kPolyphony);
    for (std::size_t i = 0; i < kPolyphony; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->prepare(sampleRate, maxBlockFrames);
        v->setContext(ctx);
        v->seedFrom(plenum);
        voicePtrs_.push_back(v.get());
        voices_.push_back(std::move(v));
    }
}

CaeciliaAudioProcessor::CaeciliaAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output",
                                                        juce::AudioChannelSet::stereo(),
                                                        true))
    , parameters_(*this)
{
}

// ---------------------------------------------------------------------------
// Lifecycle (off-thread). prepareToPlay is the ONLY place allocation happens.
// ---------------------------------------------------------------------------

void CaeciliaAudioProcessor::prepareToPlay(double sampleRate, int maxBlockSamples)
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

    // Build the default registration's voices and bind them into the engine.
    buildInstrument(sampleRate, frames);
    engine_.bindVoices(voicePtrs_.data(), voicePtrs_.size());

    // Master reverb: a large-hall preset by default; the parameter bridge then
    // syncs it to the APVTS reverb controls on the first processed block.
    reverb_.prepare(sampleRate, frames, channels);
    reverb_.setPreset(dsp::ReverbPreset::Hall);
    engine_.setMasterReverb(&reverb_);

    masterGain_.reset(sampleRate, 0.02); // 20 ms output-trim ramp
    masterGain_.setCurrentAndTargetValue(1.0f);

    updateLatency();
}

void CaeciliaAudioProcessor::releaseResources()
{
    // Engine buffers persist until the next prepare(); nothing to free here. The
    // no-allocation contract means there is no per-run scratch to release.
}

bool CaeciliaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void CaeciliaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

juce::AudioProcessorEditor* CaeciliaAudioProcessor::createEditor()
{
    return new CaeciliaEditor(*this);
}

// ---------------------------------------------------------------------------
// Capabilities.
// ---------------------------------------------------------------------------

double CaeciliaAudioProcessor::getTailLengthSeconds() const
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

void CaeciliaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    parameters_.writeState(destData);
}

void CaeciliaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
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

void CaeciliaAudioProcessor::updateLatency() noexcept
{
    // Plugin delay compensation = engine DSP group delay (oversampling + reverb
    // pre-delay). // TODO(phase0.3): query the bound master reverb / oversampler
    // for its real latency; the pure engine reports it through IReverb.
    setLatencySamples(0);
}

} // namespace caecilia::plugin

// ---------------------------------------------------------------------------
// JUCE plugin entry point: the host instantiates the processor through this.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new caecilia::plugin::CaeciliaAudioProcessor();
}
