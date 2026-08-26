// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/core/TripleBuffer.h"
#include "caecilia/dsp/OnePole.h"
#include "caecilia/engine/DeadlineBudget.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/engine/MeterSnapshot.h"
#include "caecilia/engine/RenderContext.h"
#include "caecilia/engine/SpscRing.h"
#include "caecilia/engine/VoicePool.h"
#include "caecilia/engine/VoiceScheduler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Namespace note: unlike the sibling domain modules (each mapped to a single
// sub-namespace, e.g. dsp -> caecilia::dsp), the engine module deliberately
// nests under core as caecilia::core::engine. It is the real-time seam of the
// pure core library, not a peer domain module, so it reads as "the core's
// engine". This nesting does NOT imply core depends on engine (engine -> core
// only). See src/caecilia/engine/README.md ("Namespace — a recorded exception").
namespace caecilia::core::engine
{

// ---------------------------------------------------------------------------
// Fixed engine capacities. Chosen once; all hot-path storage is sized from
// these so nothing on the audio thread ever allocates.
// ---------------------------------------------------------------------------

/// Maximum simultaneously sounding voices across the whole instrument.
inline constexpr std::size_t kMaxVoices = 1024;

/// Depth of the inbound command ring (power of two; one slot reserved).
inline constexpr std::size_t kCommandQueueCapacity = 4096;

/// Upper bound on windchest accumulation buses.
inline constexpr std::size_t kMaxWindchests = 64;

/**
 * @brief A windchest that sits inside a swell box, and whose shoe closes it.
 *
 * Enclosure is a property of the DIVISION and the accumulation buses are per
 * WINDCHEST, so something has to say which shoe governs which bus. On a real
 * instrument the answer is unambiguous because the box encloses the chest: an
 * enclosed division has its own.
 */
struct ChestEnclosure
{
    WindchestId chest{};    ///< The bus to filter.
    DivisionId  division{}; ///< Whose expression shoe closes it.
};

/**
 * @brief The single, narrow seam the JUCE layer sees — the entire pure engine
 *        behind one class.
 *
 * @c AudioEngine owns the voice pool, the scheduler, the wind/tuning/reverb
 * bindings and the per-windchest accumulation buses. Its @ref processBlock is
 * the whole audio callback body: it drains the command ring, renders every
 * active voice into its chest bus under a deadline budget, mixes the chests
 * down, runs the bound master reverb and publishes a metering snapshot for the
 * UI — with no allocation, no locks and no exceptions.
 *
 * @todo Advancing the wind model belongs in that list and is missing: the wind
 *       stage is an empty stub. See AudioEngine::stepWind().
 *
 * NOTHING JUCE appears in any signature; the only audio-buffer type is the core
 * @c AudioBlock. Cross-thread state changes arrive EXCLUSIVELY through the SPSC
 * @ref commandQueue; cross-thread reads happen EXCLUSIVELY through the
 * triple-buffered @ref latestMeters snapshot.
 *
 * @c OrganEngine is provided as a domain-facing alias of this type.
 */
class AudioEngine
{
public:
    AudioEngine() noexcept = default;

    // --- off-thread setup (NOT real-time safe) ------------------------------

    /**
     * @brief Allocate and precompute everything the audio thread will need.
     * @param sampleRate     Host sample rate in Hz (> 0).
     * @param maxBlockFrames Largest block @ref processBlock will receive.
     * @param numChannels    Output channel count (typically 2).
     * @param numWindchests  Windchests to allocate buses for (clamped to
     *                       @ref kMaxWindchests; at least 1).
     *
     * The ONLY place allocation happens. Call from prepareToPlay, never on the
     * audio thread.
     */
    void prepare(SampleRate   sampleRate,
                 std::size_t  maxBlockFrames,
                 std::size_t  numChannels,
                 std::size_t  numWindchests = 1);

    /**
     * @brief Bind the pre-allocated voice objects (owned by the synthesis-side
     *        arena) into the pool.
     * @param voices Array of @p count non-owning @c IVoice pointers.
     * @param count  Number of voices to bind (clamped to @ref kMaxVoices).
     *
     * Off-thread; the voices must already be @c prepare()-ed and outlive the
     * engine.
     */
    void bindVoices(IVoice* const* voices, std::size_t count) noexcept;

    /// Bind the wind snapshot source (read-only on the audio thread). It is read
    /// for pipe->chest routing and the wind meters, but never advanced: see
    /// AudioEngine::stepWind().
    void setWindSupply(IWindSupply* wind) noexcept { wind_ = wind; }

    /**
     * @brief Declare which windchests are inside a swell box. Off-thread.
     *
     * A chest not named here is never filtered and costs nothing. Chests past
     * @ref kMaxWindchests are dropped rather than allocated for.
     */
    void setEnclosedChests(std::span<const ChestEnclosure> enclosed) noexcept;

    /// Bind the per-pipe tuning table (read-only on the audio thread).
    void setTuning(const ITuning* tuning) noexcept { tuning_ = tuning; }

    /// Bind the master-chain reverb (processed in place each block).
    void setMasterReverb(IReverb* reverb) noexcept { masterReverb_ = reverb; }

    /// Choose how the pool steals under polyphony / budget pressure.
    void setStealPolicy(StealPolicy policy) noexcept { scheduler_.setStealPolicy(policy); }

    /// Set the per-block CPU budget in abstract cost units (see DeadlineBudget).
    void setBlockBudget(float budgetUnits) noexcept { blockBudgetUnits_ = budgetUnits; }

    // --- audio thread (real-time safe, noexcept) ----------------------------

    /**
     * @brief Render one block: drain commands, step wind, render + mix voices,
     *        apply the master chain, publish meters.
     * @param output Host output block (the engine writes it, overwriting).
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    void processBlock(AudioBlock& output) noexcept;

    /**
     * @brief Producer handle for the inbound command ring.
     * @return The SPSC ring; push @c EngineCommand values from ONE upstream
     *         thread (the MIDI/registration/parameter aggregator).
     */
    [[nodiscard]] SpscRing<EngineCommand, kCommandQueueCapacity>& commandQueue() noexcept
    {
        return commandRing_;
    }

    /**
     * @brief Latest published metering snapshot (wait-free reader).
     * @return A consistent, non-torn frame for the UI to poll.
     */
    [[nodiscard]] MeterSnapshot latestMeters() const noexcept
    {
        return meters_.read();
    }

    /// @return Voices sounding after the most recent block.
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept { return pool_.activeCount(); }

    /**
     * @brief Discard everything in flight: voices, sustain, meters.
     *
     * What a host means by reset. Synchronous and complete, unlike enqueuing a
     * panic -- which is a musical all-notes-off, starts a release rather than
     * stopping it, and would not take effect until the next block anyway.
     *
     * Safe from the audio thread (RT-safe, allocation-free) or from the message
     * thread with the callback stopped.
     */
    void reset() noexcept;

    /**
     * @brief Publish which ranks a key should sound.
     *
     * Message thread. The audio thread picks it up at the top of its next block,
     * so a registration change lands within one block rather than synchronously --
     * which is also how long a real drawstop takes to move.
     *
     * Publishing an EMPTY table is meaningful: it means no rank is drawn, and the
     * instrument is correctly silent.
     */
    void setEngagedRanks(const EngagedRankTable& table) noexcept
    {
        engagedRanks_.write(table);
    }

    /// @return Configured sample rate (0 until @ref prepare).
    [[nodiscard]] SampleRate sampleRate() const noexcept { return sampleRate_; }

private:
    // --- processBlock stages ------------------------------------------------

    /// Render exactly one slice, guaranteed to be <= maxBlockFrames_.
    void        renderSlice(AudioBlock& slice) noexcept;

    /**
     * @brief Apply every queued command timestamped at or before @p throughFrame,
     *        and report the frame the next one wants.
     * @param throughFrame Frame within the block the renderer has reached.
     * @param totalFrames  Length of the whole block.
     * @return The frame at which the next queued command takes effect, or
     *         @p totalFrames when nothing is left. ALWAYS strictly greater than
     *         @p throughFrame, which is what guarantees the render loop advances.
     */
    [[nodiscard]] std::size_t drainCommandsThrough(std::size_t throughFrame,
                                                   std::size_t totalFrames) noexcept;
    void        applyCommand(const EngineCommand& cmd) noexcept;
    void        stepWind(std::size_t numFrames) noexcept;
    void        zeroChestBuses(std::size_t numFrames) noexcept;
    RenderContext makeContext(std::size_t numFrames) noexcept;
    void        mixBusesToOutput(AudioBlock& output, std::size_t numFrames) noexcept;
    void        applyMasterChain(AudioBlock& output) noexcept;

    /// Start a fresh meter accumulation for a whole block.
    void        beginMeters() noexcept;
    /// Fold one slice's peak and energy into the block's accumulation.
    void        accumulateMeters(const AudioBlock& output, std::size_t numFrames) noexcept;
    /// Finish the block's accumulation and hand it to the UI.
    void        publishMeters() noexcept;

    // --- command handlers ---------------------------------------------------

    void handleNoteOn(const EngineCommand::NoteOnPayload& p) noexcept;

    /// One rank sounding one note: what a key press resolves to, once.
    struct KeyVoice
    {
        const EngagedRank* rank = nullptr;
        MidiNote           note = 0;
    };

    /// The most voices one key may call for.
    ///
    /// Sixty-four ranks is the registration mask's width, and an octave coupler can
    /// ask a rank for a second note -- so this covers every rank twice over. A key
    /// that somehow exceeded it drops the excess rather than overrunning: silent in
    /// one rank beats undefined behaviour in all of them.
    static constexpr std::size_t kMaxKeyVoices = 128;

    /**
     * @brief Everything a key on @p division should be sounding, couplers included.
     * @return How many entries were written into @p out.
     *
     * Couplers do NOT chain: a coupler borrows the ranks of its source division as
     * they are, not as that division would sound them with its own couplers drawn.
     * This organ declares Récit/Pédale separately from Récit/Grand-Orgue plus
     * Grand-Orgue/Pédale, which is exactly what an instrument does when the two are
     * not the same thing.
     */
    [[nodiscard]] std::size_t expandKey(MidiNote note, DivisionId division,
                                        const EngagedRankTable& table,
                                        KeyVoice* out) const noexcept;

    /// Is some OTHER held key already calling for this rank at this note?
    ///
    /// A pipe sounds while ANY key holds it. Two keys reaching the same pipe -- the
    /// Récit's own key and a Grand-Orgue key through the coupler -- must start it
    /// once and release it when the LAST of them comes up, which is what a pallet
    /// does and what makes coupled playing survive an organist letting go of one
    /// hand.
    [[nodiscard]] bool heldElsewhere(const KeyVoice& v, MidiNote exceptNote,
                                     DivisionId exceptDivision,
                                     const EngagedRankTable& table) const noexcept;

    /// Acquire and trigger one voice for one (rank, note). @return false if the
    /// pool had nothing left, which the caller has already made impossible.
    bool startRankVoice(const EngagedRank& rank, MidiNote note, Velocity velocity) noexcept;

    /// Bring the keys that are currently DOWN into line with a new rank table.
    ///
    /// Drawing or retiring a stop under a held chord is the most ordinary thing an
    /// organist does, and without this it did nothing until the key came up.
    void reconcileHeldKeys(const EngagedRankTable& next) noexcept;
    void handleNoteOff(const EngineCommand::NoteOffPayload& p) noexcept;
    void handlePanic() noexcept;
    void handleSustain(const EngineCommand::SustainPayload& p) noexcept;
    void handleExpression(const EngineCommand::ExpressionPayload& p) noexcept;

    /// Advance every shoe toward its target and publish this block's ramps.
    void stepExpression(std::size_t numFrames) noexcept;

    // --- configuration ------------------------------------------------------

    SampleRate  sampleRate_     = 0.0;
    std::size_t maxBlockFrames_ = 0;
    std::size_t numChannels_    = 0;
    std::size_t numWindchests_  = 0;
    float       blockBudgetUnits_ = 0.0f;

    // --- bound collaborators (non-owning) -----------------------------------

    IWindSupply*   wind_         = nullptr;
    const ITuning* tuning_       = nullptr;
    IReverb*       masterReverb_ = nullptr;

    // --- owned engine state -------------------------------------------------

    VoicePool<kMaxVoices>                          pool_{};
    VoiceScheduler                                 scheduler_{};
    DeadlineBudget                                 budget_{};
    SpscRing<EngineCommand, kCommandQueueCapacity> commandRing_{};

    std::uint64_t framePos_ = 0; ///< Absolute frame counter across blocks.

    /// Frames in the block currently being rendered, so a slice can work out what
    /// fraction of the block's CPU budget it is entitled to spend.
    std::size_t   blockFrames_ = 0;

    /// Sustain-pedal state per division. A note-off arriving while the pedal is
    /// down marks its voice held rather than releasing it; lifting the pedal
    /// releases everything it was holding on that division.
    static constexpr std::size_t kMaxDivisions = 256;
    std::array<bool, kMaxDivisions> sustainDown_{};

    // --- swell shoes ---------------------------------------------------------
    //
    // Two arrays because a shoe is dragged: `target` is where the pedal is now,
    // `current` is where the gain has got to. The gap between them is glided over
    // rather than stepped, and the glide is published as a per-sample ramp so it
    // is smooth WITHIN a block as well as across blocks -- a per-block step under
    // a sustained chord is exactly the zipper this exists to avoid.
    //
    // Both start fully open. An unenclosed division has no shoe at all, and a
    // division nobody has sent CC 11 for must not be silent.
    static constexpr std::size_t kExpressionDivisions =
        RenderContext::kMaxExpressionDivisions;
    std::array<float, kExpressionDivisions> expressionTarget_{};

    /// Which division's shoe closes each chest's bus; -1 for an open chest.
    ///
    /// A plain index rather than a DivisionId so "not enclosed" has a
    /// representation, and so the per-block loop is a compare against -1 rather
    /// than a lookup.
    std::array<std::int16_t, kMaxWindchests> shoeForChest_{};

    /// The shutter, one pole per chest per channel. Sized at prepare.
    ///
    /// Per CHEST, not per voice: a biquad per note is a cost the instrument pays
    /// forever, where this is three filters running whether one pipe is sounding
    /// or two hundred.
    std::vector<dsp::OnePole> shutters_;

    /// Roll the swell shutters over the enclosed chests' buses.
    void applyShutters(std::size_t numFrames) noexcept;
    std::array<float, kExpressionDivisions> expressionCurrent_{};
    std::array<RenderContext::ExpressionRamp, kExpressionDivisions> expressionRamp_{};

    /// Seconds for a shoe to travel the whole gap. Fast enough to feel connected
    /// to the pedal, slow enough that a controller sending 128 discrete CC steps
    /// does not sound like 128 steps.
    static constexpr double kExpressionGlideSeconds = 0.030;

    // --- per-windchest accumulation buses (allocated in prepare) ------------

    std::vector<float>      busSamples_{};     ///< Flat [chest*chan*maxFrames] storage.
    std::vector<float*>     busChannelPtrs_{}; ///< [chest*chan] pointers into busSamples_.
    std::vector<AudioBlock> busBlocks_{};      ///< [chest] views rebuilt per block.

    // --- metering handoff to the UI ------------------------------------------
    // TripleBuffer rather than a two-slot flip: with two slots the writer wraps
    // back onto the slot the reader is mid-copy on. See core/TripleBuffer.h.

    /// The engaged ranks, handed over by the message thread.
    ///
    /// TripleBuffer rather than a pointer swap: the table is read at the top of
    /// every block and written whenever a drawstop moves, and a torn read here
    /// would fan a note out across a rank list that never existed.
    TripleBuffer<EngagedRankTable> engagedRanks_{};
    EngagedRankTable               ranks_{};   ///< The audio thread's copy.

    /// The wind one sounding voice draws, per pool slot, set when the voice starts.
    ///
    /// Per SLOT rather than accumulated into a running total, because a voice can
    /// leave in more ways than it arrives -- released, stolen, silenced by a panic,
    /// retired with its rank -- and a running total has to be decremented correctly
    /// on every one of them. Summing the live slots once a block cannot drift.
    std::array<float, kMaxVoices>  slotFlow_{};

    /// The windchest each pool slot's voice belongs to, recorded when it starts.
    ///
    /// Beside slotFlow_ and for the same reason: the answer is fixed for the life
    /// of the voice, so it is written once rather than searched for every block.
    std::array<std::uint16_t, kMaxVoices> slotChest_{};

    /// The keys physically down, so a registration change knows what to reconcile.
    ///
    /// Not derivable from the pool: a voice in its release tail is still active but
    /// its key is up, and starting a newly drawn rank on it would sound a note
    /// nobody is holding.
    struct HeldKey { MidiNote note = 0; DivisionId division{}; Velocity velocity = 0; };
    static constexpr std::size_t kMaxHeldKeys = 128;
    std::array<HeldKey, kMaxHeldKeys> heldKeys_{};
    std::size_t                       heldKeyCount_ = 0;

    void rememberHeldKey(MidiNote note, DivisionId division, Velocity velocity) noexcept;
    void forgetHeldKey(MidiNote note, DivisionId division) noexcept;

    TripleBuffer<MeterSnapshot> meters_{};
    MeterSnapshot               pendingMeters_{};

    // The block's RMS has to be accumulated across its slices and divided once at
    // the end. Computing it per slice and publishing each time meant the UI read
    // whatever the LAST slice happened to contain -- which, on a block cut at a
    // note-off, is a 32-frame tail.
    double        meterSumSq_   = 0.0;
    std::size_t   meterSamples_ = 0;
};

/// Domain-facing alias: the organ engine seam. See @ref AudioEngine.
using OrganEngine = AudioEngine;

} // namespace caecilia::core::engine
