/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/wind/Bellows.h"

#include <cmath>

namespace caecilia::wind
{

void Bellows::configure(const BellowsConfig& config) noexcept
{
    config_ = config;
    reset();
}

void Bellows::reset() noexcept
{
    pressurePa_  = config_.nominalPressurePa;
    startPa_     = config_.nominalPressurePa;
    endPa_       = config_.nominalPressurePa;
    demandAccum_ = 0.0f;
}

void Bellows::beginStep() noexcept
{
    demandAccum_ = 0.0f;
}

void Bellows::addDemand(float flow) noexcept
{
    // Accumulate-only; negative demand is clamped away so a stray value can
    // never inflate the reservoir above nominal.
    if (flow > 0.0f)
        demandAccum_ += flow;
}

void Bellows::integrate(double dtSeconds) noexcept
{
    startPa_ = pressurePa_;

    // Equilibrium sags below nominal in proportion to demand: a stiffer feed
    // (larger conductance) yields less sag for the same demand.
    const float sagPa = config_.feedConductance > 0.0f
                            ? demandAccum_ / config_.feedConductance
                            : 0.0f;
    const float equilibriumPa = config_.nominalPressurePa - sagPa;

    // First-order relaxation toward equilibrium. Exact exponential update keeps
    // the step unconditionally stable regardless of block size.
    const double rate = config_.compliance > 0.0f
                            ? static_cast<double>(config_.feedConductance) / config_.compliance
                            : 0.0;
    const float alpha = static_cast<float>(1.0 - std::exp(-rate * dtSeconds));

    pressurePa_ += (equilibriumPa - pressurePa_) * alpha;
    endPa_       = pressurePa_;

    // TODO(phase2): add plate inertance for a second-order "shake/bounce"
    // response and a nonlinear curtain-valve regulation curve. TODO(phase2):
    // fold in the passive leak term.
}

} // namespace caecilia::wind
