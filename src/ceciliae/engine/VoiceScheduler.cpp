/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "ceciliae/engine/VoiceScheduler.h"

#include "ceciliae/core/AudioBlock.h"
#include "ceciliae/core/IVoice.h"
#include "ceciliae/core/IWindSupply.h"

namespace ceciliae::core::engine
{

void VoiceScheduler::prepare(std::size_t maxVoices, std::size_t maxBlockFrames) noexcept
{
    maxVoices_ = maxVoices;
    maxFrames_ = maxBlockFrames;
    // TODO(phase0.6): allocate per-kind SIMD scratch buffers here (off-thread).
}

std::size_t VoiceScheduler::renderActive(const VoicePoolView& pool, RenderContext& ctx) noexcept
{
    std::size_t rendered = 0;

    // Walk the active set one engine-kind batch at a time so that same-kind
    // unisons stay contiguous and can later be advanced by a vectorised kernel.
    const std::size_t kinds = pool.numKinds;
    for (std::size_t k = 0; k < kinds; ++k)
        rendered += renderBatch(pool.batch(k), ctx);

    return rendered;
}

std::size_t VoiceScheduler::renderBatch(const VoiceBatchView& batch, RenderContext& ctx) noexcept
{
    std::size_t rendered = 0;

    for (std::size_t i = 0; i < batch.count; ++i)
    {
        IVoice* voice = batch.voice(i);
        if (voice == nullptr || !voice->isActive())
            continue;

        // Deadline governor: if this voice will not fit, shed it this block.
        // Cheapest voices are shed last because the active set is walked in
        // ascending index within a kind; a later phase reorders by audibility.
        if (ctx.budget != nullptr && !ctx.budget->tryConsume(voice->cpuCostEstimate()))
        {
            // TODO(phase0.6): demote to a cheaper VoiceTier and retry instead of
            // dropping; steal the quietest voice when even Modal will not fit.
            continue;
        }

        // Route the voice to its windchest accumulation bus. Wind supply owns the
        // pipe→chest mapping; fall back to bus 0 when no wind snapshot is bound.
        AudioBlock* bus = nullptr;
        if (ctx.wind != nullptr)
            bus = ctx.busFor(ctx.wind->chestForPipe(batch.pipe(i)));
        if (bus == nullptr)
            bus = ctx.numChestBuses > 0 ? &ctx.chestBuses[0] : nullptr;
        if (bus == nullptr)
            continue;

        // Voices accumulate (+=) into the shared bus; never overwrite.
        voice->renderAdd(*bus);
        ++rendered;
    }

    return rendered;
}

} // namespace ceciliae::core::engine
