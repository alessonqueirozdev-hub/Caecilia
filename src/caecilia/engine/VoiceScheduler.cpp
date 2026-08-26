// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/engine/VoiceScheduler.h"

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"

namespace caecilia::core::engine
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
        //
        // Shedding means starting its RELEASE, not skipping it. Skipping made the
        // voice silent for exactly one block and then brought it back the next --
        // a hole punched into a sustained note, and, if the budget stayed tight,
        // an audible amplitude modulation at block rate. That is the opposite of
        // the promise that a worst-case tutti thins subtly rather than xruns: a
        // released voice fades over its own release time and then frees its slot.
        //
        // TODO(phase0.6): demote to a cheaper VoiceTier first, and only release
        // when even the cheapest tier will not fit.
        // The cost is scaled by the slice's share of the block: cpuCostEstimate()
        // describes rendering a whole block, and an event-sliced block asks for
        // this voice several times.
        if (ctx.budget != nullptr
            && !ctx.budget->tryConsume(voice->cpuCostEstimate() * ctx.costScale))
        {
            voice->noteOff();
            continue;
        }

        // The swell shoe's FLAT gain, per voice rather than per bus because the
        // buses are per WINDCHEST and enclosure is a property of the division. The
        // spectral half is deliberately not here: it lives on the bus, in
        // AudioEngine::applyShutters, where it costs one pole per chest instead of
        // a filter per note.
        const auto ramp = ctx.expressionFor(core::DivisionId{ batch.pipe(i).divisionId });
        voice->setExpression(ramp.start, ramp.inc);

        // Route the voice to its windchest accumulation bus, by INDEX. The engine
        // recorded the chest when the voice started; asking IWindSupply::chestForPipe
        // instead was a linear scan over the organ's rank bindings, per voice, per
        // block, to answer a question that had already been settled -- and it was
        // asked with the stop id in a field matched against rank ids.
        AudioBlock* bus = ctx.busFor(ctx.chestOfSlot(batch.slot(i)));
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

} // namespace caecilia::core::engine
