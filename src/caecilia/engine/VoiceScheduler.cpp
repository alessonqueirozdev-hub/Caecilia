// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/engine/VoiceScheduler.h"

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/core/IWindSupply.h"

#include <cmath>

namespace caecilia::core::engine
{

void VoiceScheduler::prepare(std::size_t maxVoices, std::size_t maxBlockFrames) noexcept
{
    maxVoices_ = maxVoices;
    maxFrames_ = maxBlockFrames;
    // TODO(phase0.6): allocate per-kind SIMD scratch buffers here (off-thread).
}

namespace
{
/// The bucket a level belongs to, quietest first.
///
/// @c frexp rather than a logarithm because this runs once per voice per block
/// and the answer only has to be right to a bucket: it splits a float into a
/// mantissa in [0.5, 1) and an exponent in one operation, which is the octave and
/// the position within it already separated. Zero and denormals never reach it --
/// they belong in bucket 0 anyway.
[[nodiscard]] std::size_t levelBucket(float level, int octaves, int subBuckets,
                                      int quietestExp) noexcept
{
    if (!(level > 0.0f))
        return 0;

    int         exponent = 0;
    const float mantissa = std::frexp(level, &exponent); // level == mantissa * 2^exponent

    const int octave = exponent - quietestExp;
    if (octave < 0)
        return 0;
    if (octave >= octaves)
        return static_cast<std::size_t>(octaves * subBuckets) - 1;

    // (mantissa - 0.5) * 2 is where in the octave it sits, in [0, 1).
    int sub = static_cast<int>((mantissa - 0.5f) * 2.0f * static_cast<float>(subBuckets));
    if (sub < 0)
        sub = 0;
    if (sub >= subBuckets)
        sub = subBuckets - 1;

    return static_cast<std::size_t>(octave * subBuckets + sub);
}

/// The quietest level that belongs in bucket @p b -- the inverse of the above.
[[nodiscard]] float bucketFloor(std::size_t b, int subBuckets, int quietestExp) noexcept
{
    const int octave = static_cast<int>(b) / subBuckets;
    const int sub    = static_cast<int>(b) % subBuckets;
    const float mantissa =
        0.5f + 0.5f * static_cast<float>(sub) / static_cast<float>(subBuckets);
    return std::ldexp(mantissa, octave + quietestExp);
}
} // namespace

VoiceScheduler::BlockPlan VoiceScheduler::planBlock(const VoicePoolView& pool,
                                                   float budgetUnits) noexcept
{
    BlockPlan plan;

    // Cost per level bucket, and how many voices are in each. Stack-resident and
    // tiny (48 floats + 48 counts): no allocation, and it stays in L1.
    float         costPerBucket[kLevelBuckets]{};
    std::uint32_t countPerBucket[kLevelBuckets]{};

    for (std::size_t i = 0; i < pool.activeCount; ++i)
    {
        IVoice* voice = pool.activeVoice(i);
        if (voice == nullptr || !voice->isActive())
            continue;

        const float cost = voice->cpuCostEstimate();
        const std::size_t b = levelBucket(voice->levelEstimate(), kOctaves,
                                          kSubBuckets, kQuietestExp);
        costPerBucket[b] += cost;
        ++countPerBucket[b];
        plan.demandUnits += cost;
    }

    if (budgetUnits <= 0.0f || plan.demandUnits <= budgetUnits)
        return plan; // the whole set fits; nothing is given up

    // Walk down from the loudest bucket, keeping while the budget lasts.
    float kept = 0.0f;
    for (std::size_t b = kLevelBuckets; b-- > 0;)
    {
        if (kept + costPerBucket[b] <= budgetUnits)
        {
            kept += costPerBucket[b];
            continue;
        }

        // Bucket b is where the budget runs out.
        //
        // If anything louder fit, drop this bucket with everything below it: the
        // voices in it are within 1.5 dB of each other, so which of them would
        // have fitted is not a musical question, and leaving them in would let
        // pool slot order decide -- which is the arbitrariness this function
        // exists to remove.
        //
        // If NOTHING louder fit, every sounding voice is in this one bucket. That
        // is a tutti, and dropping it silences the instrument rather than thinning
        // it. Keep it and let renderBatch's budget check trim inside, where the
        // order is arbitrary but so are the voices.
        const std::size_t firstShed = kept > 0.0f ? b + 1 : b;

        plan.shedBelowLevel = bucketFloor(firstShed, kSubBuckets, kQuietestExp);
        for (std::size_t q = 0; q < firstShed; ++q)
            plan.shedCount += countPerBucket[q];
        break;
    }

    return plan;
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

        // Deadline governor. Two questions, and the answer to either one is the
        // same: start this voice's release.
        //
        //   1. planBlock decided, for the whole block, that voices below a level
        //      cannot be afforded. That is the musical choice -- the quietest
        //      pipes go first.
        //   2. The budget ran out anyway. Only voices INSIDE the threshold bucket
        //      can reach this, because everything quieter was already caught by
        //      (1); it is what trims the last 6 dB, where the histogram cannot
        //      rank.
        //
        // The cost is scaled by the slice's share of the block: cpuCostEstimate()
        // describes rendering a whole block, and an event-sliced block asks for
        // this voice several times.
        // The order matters, and getting it wrong undoes the plan entirely: a
        // voice the plan already gave up must NOT be charged to the budget. It is
        // released either way, and charging it spends the allowance that was
        // computed on the assumption it was gone -- so the loud voices that were
        // meant to survive then fail the check and are released too, in whatever
        // interleaving the pool slots happen to have. Measured: an open division
        // and a boxed one, alternating slots, lost nine of twelve pipes instead
        // of the six the plan named.
        //
        // The budget is therefore a selector, not an accountant. What the block
        // actually COST is BlockPlan::demandUnits, which counts every voice
        // whatever becomes of it, and that is the figure the CpuGovernor reads.
        //
        // Shedding starts the RELEASE, and the voice still renders this block. It
        // used to noteOff() and `continue`, which the comment here claimed was
        // not skipping and was: the pipe went silent for exactly one block and
        // came back the next still at full envelope, so a shed note was bracketed
        // by two discontinuities instead of fading. Rendering the tail costs this
        // block the same as not shedding at all -- the saving is the following
        // blocks, once the tails end and free their slots. See the class note.
        if (ctx.shedBelowLevel > 0.0f && voice->levelEstimate() < ctx.shedBelowLevel)
            voice->noteOff();
        else if (ctx.budget != nullptr
                 && !ctx.budget->tryConsume(voice->cpuCostEstimate() * ctx.costScale))
            voice->noteOff();

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
