/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

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
 * engine kind so unisons render as contiguous SIMD batches.
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

        generation_[index] = nextGeneration(generation_[index]);
        pipe_[index]       = pipe;
        startFrame_[index] = framePos;
        active_[index]     = true;

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
                ++n;
            }
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
                ++n;
            }
        }
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
     * Level-aware policies use @c cpuCostEstimate() as a coarse proxy for
     * audibility in this scaffold; a later phase reads a real per-voice level
     * meter. RT-safe.
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
            const float cost   = voices_[i]->cpuCostEstimate();
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
            generation_[i] = 1; // first issued handle has generation 1 (0 is invalid)
            pipe_[i]       = PipeId{};
            startFrame_[i] = 0;
            freeNext_[i]   = (i + 1 < boundCount_) ? static_cast<std::uint16_t>(i + 1)
                                                   : VoiceHandle::kInvalidIndex;
        }
        freeHead_    = boundCount_ > 0 ? 0u : VoiceHandle::kInvalidIndex;
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
            case StealPolicy::Quietest:   return cost < bestCost;   // cheapest ~= least audible (proxy)
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
    std::array<PipeId, Capacity>        pipe_{};        ///< Pipe currently assigned to a slot.
    std::array<std::uint64_t, Capacity> startFrame_{};  ///< Slot's note-on frame (steal heuristic).

    std::array<std::uint16_t, Capacity> activeIndices_{}; ///< Kind-grouped active indices.
    std::array<std::size_t, kNumEngineKinds>     kindCount_{};  ///< Active count per kind.
    std::array<std::size_t, kNumEngineKinds + 1> kindOffset_{}; ///< Run offsets per kind (+ end).

    std::uint16_t freeHead_    = VoiceHandle::kInvalidIndex; ///< Head of the free-list.
    std::size_t   boundCount_  = 0;                          ///< Number of bound slots.
    std::size_t   activeCount_ = 0;                          ///< Active voices after refresh().
};

} // namespace caecilia::core::engine
