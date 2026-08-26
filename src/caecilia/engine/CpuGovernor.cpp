// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/engine/CpuGovernor.h"

namespace caecilia::core::engine
{
namespace
{
/// Clamp helper kept local so this translation unit pulls in no headers at all.
[[nodiscard]] constexpr float clampf(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/// A load this large is not a busy machine, it is a clock that jumped: a
/// suspended process, a re-based counter, a debugger breakpoint. Acting on one
/// would shed the entire organ on resume.
constexpr float kImplausibleLoad = 64.0f;
} // namespace

void CpuGovernor::configure(const Config& config) noexcept
{
    config_ = config;

    // Nonsense in the configuration must not become nonsense in the loop.
    if (!(config_.ceilingUnits > 0.0f))
        config_.ceilingUnits = 1024.0f;
    config_.floorUnits  = clampf(config_.floorUnits, 0.0f, config_.ceilingUnits);
    config_.targetLoad  = clampf(config_.targetLoad, 0.05f, 1.0f);
    config_.attack      = clampf(config_.attack,  0.001f, 0.9f);
    config_.release     = clampf(config_.release, 0.0f,   0.9f);
    config_.deadBand    = clampf(config_.deadBand, 0.1f,  1.0f);

    budget_ = clampf(budget_, config_.floorUnits, config_.ceilingUnits);
}

void CpuGovernor::reset() noexcept
{
    budget_         = config_.ceilingUnits;
    loadSmoothed_   = 0.0f;
    peakLoad_       = 0.0f;
    warmupLeft_     = config_.warmupBlocks;
    pressureBlocks_ = 0;
}

float CpuGovernor::observe(double elapsedSeconds, double deadlineSeconds,
                           float unitsSpent) noexcept
{
    if (deadlineSeconds <= 0.0)
        return budget_;

    const float load = static_cast<float>(elapsedSeconds / deadlineSeconds);

    // The `!(load >= 0)` form also rejects NaN, which `load < 0` would not.
    if (!(load >= 0.0f) || load > kImplausibleLoad)
        return budget_;

    // Metering happens whether or not the loop is allowed to act, because "how
    // hard is this machine working" is worth showing during an offline bounce too.
    loadSmoothed_ += kLoadSmoothing * (load - loadSmoothed_);
    peakLoad_ = load > peakLoad_ ? load : peakLoad_ * kPeakDecay;

    if (load > config_.targetLoad)
        ++pressureBlocks_;

    if (!enabled_)
        return budget_;

    if (warmupLeft_ > 0)
    {
        --warmupLeft_;
        budget_ = config_.ceilingUnits;
        return budget_;
    }

    if (load > config_.targetLoad)
    {
        float next = budget_ * (1.0f - config_.attack);

        // Already past the deadline: walking down over five blocks is five xruns.
        // Jump to what this very block says is affordable, and keep whichever of
        // the two is lower.
        if (load >= 1.0f && unitsSpent > 0.0f)
        {
            const float affordable = unitsSpent * (config_.targetLoad / load);
            if (affordable < next)
                next = affordable;
        }

        budget_ = next < config_.floorUnits ? config_.floorUnits : next;
    }
    else if (load < config_.targetLoad * config_.deadBand)
    {
        // The additive term is what lets the budget leave a floor of zero, and it
        // makes the recovery from a deep cut linear at first and geometric after
        // -- which is the right shape: the first pipes back are the cheapest to
        // afford and the least likely to overload again.
        const float next = budget_ * (1.0f + config_.release) + config_.release;
        budget_ = next > config_.ceilingUnits ? config_.ceilingUnits : next;
    }

    return budget_;
}

} // namespace caecilia::core::engine
