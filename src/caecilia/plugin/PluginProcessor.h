// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/dsp/Limiter.h"
#include "caecilia/dsp/MasterEq.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/wind/WindModel.h"
#include "caecilia/engine/SpscRing.h"
#include "caecilia/midi/LearnedControls.h"
#include "caecilia/midi/MidiLearn.h"
#include "caecilia/midi/MidiMap.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Diagnostics.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/OrganLoader.h"
#include "caecilia/registration/RegistrationHistory.h"
#include "caecilia/plugin/CommandBridge.h"
#include "caecilia/plugin/MeterBridge.h"
#include "caecilia/plugin/ParameterMirror.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/tuning/LiveTuning.h"
#include "caecilia/ui/StateMirror.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <vector>

namespace caecilia::plugin
{

/**
 * @brief The VST3 / Standalone @c juce::AudioProcessor — one of only two
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
 *  - state save/load round-trips through @ref CaeciliaParameterMirror: the APVTS
 *    plus the console tree built by @c captureConsoleState (drawn registration,
 *    trims, reverb space, master EQ, page-turn keys). The mirror's semantic
 *    registration @c ValueTree is serialised alongside them but nothing writes
 *    to it, so it round-trips empty and drives no undo/redo.
 *
 * It owns NO DSP or synthesis logic; that all lives behind the pure engine seam.
 * The class carries no JUCE type across that seam — the engine sees only
 * @c core::AudioBlock and @c EngineCommand.
 *
 * Exposed as @c CaeciliaProcessor for brevity.
 */
class CaeciliaAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AsyncUpdater
{
public:
    CaeciliaAudioProcessor();
    /// Not defaulted, and it must not be.
    ///
    /// ~AsyncUpdater cancels any callback still in flight, but a base destructor
    /// runs AFTER the derived members are gone -- so between the last member dying
    /// and the base cancelling, a pending handleAsyncUpdate() would read a
    /// half-destroyed processor. Cancelling FIRST closes that window.
    ~CaeciliaAudioProcessor() override;

    // --- lifecycle (off-thread) ---------------------------------------------

    void prepareToPlay(double sampleRate, int maxBlockSamples) override;
    void releaseResources() override;
    /// Hosts call this expecting a clean slate between takes. Clears every tail.
    void reset() override;
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

    // --- Loading an organ ---------------------------------------------------
    //
    // The instrument is whatever organ it was given. Until this existed it was
    // whichever one had been compiled into the binary, which is the difference
    // between a virtual organ and a platform for them.

    /**
     * @brief Replace the instrument with the organ in @p document.
     * @param document   Organ-file text (see @c model::OrganLoader).
     * @param sourceName What to call it in a diagnostic -- a path, usually.
     * @return The diagnostics. On any error the instrument is UNCHANGED: a
     *         half-loaded organ is worse than the one you had.
     *
     * Message thread. Audio is suspended across the swap, because this rebuilds
     * the voice pool, the wind, the buses and the channel map -- everything
     * prepareToPlay builds, which is exactly what a different organ needs.
     */
    model::LoadDiagnostics loadOrganDocument(const juce::String& document,
                                             const juce::String& sourceName,
                                             const model::ResourceResolver& resolve = {});

    /// @copydoc loadOrganDocument but reading the bytes from a file first.
    model::LoadDiagnostics loadOrganFile(const juce::File& file);

    /// Go back to the organ built into the binary.
    void loadBuiltInOrgan();

    /// @return The file this instrument was loaded from, or an empty string when
    ///         it is the built-in organ.
    [[nodiscard]] juce::String organPath() const { return organPath_; }

    /// @return The organ this project asked for and did not get, or empty.
    ///
    /// Set when a session names an organ file that will not open -- moved, renamed,
    /// on a drive that is not mounted. It is what the project is SAVED with, so a
    /// save on the wrong machine does not quietly rewrite the project to the
    /// built-in organ; and it is what the console reports, because a log file is
    /// not somewhere an organist finds out they are playing the wrong instrument.
    [[nodiscard]] juce::String unresolvedOrganPath() const { return unresolvedOrgan_; }

    /// The live audio->UI mirror (meters + lit keys) the console polls.
    [[nodiscard]] ui::StateMirror& stateMirror() noexcept { return stateMirror_; }

    /// The manual notes and lit keys are routed to (a single-keyboard player's
    /// primary division). The console lights this division as MIDI arrives.
    [[nodiscard]] core::DivisionId playDivision() const noexcept { return playDivision_; }

    // --- registration control (message thread; used by the console) ----------

    /// @return true if drawstop @p stop is currently drawn.
    [[nodiscard]] bool isStopEngaged(core::StopId stop) const noexcept;

    /// How many combinations the piston memory holds: one per MIDI program number,
    /// so every program change a host can send lands somewhere.
    static constexpr std::size_t kNumGenerals = 128;

    /// @return the registration stored in general @p index, or 0 if the slot is empty.
    [[nodiscard]] std::uint64_t generalMask(std::size_t index) const noexcept;

    /// @return true if general @p index holds a captured registration.
    [[nodiscard]] bool generalIsSet(std::size_t index) const noexcept;

    /// Store the CURRENT registration in general @p index. Message thread.
    void captureGeneral(std::size_t index);

    /// Draw the registration stored in general @p index. Message thread.
    ///
    /// An empty slot does nothing at all -- deliberately. Recalling an unset piston
    /// on a real console does not clear the jamb, and a program change arriving from
    /// a sequencer track that means something else entirely must not silence the
    /// instrument.
    void recallGeneral(std::size_t index);

    /// Empty general @p index. Message thread. The factory default is NOT restored:
    /// the defaults are built once, at construction, from the loaded organ.
    void clearGeneral(std::size_t index);

    /// The general most recently recalled, or -1. Cleared by reading it, so the
    /// console can flash the piston that fired exactly once.
    [[nodiscard]] int consumeLastGeneral() noexcept;

    /// Draw or retire the tremulant. Message thread.
    ///
    /// One switch, and it moves every chest that HAS a tremulant -- on this organ
    /// the Récit, which is where a French console puts it and why one switch is the
    /// right control. Per-division tremulant switches are a console change.
    void setUiTremulant(bool on);

    /// @return true if the tremulant is drawn, as the host parameter holds it.
    [[nodiscard]] bool uiTremulant() const noexcept
    {
        const std::atomic<float>* p = parameters_.rawParameter(ParameterLayout::kTremulantOn);
        return p != nullptr && p->load(std::memory_order_relaxed) >= 0.5f;
    }

    /// @return The drawn couplers, as a mask keyed by the organ's coupler index.
    [[nodiscard]] std::uint32_t drawnCouplers() const noexcept { return couplers_; }

    /// Draw / retire one coupler. Message thread.
    void toggleCoupler(std::size_t index);

    /// Draw / retire a stop. Message thread, and it costs a table publication: the
    /// rank voicings already exist, so drawing a stop only changes WHICH of them the
    /// engine is holding. Voices already sounding are not touched at all.
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
    // --- MIDI learn ---------------------------------------------------------
    //
    // Binding a physical control to a drawstop or a piston. An organist with a
    // stop-tab console or a rank of toe studs has one instrument in front of them
    // and another on the screen, and this is what makes them the same instrument.
    //
    // Program change already reached the generals before any of this, because a
    // toe stud usually sends one. What did not work was a stud that sends a NOTE
    // or a CC, and nothing at all could reach a drawstop.

    /// Arm learn on one stop: the next control the organist actuates binds to it.
    /// Message thread. Re-arming replaces the pending target.
    void armMidiLearnStop(core::StopId stop);

    /// Arm learn on a general piston.
    void armMidiLearnGeneral(std::size_t index);

    /// Abandon a pending learn.
    void cancelMidiLearn();

    /// @return true while a learn is waiting for a control.
    [[nodiscard]] bool midiLearnArmed() const noexcept
    {
        return midiLearnArmed_.load(std::memory_order_relaxed);
    }

    /// Forget every learned binding. Message thread.
    void clearMidiBindings();

    /// Forget whatever is bound to this stop / this general. Message thread.
    void clearMidiBindingForStop(core::StopId stop);
    void clearMidiBindingForGeneral(std::size_t index);

    /// The learned bindings, for the console and for the saved document.
    [[nodiscard]] const midi::MidiMap& midiMap() const noexcept { return midiMap_; }

    /**
     * @brief Run the message-thread work the audio thread has queued, now.
     *
     * A MIDI event that reaches a learned control is packed into a ring by the
     * audio thread and woken with triggerAsyncUpdate; the binding is installed,
     * and the registration changed, when the host next pumps its message loop.
     *
     * This is that pump, for a caller that has no host. It exists for the tests --
     * said plainly rather than dressed up, because there is no other caller and
     * inventing one would be worse than admitting it. What it buys is that the
     * whole path can be exercised end to end instead of at its two ends, and the
     * alternative was to widen AsyncUpdater from private to public, which would
     * publish triggerAsyncUpdate and cancelPendingUpdate to everyone.
     *
     * Message thread. Idempotent: with nothing queued it does nothing.
     */
    void flushQueuedMidiActions() { handleMidiActions(); }

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

    /// @return The drawn registration as a StopId mask. Message thread.
    [[nodiscard]] std::uint64_t drawnStops() const noexcept { return registration_; }

    // --- Taking a registration back -----------------------------------------
    //
    // An organist pulls the wrong knob in the middle of a phrase and wants the
    // sound they had. The history is BRANCHING, so an undo does not throw the
    // other path away: going back and trying something else keeps both, which is
    // what auditioning a plenum actually looks like.

    /// Step back to the previous registration. Does nothing at the root.
    void undoRegistration();

    /// Step forward again, onto the most recently explored branch.
    void redoRegistration();

    [[nodiscard]] bool canUndoRegistration() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedoRegistration() const noexcept { return history_.canRedo(); }

    // --- Asking for stops by description ------------------------------------

    /// What a selector expression picks out of THIS organ, right now.
    struct SelectorResult
    {
        std::vector<core::StopId> stops;    ///< Sorted ascending, as StopSet gives them.
        juce::String              error;    ///< Empty when the expression parsed.
        int                       errorPos = 0; ///< 0-based offset of the error in the query.
    };

    /**
     * @brief Resolve a selector expression against the loaded organ.
     * @param query e.g. `family:reed & pitch:8 & div:swell`, or `engaged - div:pedal`.
     *
     * The grammar is @c registration::SelectorParser's, which is a versioned
     * public contract shared with the factory generals and, when they are wired,
     * with OSC and JSON-RPC. It is resolved against the LIVE registration, which
     * is what makes `engaged` mean anything.
     *
     * Message thread: this parses and allocates.
     */
    [[nodiscard]] SelectorResult selectStops(const juce::String& query) const;

    /**
     * @brief Draw everything @p query picks out, as ONE gesture.
     * @return How many stops the expression matched (0 if it did not parse).
     *
     * Additive, like typing an expression into the omnibar and pressing return has
     * always been: it draws what you asked for and leaves the rest alone. One
     * call to the single writer, so it is one entry in the history -- an organist
     * who draws the reeds by description and thinks better of it takes back the
     * whole gesture, not fourteen stops one at a time.
     */
    std::size_t drawSelector(const juce::String& query);

    /**
     * @brief Draw the plenum of the division being played.
     * @param withReeds Add the chorus reeds -- a grand plein-jeu rather than a
     *                  diapason chorus.
     * @return How many stops the plenum came to.
     *
     * Not a tutti. A plenum is the principal-family pyramid 16'-8'-4'-2' crowned
     * by mixtures, with the mutations left out so the chorus is not polluted by a
     * tierce -- which is the registration an organist actually wants far more
     * often than everything at once, and the one this instrument could not offer
     * despite having had a builder for it all along.
     *
     * Replaces what is drawn rather than adding to it: a plenum is a complete
     * registration, and half of one over the top of something else is neither.
     * One gesture, so one entry in the history.
     */
    std::size_t drawPlenum(bool withReeds);

    /// Draw exactly this set. Message thread; moves the host parameters with it.
    void setDrawnStops(std::uint64_t bits);

    /// Console GAIN (pre-limiter drive): how hard the instrument pushes into the
    /// master limiter — affects loudness AND how much the limiter works. 0..2.
    void setUiMaster(float linearGain) noexcept
    {
        uiMaster_.store(juce::jlimit(0.0f, 2.0f, linearGain), std::memory_order_relaxed);
    }
    /// Console VOLUME (post-limiter output level): a clean final attenuation of the
    /// mastered signal, distinct from Gain. 0..1 (unity = full). RT-safe.
    void setUiVolume(float linear) noexcept
    {
        uiVolume_.store(juce::jlimit(0.0f, 1.0f, linear), std::memory_order_relaxed);
    }
    void setUiReverb(int spaceIndex, float mix);

    /// Console master-EQ control. @p band is 0..4 (Warmth/Boxiness/Body/Presence/
    /// Air); @p gainDb the band gain. Message thread; applied under the callback lock.
    void setUiEqGain(int band, float gainDb);
    /// Enable/disable the whole master EQ. Message thread.
    void setUiEqEnabled(bool on);

    /// Move a division's swell shoe from the console. @p position is 0 (shut) to
    /// 1 (open); the engine owns how much a shut box attenuates.
    void setUiExpression(int division, float position);

    /// Mark the start and end of one continuous EQ drag.
    ///
    /// Without this a drag is a burst of independent parameter writes, and a host
    /// recording automation stipples the lane with a point per mouse pixel instead
    /// of the ramp the user drew. It is also what makes the host's undo treat the
    /// whole drag as one step.
    void setUiEqGesture(int band, bool begin);

    /// @return The EQ enable and band gains as the parameters currently hold them.
    ///         For the console's state frame, so it can show what the plugin is
    ///         actually doing rather than what its own localStorage remembers.
    ///         Message thread; reads atomics.
    [[nodiscard]] bool  uiEqEnabled() const noexcept;
    [[nodiscard]] float uiEqGain(int band) const noexcept;

private:
    void updateLatency() noexcept;

    /// Snapshot everything the host cannot express as a parameter: the drawn
    /// registration, the console trims, the reverb space, the master EQ and the
    /// page-turn bindings. Message thread.
    [[nodiscard]] juce::ValueTree captureConsoleState() const;

    /// Apply a snapshot produced by @ref captureConsoleState. Message thread.
    void applyConsoleState(const juce::ValueTree& state);

    /// Off-thread: allocate the voice pool, build one voicing per stop, and bind
    /// both into the engine. Called from prepareToPlay only -- a later registration
    /// change publishes a different set of the SAME voicings and rebuilds nothing.
    void buildInstrument(double sampleRate, std::size_t maxBlockFrames);

    /// Choose a sensible opening registration (a principal chorus + flute on the
    /// primary manual) and the manual MIDI is routed to. Off-thread.

    // --- pure engine (owns all DSP/synthesis behind the core seam) ----------
    core::engine::AudioEngine engine_;

    // --- the instrument specification (immutable once built) ----------------
    // The console lays itself out from this, and the registration is a subset of
    // its stops. Built once off-thread in the constructor.
    // ORDER MATTERS. The parameter mirror's layout is built FROM the organ -- the
    // stop parameters take their names from its stops and their DEFAULTS from its
    // opening plenum -- so the organ must be constructed first. That is what makes
    // the host and the instrument agree from the first instant, including across
    // the host's own "reset to default", and it is why the opening registration is
    // no longer chosen in the constructor body.
    model::Organ            organ_;

    /// Where @ref organ_ came from; empty means the one built into the binary.
    /// Saved with the document so a session reopens the same instrument.
    juce::String            organPath_;

    /// @see unresolvedOrganPath. Cleared by any successful load, including a
    /// deliberate revert to the built-in organ -- that is the user choosing, and
    /// holding on to the old path past that point would be second-guessing them.
    juce::String            unresolvedOrgan_;

    /// Swap in a compiled organ and rebuild everything that depends on it.
    /// Message thread, audio suspended by the caller.
    void adoptOrgan(model::Organ&& organ, juce::String path);
    core::DivisionId        playDivision_{};   ///< Primary manual for MIDI + key lights.

    /// THE registration. One mask, keyed by StopId::value, and the only answer to
    /// "what is drawn?" in this class.
    ///
    /// It replaced two members that disagreed: a StopId-keyed `engaged_` bitmap
    /// that the composite builder read, and a family+footage `currentRanks_` list the
    /// console wrote. prepareToPlay rebuilt from one of them, so a host sample-rate
    /// or buffer-size change silently reverted the sounding registration to the
    /// factory plenum while the console went on displaying the user's stops.
    ///
    /// The authoritative copy lives in the host parameters; this is the audio
    /// thread's cached view of them, and applyRegistration is the only writer.
    std::uint64_t           registration_ = 0;

    /// THE couplers. One mask, keyed by the organ's coupler index, and the audio
    /// thread's cached view of the host parameters that own them.
    std::uint32_t           couplers_ = 0;

    /// A coupler change the audio thread noticed, waiting for the message thread.
    std::atomic<std::uint32_t> pendingCouplers_{ 0 };
    std::atomic<bool>          pendingCouplersValid_{ false };

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
    /// The sounding tuning, and the only thing the voices ever point at.
    ///
    /// A LiveTuning rather than a TuningModel because a model rebuilds its table in
    /// place, and a note-on reading that table mid-rebuild is a data race. Its
    /// address is stable for the life of the instrument, so changing temperament
    /// costs a snapshot handoff rather than re-pointing five hundred voices.
    tuning::LiveTuning                                  liveTuning_;

    /// A temperament change the audio thread noticed, packed into one word so the
    /// two halves cannot be read from different parameter snapshots: the choice
    /// index in the high 32 bits, the A4 reference as float bits in the low 32.
    ///
    /// -1 in the high half means nothing pending.
    std::atomic<std::uint64_t> pendingTuning_{ 0 };
    std::atomic<bool>          pendingTuningValid_{ false };

    /// What the audio thread last saw, so a change is sent once and not every block.
    int   sentTemperament_ = -1;
    float sentTuningA4_    = 0.0f;

    /// Off-thread: rebuild the table for @p choice / @p a4 and hand it over.
    void republishTuning(int choice, float a4Hz);

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

    /// Console swell shoe -> audio. Its own ring rather than a shared one because
    /// the SPSC contract is one producer, one consumer: the message thread writes
    /// here and the audio thread drains it at the top of the block, exactly as the
    /// keyboard does.
    ///
    /// Sixty-four slots is ample for a shoe: it is dragged, so the message thread
    /// produces at pointer-move rate and the audio thread empties it every block.
    struct UiExpressionEvent { core::DivisionId division{}; float position = 1.0f; };
    core::engine::SpscRing<UiExpressionEvent, 64> uiExpression_;
    juce::MidiBuffer        uiScratch_;        ///< Pre-reserved: UI notes -> command bridge, no RT alloc.

    /// Host-facing output trim (NOT part of the synthesis chain), applied post-engine
    /// and smoothed to avoid zipper noise. // TODO(phase0.3): fold gain staging into
    /// the engine master chain if a pre-reverb trim is ever wanted.
    juce::LinearSmoothedValue<float> masterGain_;

    /// Left over from the polyphony compensation that was REMOVED: an organ sums
    /// acoustically, so a ~1/(1+k*(N-1)) trim ducked dense passages and breathed as
    /// chords were released (the reasoning is in @c processBlock). This value is
    /// still reset in prepareToPlay but never applied to any buffer.
    /// @todo Delete the member once nothing refers to it.
    juce::LinearSmoothedValue<float> polyGain_;

    /// Console GAIN (pre-limiter drive, 0..2, default 1) — the "how hard we push".
    std::atomic<float> uiMaster_{ 1.0f };
    /// Console VOLUME (post-limiter output level, 0..1, default 1) — the clean
    /// final trim, applied after the limiter so it never re-introduces clipping.
    std::atomic<float> uiVolume_{ 1.0f };

    /// Set by the console (stop demo / release keys); the audio thread panics and
    /// clears it. Fixes notes sticking when a demo is stopped mid-phrase.
    std::atomic<bool> uiPanic_{ false };

    /// The wind supply. Owned HERE rather than by the engine because it is compiled
    /// from the organ, which is the processor's; the engine holds a pointer.
    ///
    /// It must outlive the engine's pointer to it, which member order guarantees:
    /// declared after engine_, so it is destroyed before it.
    wind::WindModel wind_;

    // NOTE: there is no held-note replay here any more, and no need for one. A
    // registration change publishes a rank table; the ranks that were already
    // sounding are not touched, and the engine gives a retired rank a real note-off
    // and a newly drawn one a real note-on on every key that is still down.

    /// Which notes the page-turn filter actually swallowed. A note-off may only be
    /// swallowed if its note-on was: without this, enabling the feature while one
    /// of the bound notes was already sounding ate its note-off and left the note
    /// stuck until a panic.
    std::bitset<128> navSwallowed_{};

    /// Console trims, smoothed. Reading the atomics straight into a per-block gain
    /// stepped the level at every block boundary — zipper noise while dragging,
    /// and an audible click on any large jump.
    juce::LinearSmoothedValue<float> uiMasterSmooth_;
    juce::LinearSmoothedValue<float> uiVolumeSmooth_;

    /// The registration the console last sent, kept so it can be saved. This is
    /// the ONLY authoritative copy of what is actually sounding.
    /// Where a registration change came from, which decides whether the host
    /// parameters need writing back. A change that CAME from the parameters must
    /// not be written to them again -- not because it would misbehave (the
    /// idempotence guard handles that) but because it would put a redundant point
    /// into every automation lane it touched.
    enum class RegistrationOrigin
    {
        HostParameters, ///< The per-block diff saw the host move something.
        Console,        ///< A drawstop click, a piston, a selector query.
        Restore         ///< setStateInformation, or a construction-time default.
    };

    /// The single writer of registration_. Idempotent: a set equal to the one
    /// already engaged returns immediately, which is what makes the round trip
    /// through the parameters terminate instead of oscillating.
    void applyRegistration(std::uint64_t next, RegistrationOrigin origin);

    /// Draw / retire couplers and republish. Message thread; same single-writer
    /// discipline as @ref applyRegistration, and idempotent for the same reason.
    void applyCouplers(std::uint32_t next, RegistrationOrigin origin);

    /// Every registration this organist has been through, as a tree.
    ///
    /// Only DELIBERATE gestures are recorded -- Console, which covers a drawstop
    /// click, a piston and a learned tab. Host automation is excluded on purpose:
    /// a lane sweeping the stops would record a node per block, this history has
    /// no cap, and a host already has its own undo for its own controls. Restore
    /// is excluded because reopening a project is not a move to take back.
    registration::RegistrationHistory history_;

    /// The registration as the history stores it: stops AND couplers together,
    /// because taking back "I drew the Récit reeds and coupled them" has to take
    /// back both or it takes back neither usefully.
    [[nodiscard]] registration::RegistrationState currentRegistrationState() const;

    /// Record the state as it now stands, under @p kind.
    void recordRegistration(registration::RegistrationCommand::Kind kind);

    /// Draw exactly @p state, without recording it -- an undo is not a new move.
    void applyRegistrationState(const registration::RegistrationState& state);

    /// Fill the first few generals from the organ, using the shared selector
    /// grammar. Construction only.
    void buildDefaultGenerals();

    /// Hand the engine the ranks the current registration draws.
    ///
    /// One voice per (rank, note): the engine fans a key out across this table, so
    /// this is what makes a registration audible. Publishing an EMPTY table is
    /// meaningful — nothing drawn, correctly silent.
    void publishEngagedRanks();

    /// Every stop's voicing, indexed by StopId::value, built once per prepare.
    ///
    /// A rank's voicing is a property of its STOP -- family, footage, spectrum,
    /// speech -- not of the registration, so drawing and retiring stops never
    /// rebuilds any of this. The engine holds pointers into it, which is safe
    /// because it is only ever rebuilt in prepareToPlay, with the callback stopped.
    std::vector<synth::RankVoicing> rankVoicings_;

    /// Bumped on every table publication. Diagnostic, and the handle a voice would
    /// use to notice its rank changed underneath it.
    std::uint32_t registrationEpoch_ = 0;

    /// Piston memory. Message thread owns both; the audio thread never reads them.
    std::array<std::uint64_t, kNumGenerals> generals_{};
    std::bitset<kNumGenerals>               generalsSet_{};

    /// A program change seen by the audio thread, waiting for the message thread.
    /// -1 is "nothing pending"; exchange(-1) is how it is claimed.
    std::atomic<int> pendingProgram_{ -1 };

    /// Which piston last fired, for the console's flash. -1 once consumed.
    std::atomic<int> lastGeneral_{ -1 };

    /// Whether @ref pendingHostRegistration_ actually holds something unconsumed.
    ///
    /// Two producers share one AsyncUpdater dispatch now. Without this, a program
    /// change that triggered the update would also re-apply whatever registration
    /// happened to be sitting in the host slot -- and since the piston writes the
    /// parameters, that stale value is the registration the piston just replaced.
    std::atomic<bool> pendingHostRegistrationValid_{ false };

    /// The audio thread saw the host move a stop parameter; rebuild off-thread.
    ///
    /// juce::AsyncUpdater, because the rebuild allocates: seedFrom() does not, but
    /// the composite spectrum it is handed does, and the audio thread must not.
    void handleAsyncUpdate() override;

    /// What the audio thread last read out of the stop parameters, waiting for the
    /// message thread to act on it. Written by the audio thread, read by the
    /// message thread, so it is atomic; nothing else synchronises on it, so
    /// relaxed is enough.
    std::atomic<std::uint64_t> pendingHostRegistration_{ 0 };
    int   reverbSpace_ = 2;      ///< Console space index (2 = Hall).
    float reverbMix_   = 0.28f;  ///< Console reverb mix.

    /// The console's reverb set, published to the audio thread.
    ///
    /// setUiReverb used to call reverb_.setParams() directly under
    /// getCallbackLock(). That is the message thread taking a lock the audio
    /// callback also takes -- so a UI click could stall the callback -- and it
    /// bypassed the command ring, which this design says is the only channel from
    /// the message thread into the engine. Publishing here costs a copy and an
    /// exchange, and the audio thread picks it up on its next block.
    core::TripleBuffer<core::ReverbParams> uiReverb_{};

    /// What the host's getTailLengthSeconds() answers from.
    ///
    /// It used to read reverb_.params() -- plain floats owned by the audio thread
    /// -- from the message thread, which is a data race a sanitiser would be right
    /// about. The audio thread republishes these after every reverb command.
    std::atomic<float> tailDecaySec_{ 2.6f };    ///< Hall's decay, until the first block.
    std::atomic<float> tailPreDelayMs_{ 18.0f }; ///< Hall's pre-delay.

    /// Build and publish the console's reverb set from reverbSpace_/reverbMix_.
    void publishConsoleReverb();

    /// Copy the EQ parameters into masterEq_. The ONLY writer of that object.
    ///
    /// It used to be written from the message thread on every console click while
    /// the audio thread filtered with it, and read back from the message thread
    /// for state saving. One owner, one direction: a click moves a parameter, and
    /// this moves the EQ. Audio thread; RT-safe (cached pointers, no lookups).
    void applyEqParameters() noexcept;

    /// The EQ parameters, resolved once so the audio thread never looks a
    /// parameter up by string.
    std::array<std::atomic<float>*, dsp::MasterEq::kBands> eqBandParam_{};
    std::atomic<float>*                                    eqOnParam_ = nullptr;

    // --- Sequencer page-turn state ------------------------------------------
    std::atomic<int>  seqPrevNote_{ 83 };     ///< MIDI note for "previous" (si5 default).
    std::atomic<int>  seqNextNote_{ 84 };     ///< MIDI note for "next" (do6 default).
    std::atomic<bool> seqNavEnabled_{ true }; ///< Master enable for the page-turn keys.
    // --- MIDI learn ---------------------------------------------------------

    /// The learned bindings and the capture machine. BOTH message-thread only:
    /// installing a binding writes a table and resolving one parses a selector,
    /// and neither belongs on the audio thread.
    midi::MidiMap   midiMap_{};
    midi::MidiLearn midiLearn_{};

    /// The audio thread's half: one bit per control, and the verdict it turns an
    /// incoming event into. It is core code, and deliberately so -- written here it
    /// was the one piece of a learned drawstop the headless suite could not reach,
    /// because the suite links caecilia::core and there is no plugin harness.
    ///
    /// Published rather than shared: 512 bytes across a triple buffer, instead of
    /// the thirty kilobytes a MidiMap occupies and the question of whether it is
    /// safe to copy there.
    core::TripleBuffer<midi::LearnedControls> boundControls_{};

    /// The audio thread's own copy, refreshed only when the message thread has
    /// published a new one. A triple-buffer read per EVENT would be an atomic
    /// exchange per note.
    midi::LearnedControls boundSnapshot_{};

    /// Set while a learn is pending, so the audio thread knows to capture rather
    /// than to play.
    std::atomic<bool> midiLearnArmed_{ false };

    /// Audio -> message: packed MidiEvents that either hit a binding or completed
    /// a learn. One producer (the audio thread), one consumer (handleAsyncUpdate).
    core::engine::SpscRing<std::uint32_t, 64> midiActions_{};

    /// Recompute @ref boundControls_ from @ref midiMap_ and publish it. Message
    /// thread; called after every binding edit.
    void publishBoundControls();

    /// Drain @ref midiActions_ on the message thread: capture a learn, or fire the
    /// binding an event matched.
    void handleMidiActions();

    /// Apply one learned binding's action. Message thread.
    void applyMidiBinding(const midi::MidiLearnBinding& binding);

    /// Arm learn on an arbitrary action.
    void armMidiLearn(const midi::RegistrationCommandTemplate& target);

    /// Drop every binding whose action equals @p target.
    void clearMidiBindingFor(const midi::RegistrationCommandTemplate& target);

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
