/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/engine/AudioEngine.h"
#include "ceciliae/plugin/CommandBridge.h"
#include "ceciliae/plugin/MeterBridge.h"
#include "ceciliae/plugin/ParameterMirror.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ceciliae::plugin
{

/**
 * @brief The VST3 / AU / Standalone @c juce::AudioProcessor — one of only two
 *        modules allowed to include JUCE, and the whole boundary between the host
 *        and the pure @c ceciliae_core engine.
 *
 * The processor owns the pure @c core::engine::AudioEngine and a small set of
 * bridges, and does nothing but glue:
 *  - @c prepareToPlay calls @c AudioEngine::prepare — the ONLY place allocation
 *    happens — sets FTZ/DAZ, and reports plugin delay compensation.
 *  - @c processBlock sets @c ScopedNoDenormals, lets @ref CommandBridge translate
 *    MIDI + changed parameters into @c EngineCommand values on the SPSC ring, wraps
 *    the host buffer in a @c core::AudioBlock, and calls @c AudioEngine::processBlock.
 *  - state save/load round-trips through @ref CeciliaeParameterMirror (APVTS plus a
 *    parallel semantic registration @c ValueTree with undo/redo).
 *
 * It owns NO DSP or synthesis logic; that all lives behind the pure engine seam.
 * The class carries no JUCE type across that seam — the engine sees only
 * @c core::AudioBlock and @c EngineCommand.
 *
 * Exposed as @c CeciliaeProcessor for brevity.
 */
class CeciliaeAudioProcessor final : public juce::AudioProcessor
{
public:
    CeciliaeAudioProcessor();
    ~CeciliaeAudioProcessor() override = default;

    // --- lifecycle (off-thread) ---------------------------------------------

    void prepareToPlay(double sampleRate, int maxBlockSamples) override;
    void releaseResources() override;
    [[nodiscard]] bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // --- audio callback (real-time thread) ----------------------------------

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock; // keep the double-precision overload visible

    // --- editor -------------------------------------------------------------

    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override { return true; }

    // --- identity / capabilities --------------------------------------------

    [[nodiscard]] const juce::String getName() const override { return "Ceciliae"; }
    [[nodiscard]] bool acceptsMidi() const override { return true; }
    [[nodiscard]] bool producesMidi() const override { return false; }
    [[nodiscard]] bool isMidiEffect() const override { return false; }
    [[nodiscard]] double getTailLengthSeconds() const override;

    // --- programs (single default program) ----------------------------------

    [[nodiscard]] int getNumPrograms() override { return 1; }
    [[nodiscard]] int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    [[nodiscard]] const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // --- persistence --------------------------------------------------------

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- accessors for the editor / UI --------------------------------------

    [[nodiscard]] CeciliaeParameterMirror& parameters() noexcept { return parameters_; }
    [[nodiscard]] MeterBridge&             meters() noexcept { return meterBridge_; }
    [[nodiscard]] core::engine::AudioEngine& engine() noexcept { return engine_; }

private:
    void updateLatency() noexcept;

    // --- pure engine (owns all DSP/synthesis behind the core seam) ----------
    core::engine::AudioEngine engine_;

    // --- host-facing state + bridges ----------------------------------------
    CeciliaeParameterMirror parameters_;
    CommandBridge           commandBridge_;
    MeterBridge             meterBridge_;

    /// Host-facing output trim (NOT part of the synthesis chain), applied post-engine
    /// and smoothed to avoid zipper noise. // TODO(phase0.3): fold gain staging into
    /// the engine master chain if a pre-reverb trim is ever wanted.
    juce::LinearSmoothedValue<float> masterGain_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CeciliaeAudioProcessor)
};

/// Short alias matching the module brief's @c CeciliaeProcessor type name.
using CeciliaeProcessor = CeciliaeAudioProcessor;

} // namespace ceciliae::plugin
