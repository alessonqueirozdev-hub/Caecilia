/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IReverb.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/engine/DeadlineBudget.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/engine/MeterSnapshot.h"
#include "caecilia/engine/RenderContext.h"
#include "caecilia/engine/SpscRing.h"
#include "caecilia/engine/VoicePool.h"
#include "caecilia/engine/VoiceScheduler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
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
 * @brief The single, narrow seam the JUCE layer sees — the entire pure engine
 *        behind one class.
 *
 * @c AudioEngine owns the voice pool, the scheduler, the wind/tuning/reverb
 * bindings and the per-windchest accumulation buses. Its @ref processBlock is
 * the whole audio callback body: it drains the command ring, advances the wind
 * model, renders every active voice into its chest bus under a deadline budget,
 * mixes the chests down, applies the master DSP chain and publishes a metering
 * snapshot for the UI — with no allocation, no locks and no exceptions.
 *
 * NOTHING JUCE appears in any signature; the only audio-buffer type is the core
 * @c AudioBlock. Cross-thread state changes arrive EXCLUSIVELY through the SPSC
 * @ref commandQueue; cross-thread reads happen EXCLUSIVELY through the
 * double-buffered @ref latestMeters snapshot.
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

    /// Bind the per-block wind snapshot source (read-only on the audio thread).
    void setWindSupply(IWindSupply* wind) noexcept { wind_ = wind; }

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
        return meters_[meterFront_.load(std::memory_order_acquire)];
    }

    /// @return Voices sounding after the most recent block.
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept { return pool_.activeCount(); }

    /// @return Configured sample rate (0 until @ref prepare).
    [[nodiscard]] SampleRate sampleRate() const noexcept { return sampleRate_; }

private:
    // --- processBlock stages ------------------------------------------------

    void        drainCommands() noexcept;
    void        stepWind(std::size_t numFrames) noexcept;
    void        zeroChestBuses(std::size_t numFrames) noexcept;
    RenderContext makeContext(std::size_t numFrames) noexcept;
    void        mixBusesToOutput(AudioBlock& output, std::size_t numFrames) noexcept;
    void        applyMasterChain(AudioBlock& output) noexcept;
    void        captureMeters(const AudioBlock& output, std::size_t numFrames) noexcept;
    void        publishMeters() noexcept;

    // --- command handlers ---------------------------------------------------

    void handleNoteOn(const EngineCommand::NoteOnPayload& p) noexcept;
    void handleNoteOff(const EngineCommand::NoteOffPayload& p) noexcept;
    void handlePanic() noexcept;

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

    // --- per-windchest accumulation buses (allocated in prepare) ------------

    std::vector<float>      busSamples_{};     ///< Flat [chest*chan*maxFrames] storage.
    std::vector<float*>     busChannelPtrs_{}; ///< [chest*chan] pointers into busSamples_.
    std::vector<AudioBlock> busBlocks_{};      ///< [chest] views rebuilt per block.

    // --- double-buffered metering -------------------------------------------

    MeterSnapshot          meters_[2]{};
    MeterSnapshot          pendingMeters_{};
    std::atomic<int>       meterFront_{0}; ///< Index the UI currently reads.
};

/// Domain-facing alias: the organ engine seam. See @ref AudioEngine.
using OrganEngine = AudioEngine;

} // namespace caecilia::core::engine
