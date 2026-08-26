// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/engine/StealPolicy.h"
#include "caecilia/engine/VoiceBatchView.h"
#include "caecilia/engine/VoiceHandle.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::core::engine
{

/**
 * @brief A fixed-capacity, lock-free pool of voice slots with generation-tagged
 *        handles and per-@c EngineKind Structure-of-Arrays grouping.
 *
 * The pool does NOT own the concrete voice objects — those are polymorphic
 * @c IVoice implementations from the synthesis module, pre-allocated once and
 * @ref bind() -ed in. What the pool owns is the RT-safe LIFECYCLE: an intrusive
 * free-list for O(1) @ref acquire / @ref release, generation counters that make
 * a stolen slot reject stale @c VoiceHandle references, and a per-block
 * @ref refresh() pass that culls finished voices and sorts the survivors by
 * engine kind so unisons render as contiguous same-kind runs; the vectorised
 * kernel over those runs is still to come.
 *
 * ## Real-time contract
 * - @ref bind runs off the audio thread (it only stores caller-owned pointers;
 *   it does not allocate).
 * - @ref acquire, @ref release, @ref resolve, @ref refresh, @ref view,
 *   @ref batch and @ref chooseVictim are all @c noexcept, allocation-free and
 *   lock-free — safe on the audio thread.
 *
 * @tparam Capacity Maximum simultaneous voices. Must fit a 16-bit index.
 */
template <std::size_t Capacity>
class VoicePool
{
public:
    static_assert(Capacity >= 1, "VoicePool needs at least one slot");
    static_assert(Capacity < VoiceHandle::kInvalidIndex, "VoicePool capacity must fit a 16-bit index");

    /// Number of distinct @c EngineKind buckets used for SoA grouping.
    static constexpr std::size_t kNumEngineKinds = 4;

    VoicePool() noexcept { clearLifecycle(); }

    /// @return Maximum number of slots this pool can hold.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// @return Number of slots currently bound to a voice.
    [[nodiscard]] std::size_t boundCount() const noexcept { return boundCount_; }

    /// @return Number of voices marked active after the last @ref refresh().
    [[nodiscard]] std::size_t activeCount() const noexcept { return activeCount_; }

    /// @return Slots currently on the free list.
    ///
    /// One key is several voices now — one per engaged rank — so the engine has to
    /// ask whether a WHOLE key will fit before it acquires the first of them. A
    /// chord where some ranks speak and others do not is worse than one that does
    /// not speak: the player cannot tell what happened or how to avoid it.
    [[nodiscard]] std::size_t freeCount() const noexcept { return freeCount_; }

    /**
     * @brief Bind pre-allocated voices into the pool and reset all lifecycle
     *        state. Call from @c prepare(), never on the audio thread.
     * @param voices Array of at least @p count non-owning @c IVoice pointers.
     * @param count  Number of voices to bind (clamped to @ref capacity()).
     *
     * The pointers must outlive the pool; the synthesis-side arena owns them.
     */
    void bind(IVoice* const* voices, std::size_t count) noexcept
    {
        boundCount_ = count < Capacity ? count : Capacity;
        for (std::size_t i = 0; i < Capacity; ++i)
            voices_[i] = (i < boundCount_ && voices != nullptr) ? voices[i] : nullptr;
        clearLifecycle();
    }

    /**
     * @brief Claim a free slot for a new note.
     * @param pipe     The pipe the voice will sound (kept for stealing heuristics
     *                 and pipe→chest routing).
     * @param framePos Absolute start frame, used by the @c Oldest steal policy.
     * @return A valid handle, or @ref VoiceHandle::invalid() if the pool is full
     *         (the caller should then @ref chooseVictim, @ref release it, and
     *         retry).
     */
    [[nodiscard]] VoiceHandle acquire(PipeId pipe, std::uint64_t framePos) noexcept
    {
        if (freeHead_ == VoiceHandle::kInvalidIndex)
            return VoiceHandle::invalid();

        const std::uint16_t index = freeHead_;
        freeHead_ = freeNext_[index];
        if (freeCount_ > 0)
            --freeCount_;

        generation_[index] = nextGeneration(generation_[index]);
        pipe_[index]       = pipe;
        startFrame_[index] = framePos;
        active_[index]     = true;
        sustained_[index]  = false;

        return VoiceHandle{index, generation_[index]};
    }

    /**
     * @brief Release a slot back to the free-list (idempotent for stale handles).
     * @param handle The handle returned by @ref acquire.
     *
     * The generation is bumped so any surviving copy of @p handle immediately
     * fails @ref resolve — the guarantee that a stolen voice cannot be addressed
     * by its former owner.
     */
    void release(VoiceHandle handle) noexcept
    {
        if (!ownsLive(handle))
            return;

        const std::uint16_t index = handle.index;
        active_[index]     = false;
        generation_[index] = nextGeneration(generation_[index]);
        freeNext_[index]   = freeHead_;
        freeHead_          = index;
        ++freeCount_;
    }

    /**
     * @brief Release every voice sounding note @p note on division @p division.
     * @return Number of voices released into their tail.
     *
     * One key is several PipeIds that differ only in rankId, so a note-off has to
     * match on the key rather than on the pipe. It also replaces what would
     * otherwise be one command per rank on the ring for every key release.
     * RT-safe.
     */
    std::size_t noteOffKey(MidiNote note, DivisionId division) noexcept
    {
        const auto div = static_cast<std::uint8_t>(division.value);
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
            if (active_[i] && voices_[i] != nullptr
                && pipe_[i].midiNote == note && pipe_[i].divisionId == div)
            {
                voices_[i]->noteOff();
                sustained_[i] = false;
                ++n;
            }
        return n;
    }

    /// @copydoc noteOffKey but marks the voices sustained instead of releasing.
    std::size_t holdKeyForSustain(MidiNote note, DivisionId division) noexcept
    {
        const auto div = static_cast<std::uint8_t>(division.value);
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
            if (active_[i] && voices_[i] != nullptr
                && pipe_[i].midiNote == note && pipe_[i].divisionId == div)
            {
                sustained_[i] = true;
                ++n;
            }
        return n;
    }

    /**
     * @brief Stop every voice of the least audible sounding key, synchronously.
     * @return Number of slots reclaimed, which is what the free list gained.
     *
     * A HARD reclaim, not a release: the slots have to be usable this instant,
     * because the caller is about to acquire them. A release-only version would
     * make the reservation count slots it has not actually freed, and the whole
     * admission guard would fail open — which is the bug it exists to prevent.
     *
     * Stealing a WHOLE key rather than a voice: taking one rank of a sounding
     * chord leaves the chord with a hole in its registration, which is far more
     * audible than one note ending early. RT-safe.
     */
    std::size_t stealQuietestKey(const StealPolicy& policy) noexcept
    {
        const VoiceHandle victim = chooseVictim(policy);
        if (!victim.isValid())
            return 0;

        const PipeId    p    = pipe_[victim.index];
        const MidiNote  note = p.midiNote;
        const auto      div  = p.divisionId;

        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (!active_[i] || pipe_[i].midiNote != note || pipe_[i].divisionId != div)
                continue;
            if (voices_[i] != nullptr)
                voices_[i]->silence();
            active_[i]     = false;
            sustained_[i]  = false;
            generation_[i] = nextGeneration(generation_[i]);
            freeNext_[i]   = freeHead_;
            freeHead_      = static_cast<std::uint16_t>(i);
            ++freeCount_;
            ++n;
        }
        return n;
    }

    /**
     * @brief Resolve a handle to its voice, rejecting stale references.
     * @return The bound voice, or nullptr if the handle is stale / inactive.
     */
    [[nodiscard]] IVoice* resolve(VoiceHandle handle) const noexcept
    {
        return ownsLive(handle) ? voices_[handle.index] : nullptr;
    }

    /// @return true if @p handle still refers to the live occupant of its slot.
    [[nodiscard]] bool isActive(VoiceHandle handle) const noexcept { return ownsLive(handle); }

    /**
     * @brief Send note-off to every active voice sounding @p pipe.
     * @return Number of voices released into their tail.
     *
     * The voices keep sounding through their release layer and are reclaimed by
     * the next @ref refresh(); this only starts the release. RT-safe.
     */
    std::size_t noteOff(PipeId pipe) noexcept
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (active_[i] && voices_[i] != nullptr && pipe_[i] == pipe)
            {
                voices_[i]->noteOff();
                sustained_[i] = false;
                ++n;
            }
        }
        return n;
    }

    /**
     * @brief Mark every voice sounding @p pipe as held by the sustain pedal.
     * @return Number of voices marked.
     *
     * The key is up but the pedal is down, so the voice keeps speaking until the
     * pedal lifts. RT-safe.
     */
    std::size_t holdForSustain(PipeId pipe) noexcept
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
            if (active_[i] && voices_[i] != nullptr && pipe_[i] == pipe)
            {
                sustained_[i] = true;
                ++n;
            }
        return n;
    }

    /**
     * @brief Release every pedal-held voice of one division. RT-safe.
     * @return Number of voices released.
     */
    std::size_t releaseSustained(DivisionId division) noexcept
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
            if (active_[i] && sustained_[i] && voices_[i] != nullptr
                && pipe_[i].divisionId == static_cast<std::uint8_t>(division.value))
            {
                voices_[i]->noteOff();
                sustained_[i] = false;
                ++n;
            }
        return n;
    }

    /**
     * @brief Send note-off to every active voice (all-notes-off / panic).
     * @return Number of voices released. RT-safe.
     */
    std::size_t allNotesOff() noexcept
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (active_[i] && voices_[i] != nullptr)
            {
                voices_[i]->noteOff();
                sustained_[i] = false;
                ++n;
            }
        }
        return n;
    }

    /**
     * @brief Stop every voice dead and reclaim its slot. RT-safe.
     * @return Number of voices that were sounding.
     *
     * Distinct from @ref allNotesOff, which is a musical all-notes-off and starts
     * a release. This is what a host reset means: nothing survives it, and the
     * next block is silent rather than a third of a second later.
     */
    std::size_t silenceAll() noexcept
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (voices_[i] != nullptr)
                voices_[i]->silence();
            if (active_[i])
                ++n;
            sustained_[i] = false;
        }
        // refresh() reclaims the slots, and every voice now reports inactive.
        (void) refresh();
        return n;
    }

    /**
     * @brief Cull finished voices and rebuild the active set, grouped by kind.
     * @return Number of still-active voices.
     *
     * Any active slot whose voice reports @c isActive()==false is reclaimed.
     * The survivors' indices are written into @ref activeIndices_ sorted by
     * @c EngineKind via a stable counting sort, so @ref batch() can hand out
     * contiguous per-kind runs. Call once at the top of each render.
     */
    std::size_t refresh() noexcept
    {
        // Pass 1: reclaim voices that have finished their release tail.
        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (active_[i] && (voices_[i] == nullptr || !voices_[i]->isActive()))
            {
                active_[i]     = false;
                generation_[i] = nextGeneration(generation_[i]);
                freeNext_[i]   = freeHead_;
                freeHead_      = static_cast<std::uint16_t>(i);
                ++freeCount_;
            }
        }

        // Pass 2: counting-sort the survivors into per-kind contiguous runs.
        for (auto& c : kindCount_) c = 0;
        for (std::size_t i = 0; i < boundCount_; ++i)
            if (active_[i])
                ++kindCount_[kindBucket(voices_[i])];

        std::size_t running = 0;
        for (std::size_t k = 0; k < kNumEngineKinds; ++k)
        {
            kindOffset_[k] = running;
            running += kindCount_[k];
        }
        kindOffset_[kNumEngineKinds] = running;

        std::array<std::size_t, kNumEngineKinds> cursor{}; // per-kind write cursors
        for (std::size_t k = 0; k < kNumEngineKinds; ++k)
            cursor[k] = kindOffset_[k];

        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (!active_[i])
                continue;
            const std::size_t k = kindBucket(voices_[i]);
            activeIndices_[cursor[k]++] = static_cast<std::uint16_t>(i);
        }

        activeCount_ = running;
        return activeCount_;
    }

    /// @return A flattened, non-owning view of the active set for the scheduler.
    [[nodiscard]] VoicePoolView view() const noexcept
    {
        VoicePoolView v;
        v.voices        = voices_.data();
        v.pipes         = pipe_.data();
        v.voiceCount    = boundCount_;
        v.activeIndices = activeIndices_.data();
        v.activeCount   = activeCount_;
        v.kindOffsets   = kindOffset_.data();
        v.numKinds      = kNumEngineKinds;
        return v;
    }

    /**
     * @brief The contiguous run of active voices of a single engine kind.
     * @param kind The engine kind to slice.
     * @return A batch view (possibly empty) valid until the next @ref refresh().
     */
    [[nodiscard]] VoiceBatchView batch(EngineKind kind) const noexcept
    {
        const std::size_t k = static_cast<std::size_t>(kind);
        VoiceBatchView b;
        b.kind    = kind;
        b.voices  = voices_.data();
        b.pipes   = pipe_.data();
        if (k < kNumEngineKinds)
        {
            b.indices = activeIndices_.data() + kindOffset_[k];
            b.count   = kindOffset_[k + 1] - kindOffset_[k];
        }
        return b;
    }

    /**
     * @brief Pick the best voice to sacrifice under a policy.
     * @param policy How to rank candidates (@c Quietest by default).
     * @return A handle to steal, or @ref VoiceHandle::invalid() if none exist.
     *
     * Level-aware policies rank candidates by @c IVoice::levelEstimate(), the
     * voice's own audibility estimate — not by @c cpuCostEstimate(), which is
     * identical across a composite registration and so never discriminated.
     * RT-safe.
     */
    [[nodiscard]] VoiceHandle chooseVictim(StealPolicy policy = StealPolicy::Quietest) const noexcept
    {
        VoiceHandle best     = VoiceHandle::invalid();
        float       bestCost = 0.0f;
        std::uint64_t bestStart = 0;
        bool        haveBest  = false;

        for (std::size_t i = 0; i < boundCount_; ++i)
        {
            if (!active_[i] || voices_[i] == nullptr)
                continue;

            const VoiceHandle h{static_cast<std::uint16_t>(i), generation_[i]};
            // Rank by AUDIBILITY, not by CPU cost. Every voice of a composite
            // registration reports the same cost, so a cost comparison never
            // fired and the victim was always the lowest slot index.
            const float cost   = voices_[i]->levelEstimate();
            const bool  better = pickBetter(policy, haveBest, cost, bestCost,
                                            startFrame_[i], bestStart);
            if (better)
            {
                best      = h;
                bestCost  = cost;
                bestStart = startFrame_[i];
                haveBest  = true;
            }
        }
        return best;
    }

private:
    // --- helpers ------------------------------------------------------------

    void clearLifecycle() noexcept
    {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            active_[i]     = false;
            sustained_[i]  = false;
            generation_[i] = 1; // first issued handle has generation 1 (0 is invalid)
            pipe_[i]       = PipeId{};
            startFrame_[i] = 0;
            freeNext_[i]   = (i + 1 < boundCount_) ? static_cast<std::uint16_t>(i + 1)
                                                   : VoiceHandle::kInvalidIndex;
        }
        freeHead_    = boundCount_ > 0 ? 0u : VoiceHandle::kInvalidIndex;
        freeCount_   = boundCount_;
        activeCount_ = 0;
        for (auto& c : kindCount_) c = 0;
        for (auto& o : kindOffset_) o = 0;
    }

    [[nodiscard]] bool ownsLive(VoiceHandle handle) const noexcept
    {
        return handle.isValid()
            && handle.index < boundCount_
            && active_[handle.index]
            && generation_[handle.index] == handle.generation;
    }

    /// Bump a generation, wrapping but never landing on the reserved 0.
    [[nodiscard]] static std::uint16_t nextGeneration(std::uint16_t g) noexcept
    {
        const std::uint16_t n = static_cast<std::uint16_t>(g + 1);
        return n == 0 ? 1 : n;
    }

    /// Map a voice to its SoA bucket by engine kind (nullptr → Additive).
    [[nodiscard]] static std::size_t kindBucket(const IVoice* v) noexcept
    {
        if (v == nullptr)
            return static_cast<std::size_t>(EngineKind::Additive);
        const std::size_t k = static_cast<std::size_t>(v->kind());
        return k < kNumEngineKinds ? k : static_cast<std::size_t>(EngineKind::Additive);
    }

    /// Decide whether candidate i beats the current best under @p policy.
    [[nodiscard]] static bool pickBetter(StealPolicy policy, bool haveBest,
                                         float cost, float bestCost,
                                         std::uint64_t start, std::uint64_t bestStart) noexcept
    {
        if (!haveBest)
            return true;
        switch (policy)
        {
            case StealPolicy::Quietest:   return cost < bestCost;   // least audible
            case StealPolicy::LowestTier: return cost < bestCost;   // cheapest tier
            case StealPolicy::Oldest:     return start < bestStart; // earliest start frame
            case StealPolicy::Newest:     return start > bestStart; // latest start frame
        }
        return false;
    }

    // --- storage (all fixed-capacity, no heap) ------------------------------

    std::array<IVoice*, Capacity>       voices_{};      ///< Bound, non-owning voice pointers.
    std::array<std::uint16_t, Capacity> generation_{};  ///< Per-slot occupancy generation.
    std::array<std::uint16_t, Capacity> freeNext_{};    ///< Intrusive free-list links.
    std::array<bool, Capacity>          active_{};      ///< Per-slot active flag.
    std::array<bool, Capacity>          sustained_{};   ///< Key up, but the pedal is down.
    std::array<PipeId, Capacity>        pipe_{};        ///< Pipe currently assigned to a slot.
    std::array<std::uint64_t, Capacity> startFrame_{};  ///< Slot's note-on frame (steal heuristic).

    std::array<std::uint16_t, Capacity> activeIndices_{}; ///< Kind-grouped active indices.
    std::array<std::size_t, kNumEngineKinds>     kindCount_{};  ///< Active count per kind.
    std::array<std::size_t, kNumEngineKinds + 1> kindOffset_{}; ///< Run offsets per kind (+ end).

    std::uint16_t freeHead_    = VoiceHandle::kInvalidIndex; ///< Head of the free-list.

    /// How many slots the free list holds, maintained alongside it.
    ///
    /// Walking the list to count would be O(n) on the audio thread, and the
    /// question is asked once per note-on: one key is several voices now, so the
    /// engine has to know whether a whole key will fit before it takes the first.
    std::size_t   freeCount_   = 0;
    std::size_t   boundCount_  = 0;                          ///< Number of bound slots.
    std::size_t   activeCount_ = 0;                          ///< Active voices after refresh().
};

} // namespace caecilia::core::engine
