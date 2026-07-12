/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/SpscRing.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/plugin/CommandBridge.h"
#include "caecilia/plugin/MeterBridge.h"
#include "caecilia/plugin/ParameterMirror.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/tuning/TuningModel.h"
#include "caecilia/ui/StateMirror.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace caecilia::plugin
{

/**
 * @brief The VST3 / AU / Standalone @c juce::AudioProcessor — one of only two
 *        modules allowed to include JUCE, and the whole boundary between the host
 *        and the pure @c caecilia_core engine.
 *
 * The processor owns the pure @c core::engine::AudioEngine and a small set of
 * bridges, and does nothing but glue:
 *  - @c prepareToPlay calls @c AudioEngine::prepare — the ONLY place allocation
 *    happens — sets FTZ/DAZ, and reports plugin delay compensation.
 *  - @c processBlock sets @c ScopedNoDenormals, lets @ref CommandBridge translate
 *    MIDI + changed parameters into @c EngineCommand values on the SPSC ring, wraps
 *    the host buffer in a @c core::AudioBlock, and calls @c AudioEngine::processBlock.
 *  - state save/load round-trips through @ref CaeciliaParameterMirror (APVTS plus a
 *    parallel semantic registration @c ValueTree with undo/redo).
 *
 * It owns NO DSP or synthesis logic; that all lives behind the pure engine seam.
 * The class carries no JUCE type across that seam — the engine sees only
 * @c core::AudioBlock and @c EngineCommand.
 *
 * Exposed as @c CaeciliaProcessor for brevity.
 */
class CaeciliaAudioProcessor final : public juce::AudioProcessor
{
public:
    CaeciliaAudioProcessor();
    ~CaeciliaAudioProcessor() override = default;

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

    [[nodiscard]] const juce::String getName() const override { return "Caecilia"; }
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

    [[nodiscard]] CaeciliaParameterMirror& parameters() noexcept { return parameters_; }
    [[nodiscard]] MeterBridge&             meters() noexcept { return meterBridge_; }
    [[nodiscard]] core::engine::AudioEngine& engine() noexcept { return engine_; }

    /// The compiled instrument the console lays itself out from (immutable).
    [[nodiscard]] const model::Organ& organ() const noexcept { return organ_; }

    /// The live audio->UI mirror (meters + lit keys) the console polls.
    [[nodiscard]] ui::StateMirror& stateMirror() noexcept { return stateMirror_; }

    /// The manual notes and lit keys are routed to (a single-keyboard player's
    /// primary division). The console lights this division as MIDI arrives.
    [[nodiscard]] core::DivisionId playDivision() const noexcept { return playDivision_; }

    // --- registration control (message thread; used by the console) ----------

    /// @return true if drawstop @p stop is currently drawn.
    [[nodiscard]] bool isStopEngaged(core::StopId stop) const noexcept;

    /// Draw / retire a stop and rebuild the sounding registration. Message thread:
    /// builds the new voice bank off the audio thread, then swaps it into the
    /// engine under the processing lock so the audio thread never sees a torn pool.
    void toggleStop(core::StopId stop);

    /// Play (or release) a note from the on-screen keyboard. Message thread: the
    /// event is queued lock-free and merged into the next block's MIDI.
    void uiNote(core::DivisionId division, core::MidiNote note, bool down);

private:
    void updateLatency() noexcept;

    /// Off-thread: seed the pre-allocated voices with the composite spectrum of the
    /// currently drawn stops and bind them into the engine. Called from
    /// prepareToPlay and rebuilt (with the processing lock held) on every stop change.
    void buildInstrument(double sampleRate, std::size_t maxBlockFrames);

    /// Off-thread: sum every drawn stop's family recipe into one composite spectrum
    /// referenced to 8' unison (footage folded into the partial ratios, exactly as
    /// the engine's own per-stop voicing does), so a single voice sounds the whole
    /// drawn registration cleanly and polyphonically.
    [[nodiscard]] synth::SpectralModel compositeSpectrum() const;

    /// Choose a sensible opening registration (a principal chorus + flute on the
    /// primary manual) and the manual MIDI is routed to. Off-thread.
    void chooseDefaultRegistration();

    // --- pure engine (owns all DSP/synthesis behind the core seam) ----------
    core::engine::AudioEngine engine_;

    // --- the instrument specification (immutable once built) ----------------
    // The console lays itself out from this, and the registration is a subset of
    // its stops. Built once off-thread in the constructor.
    model::Organ            organ_;
    std::vector<bool>       engaged_;          ///< Drawn state per StopId::value.
    core::DivisionId        playDivision_{};   ///< Primary manual for MIDI + key lights.

    // --- synthesis instance owned by the plugin (pre-allocated, bound in) ---
    // The concrete voices live here (the engine's pool only borrows pointers),
    // so they outlive the engine. Every voice is seeded with the same composite
    // spectrum of the drawn stops, referenced to 8', so any key sounds the full
    // drawn registration cleanly and polyphonically.
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices_;
    std::vector<core::IVoice*>                          voicePtrs_;
    dsp::FdnReverb                                      reverb_;
    tuning::TuningModel                                 tuning_;

    // --- host-facing state + bridges ----------------------------------------
    CaeciliaParameterMirror parameters_;
    CommandBridge           commandBridge_;
    MeterBridge             meterBridge_;

    // --- live audio -> UI feed (console reads this at frame rate) ------------
    ui::StateMirror         stateMirror_;      ///< Meters + lit keys, double-buffered.
    ui::KeyStateSnapshot    keys_{};           ///< Lit-key accumulator (audio thread).

    /// One on-screen-keyboard note event, queued lock-free for the audio thread.
    struct UiNoteEvent { core::MidiNote note = 0; bool down = false; };
    core::engine::SpscRing<UiNoteEvent, 512> uiNotes_; ///< Console keyboard -> audio.

    /// Host-facing output trim (NOT part of the synthesis chain), applied post-engine
    /// and smoothed to avoid zipper noise. // TODO(phase0.3): fold gain staging into
    /// the engine master chain if a pre-reverb trim is ever wanted.
    juce::LinearSmoothedValue<float> masterGain_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaeciliaAudioProcessor)
};

/// Short alias matching the module brief's @c CaeciliaProcessor type name.
using CaeciliaProcessor = CaeciliaAudioProcessor;

} // namespace caecilia::plugin
