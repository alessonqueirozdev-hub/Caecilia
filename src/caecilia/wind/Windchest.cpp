/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/wind/Windchest.h"

namespace caecilia::wind
{

void Windchest::configure(const WindchestConfig& config) noexcept
{
    config_ = config;
    reset();
}

void Windchest::reset() noexcept
{
    startPa_     = config_.nominalPressurePa;
    endPa_       = config_.nominalPressurePa;
    frames_      = 1;
    demandAccum_ = 0.0f;
}

void Windchest::registerDemand(float flow) noexcept
{
    if (flow > 0.0f)
        demandAccum_ += flow;
}

void Windchest::updateBlock(float bellowsStartPa, float bellowsEndPa, std::size_t numFrames) noexcept
{
    // Local trunk drop: the chest sits at a slightly lower pressure than its
    // bellows, by an amount proportional to the flow it is drawing.
    const float localDrop = config_.trunkConductance > 0.0f
                                ? demandAccum_ / config_.trunkConductance
                                : 0.0f;

    startPa_ = bellowsStartPa - localDrop;
    endPa_   = bellowsEndPa - localDrop;
    frames_  = numFrames == 0 ? 1 : numFrames;

    // TODO(phase3): expose weak pipe-to-pipe frequency pulling on a shared chest
    // via a per-chest coupling term derived from neighbour demand.
}

float Windchest::sagPressureAt(std::size_t frameInBlock) const noexcept
{
    if (frames_ <= 1)
        return startPa_;

    float t = static_cast<float>(frameInBlock) / static_cast<float>(frames_ - 1);
    if (t > 1.0f)
        t = 1.0f;

    return startPa_ + (endPa_ - startPa_) * t;
}

} // namespace caecilia::wind
