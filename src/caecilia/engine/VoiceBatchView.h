// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"

#include <cstddef>
#include <cstdint>

namespace caecilia::core::engine
{

/**
 * @brief A contiguous slice of the active set holding voices of ONE
 *        @c EngineKind, ready for a SIMD batch kernel.
 *
 * Because the pool sorts active indices by kind each block, every voice in a
 * batch shares the same synthesis engine and can be advanced together (e.g. a
 * rank of Additive unisons summed with one vectorised partial-accumulate). The
 * view stores no audio state — it just names a run of the pool's index array.
 */
struct VoiceBatchView
{
    EngineKind           kind    = EngineKind::Additive; ///< Engine kind shared by the batch.
    IVoice* const*       voices  = nullptr;              ///< Pool voice-pointer array.
    const PipeId*        pipes   = nullptr;              ///< Pool pipe-per-slot array.
    const std::uint16_t* indices = nullptr;              ///< Start of this kind's index run.
    std::size_t          count   = 0;                    ///< Number of voices in the run.

    /// @return The i-th voice in the batch (nullptr if out of range).
    [[nodiscard]] IVoice* voice(std::size_t i) const noexcept
    {
        return i < count ? voices[indices[i]] : nullptr;
    }

    /// @return The pipe backing the i-th voice.
    [[nodiscard]] PipeId pipe(std::size_t i) const noexcept
    {
        return (i < count && pipes != nullptr) ? pipes[indices[i]] : PipeId{};
    }

    /// @return The POOL SLOT of the i-th voice, which is how per-slot engine state
    ///         -- its windchest, its wind draw -- is indexed.
    [[nodiscard]] std::uint16_t slot(std::size_t i) const noexcept
    {
        return i < count ? indices[i] : 0;
    }

    /// @return true if the batch has no voices.
    [[nodiscard]] bool isEmpty() const noexcept { return count == 0; }
};

/**
 * @brief A non-owning, flattened window over the pool's currently-active voices.
 *
 * Produced by @c VoicePool::view(), this is the only shape the hot scheduler
 * code sees, which is why the pool can stay a template on capacity while
 * @c VoiceScheduler stays a plain, separately-compiled translation unit. All
 * arrays are pool-owned and outlive the view for the duration of the block.
 *
 * The @ref activeIndices array indexes into @ref voices / @ref pipes and is
 * grouped by @c EngineKind, delimited by @ref kindOffsets, so same-kind unisons
 * form contiguous runs (@ref batch) that vectorise cleanly.
 */
struct VoicePoolView
{
    IVoice* const*      voices       = nullptr; ///< [voiceCount] bound voice pointers.
    const PipeId*       pipes        = nullptr; ///< [voiceCount] pipe assigned per slot.
    std::size_t         voiceCount   = 0;       ///< Number of bound slots.

    const std::uint16_t* activeIndices = nullptr; ///< [activeCount] indices, grouped by kind.
    std::size_t          activeCount   = 0;       ///< Number of active voices this block.

    const std::size_t*  kindOffsets   = nullptr; ///< [numKinds + 1] run offsets into activeIndices.
    std::size_t         numKinds      = 0;        ///< Number of engine-kind buckets.

    /// @return The i-th active voice pointer (nullptr if out of range).
    [[nodiscard]] IVoice* activeVoice(std::size_t i) const noexcept
    {
        return i < activeCount ? voices[activeIndices[i]] : nullptr;
    }

    /// @return The pipe of the i-th active voice.
    [[nodiscard]] PipeId activePipe(std::size_t i) const noexcept
    {
        return i < activeCount ? pipes[activeIndices[i]] : PipeId{};
    }

    /// @return The pipe backing slot @p index (for pipe→chest routing during render).
    [[nodiscard]] PipeId pipeForSlot(std::uint16_t index) const noexcept
    {
        return index < voiceCount ? pipes[index] : PipeId{};
    }

    /**
     * @brief The contiguous batch of active voices in kind bucket @p k.
     * @param k Bucket index in [0, numKinds).
     * @return A batch view over that kind's run (empty if @p k is out of range).
     */
    [[nodiscard]] VoiceBatchView batch(std::size_t k) const noexcept
    {
        VoiceBatchView b;
        b.voices = voices;
        b.pipes  = pipes;
        if (kindOffsets != nullptr && k + 1 <= numKinds)
        {
            b.kind    = static_cast<EngineKind>(k);
            b.indices = activeIndices + kindOffsets[k];
            b.count   = kindOffsets[k + 1] - kindOffsets[k];
        }
        return b;
    }
};

} // namespace caecilia::core::engine
