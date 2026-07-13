/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/MasterEq.h"
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

#include <cstdint>
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

    /// All-notes-off / panic from the console (stopping a demo, Release keys).
    /// Message thread: sets a flag the audio thread acts on next block.
    void uiAllNotesOff() noexcept { uiPanic_.store(true, std::memory_order_relaxed); }

    // --- Sequencer page-turn (configurable MIDI keys -> Prev/Next) -----------
    // Two configurable MIDI notes step the console's registration sequencer and
    // are SWALLOWED so they never sound a pipe. Defaults are the user's Casio top
    // keys (si5 = 83 Previous, do6 = 84 Next), but any key on any keyboard can be
    // bound via MIDI-learn. Message-thread setters; audio-thread reads.

    /// Configure the page-turn keys and enable/disable the feature. Message thread.
    void setSeqNav(int prevNote, int nextNote, bool enabled) noexcept
    {
        seqPrevNote_.store(prevNote, std::memory_order_relaxed);
        seqNextNote_.store(nextNote, std::memory_order_relaxed);
        seqNavEnabled_.store(enabled, std::memory_order_relaxed);
    }

    /// Arm MIDI-learn: the next note-on becomes the binding. 1 = Previous,
    /// 2 = Next, 0 = cancel. Message thread.
    void armSeqLearn(int which) noexcept { seqLearn_.store(which, std::memory_order_relaxed); }

    /// Editor drain: pop one page-turn direction (-1 = Previous, +1 = Next).
    /// @return false when the queue is empty. Message thread (single consumer).
    [[nodiscard]] bool popSeqNav(std::int8_t& dir) noexcept { return seqNav_.pop(dir); }

    /// Editor drain: take a learned binding, encoded as (which * 256 + note), or
    /// -1 if none was captured since the last call. Message thread.
    [[nodiscard]] int takeLearnedNote() noexcept
    {
        return seqLearnedNote_.exchange(-1, std::memory_order_relaxed);
    }

    [[nodiscard]] int  seqPrevNote()   const noexcept { return seqPrevNote_.load(std::memory_order_relaxed); }
    [[nodiscard]] int  seqNextNote()   const noexcept { return seqNextNote_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool seqNavEnabled() const noexcept { return seqNavEnabled_.load(std::memory_order_relaxed); }

    /// Final output peak per channel this block (post master-trim + soft-clip),
    /// linear 0..1. The console VU reads these so it reflects the REAL audio
    /// (including physically-played MIDI), not the on-screen keyboard state.
    [[nodiscard]] float outputPeakL() const noexcept { return outPeakL_.load(std::memory_order_relaxed); }
    [[nodiscard]] float outputPeakR() const noexcept { return outPeakR_.load(std::memory_order_relaxed); }

    /// Set the whole sounding registration from the WebView console (a list of
    /// drawn family+footage ranks). Message thread: builds the composite spectrum
    /// off the audio thread and swaps the freshly-seeded voices into the engine
    /// under the processing lock. This is the console's real audio path.
    void setUiRegistration(const std::vector<model::RegistrationRank>& ranks);

    /// Console settings -> engine. Master is a linear output trim; reverb space is
    /// a preset index (0=Room..4=Plate) and mix is 0..1. RT-safe / message thread.
    void setUiMaster(float linearGain) noexcept
    {
        uiMaster_.store(juce::jlimit(0.0f, 2.0f, linearGain), std::memory_order_relaxed);
    }
    void setUiReverb(int spaceIndex, float mix);

    /// Console master-EQ control. @p band is 0..4 (Warmth/Boxiness/Body/Presence/
    /// Air); @p gainDb the band gain. Message thread; applied under the callback lock.
    void setUiEqGain(int band, float gainDb);
    /// Enable/disable the whole master EQ. Message thread.
    void setUiEqEnabled(bool on);

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

    /// Build a fresh voice bank seeded with @p composite and swap it into the
    /// engine under the processing lock (RT-safe hand-off). Message thread.
    void swapVoicesFromComposite(const synth::SpectralModel& composite);

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
    dsp::MasterEq                                       masterEq_;  ///< Post-reverb tone voicing.
    dsp::Limiter                                        limiter_;   ///< Brick-wall master limiter (Tutti safety).
    tuning::TuningModel                                 tuning_;

    // --- host-facing state + bridges ----------------------------------------
    CaeciliaParameterMirror parameters_;
    CommandBridge           commandBridge_;
    MeterBridge             meterBridge_;

    // --- live audio -> UI feed (console reads this at frame rate) ------------
    ui::StateMirror         stateMirror_;      ///< Meters + lit keys, double-buffered.
    ui::KeyStateSnapshot    keys_{};           ///< Lit-key accumulator (audio thread).

    /// One on-screen-keyboard note event, queued lock-free for the audio thread.
    /// Carries its division so the correct keyboard sounds and lights.
    struct UiNoteEvent { core::DivisionId division{}; core::MidiNote note = 0; bool down = false; };
    core::engine::SpscRing<UiNoteEvent, 512> uiNotes_; ///< Console keyboard -> audio.
    juce::MidiBuffer        uiScratch_;        ///< Pre-reserved: UI notes -> command bridge, no RT alloc.

    /// Host-facing output trim (NOT part of the synthesis chain), applied post-engine
    /// and smoothed to avoid zipper noise. // TODO(phase0.3): fold gain staging into
    /// the engine master chain if a pre-reverb trim is ever wanted.
    juce::LinearSmoothedValue<float> masterGain_;

    /// Mild polyphony compensation (smoothed): a dense chord sums much hotter than a
    /// single note, so trim the bus by ~1/(1+k*(N-1)) before the limiter. This lets
    /// sparse playing stay loud while a full Tutti only rides GENTLY into the limiter
    /// (no bass pumping / distortion) instead of slamming it. N = active voices.
    juce::LinearSmoothedValue<float> polyGain_;

    /// Console master trim (0..2, default 1) set from the Settings panel.
    std::atomic<float> uiMaster_{ 1.0f };

    /// Set by the console (stop demo / release keys); the audio thread panics and
    /// clears it. Fixes notes sticking when a demo is stopped mid-phrase.
    std::atomic<bool> uiPanic_{ false };

    // --- Sequencer page-turn state ------------------------------------------
    std::atomic<int>  seqPrevNote_{ 83 };     ///< MIDI note for "previous" (si5 default).
    std::atomic<int>  seqNextNote_{ 84 };     ///< MIDI note for "next" (do6 default).
    std::atomic<bool> seqNavEnabled_{ true }; ///< Master enable for the page-turn keys.
    std::atomic<int>  seqLearn_{ 0 };         ///< MIDI-learn arm: 0=off, 1=Prev, 2=Next.
    std::atomic<int>  seqLearnedNote_{ -1 };  ///< Captured learn result (which*256+note); -1 = none.
    core::engine::SpscRing<std::int8_t, 64> seqNav_; ///< Audio -> editor: page-turn directions.
    juce::MidiBuffer  hostScratch_;           ///< Pre-reserved: host MIDI minus nav keys (no RT alloc).

    // --- Output metering (real VU feed for the console) ---------------------
    std::atomic<float> outPeakL_{ 0.0f };     ///< Left  output peak this block (linear).
    std::atomic<float> outPeakR_{ 0.0f };     ///< Right output peak this block (linear).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaeciliaAudioProcessor)
};

/// Short alias matching the module brief's @c CaeciliaProcessor type name.
using CaeciliaProcessor = CaeciliaAudioProcessor;

} // namespace caecilia::plugin
