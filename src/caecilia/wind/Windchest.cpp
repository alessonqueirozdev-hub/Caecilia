// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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

void Windchest::updateBlock(float bellowsSagStart, float bellowsSagEnd,
                            std::size_t numFrames) noexcept
{
    // Local trunk drop: the chest sits a little below what its regulator delivers,
    // by an amount proportional to the flow it is drawing.
    const float localDrop = config_.trunkConductance > 0.0f
                                ? demandAccum_ / config_.trunkConductance
                                : 0.0f;

    // The chest's OWN nominal, scaled by how far the shared reservoir has sagged.
    // Proportional and not absolute: a regulator holds this chest at its voiced
    // pressure and passes the trunk's movement through as a fraction, which is why
    // a heavy pedal chord makes the Récit flinch without dragging it down to the
    // Pédale's wind.
    const float nominal = config_.nominalPressurePa;
    startPa_ = nominal * bellowsSagStart - localDrop;
    endPa_   = nominal * bellowsSagEnd   - localDrop;
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
