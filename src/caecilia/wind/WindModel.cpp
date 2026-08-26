// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/wind/WindModel.h"

namespace caecilia::wind
{

void WindModel::prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames)
{
    sampleRate_     = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockFrames_ = maxBlockFrames;
    reset();
}

void WindModel::configure(const WindModelConfig& config)
{
    // All allocation happens here, off the audio thread.
    bellows_.clear();
    bellows_.resize(config.bellows.size());
    for (std::size_t i = 0; i < config.bellows.size(); ++i)
        bellows_[i].configure(config.bellows[i]);

    tremulants_.clear();
    tremulants_.resize(config.tremulants.size());

    chests_.clear();
    chests_.resize(config.chests.size());
    for (std::size_t i = 0; i < config.chests.size(); ++i)
        chests_[i].configure(config.chests[i]);

    // Configure each tremulant with the nominal pressure of the FIRST chest it
    // serves, so its depth fraction maps to an absolute swing.
    for (std::size_t t = 0; t < config.tremulants.size(); ++t)
    {
        float nominalPa = kDefaultNominalPressurePa;
        for (const auto& chest : config.chests)
        {
            if (chest.tremulantIndex == static_cast<std::int32_t>(t))
            {
                nominalPa = chest.nominalPressurePa;
                break;
            }
        }
        tremulants_[t].configure(config.tremulants[t], nominalPa);
    }

    pipeBindings_ = config.pipeBindings;
}

void WindModel::reset() noexcept
{
    for (auto& b : bellows_)
        b.reset();
    for (auto& c : chests_)
        c.reset();
    for (auto& t : tremulants_)
        t.reset();
}

void WindModel::step(std::size_t numFrames) noexcept
{
    const double dtSeconds =
        sampleRate_ > 0.0 ? static_cast<double>(numFrames) / sampleRate_ : 0.0;

    // 1. Sum each chest's demand into its feeding bellows.
    for (auto& b : bellows_)
        b.beginStep();

    for (const auto& c : chests_)
    {
        const std::uint16_t bi = c.bellowsIndex();
        if (bi < bellows_.size())
            bellows_[bi].addDemand(c.pendingDemand());
    }

    // 2. Integrate every reservoir once for the block.
    for (auto& b : bellows_)
        b.integrate(dtSeconds);

    // 3. Advance every tremulant's phase across the block.
    for (auto& t : tremulants_)
        t.advanceBlock(numFrames, sampleRate_);

    // 4. Latch each chest's per-frame sag trajectory, then clear its demand.
    for (auto& c : chests_)
    {
        const std::uint16_t bi = c.bellowsIndex();
        const float startPa = bi < bellows_.size() ? bellows_[bi].pressureStartPa()
                                                    : c.nominalPressurePa();
        const float endPa   = bi < bellows_.size() ? bellows_[bi].pressureEndPa()
                                                    : c.nominalPressurePa();
        c.updateBlock(startPa, endPa, numFrames);
        c.resetDemand();
    }
}

float WindModel::nominalPressurePa(core::WindchestId chest) const noexcept
{
    const std::ptrdiff_t idx = indexForChest(chest);
    return idx >= 0 ? chests_[static_cast<std::size_t>(idx)].nominalPressurePa()
                    : kDefaultNominalPressurePa;
}

float WindModel::pressureAt(core::WindchestId chest, std::size_t frameInBlock) const noexcept
{
    const std::ptrdiff_t idx = indexForChest(chest);
    if (idx < 0)
        return kDefaultNominalPressurePa;

    const Windchest& c = chests_[static_cast<std::size_t>(idx)];

    float pressure = c.sagPressureAt(frameInBlock);

    const std::int32_t ti = c.tremulantIndex();
    if (ti >= 0 && static_cast<std::size_t>(ti) < tremulants_.size())
        pressure += tremulants_[static_cast<std::size_t>(ti)].pressureDeltaPa(frameInBlock);

    // Pressure can never physically go negative.
    return pressure > 0.0f ? pressure : 0.0f;
}

float WindModel::pressureDeviation(core::WindchestId chest, std::size_t frameInBlock) const noexcept
{
    const float nominal = nominalPressurePa(chest);
    if (nominal <= 0.0f)
        return 0.0f;
    return (pressureAt(chest, frameInBlock) - nominal) / nominal;
}

core::WindchestId WindModel::chestForPipe(core::PipeId pipe) const noexcept
{
    for (const auto& binding : pipeBindings_)
    {
        if (binding.rank.value == pipe.rankId)
            return binding.chest;
    }
    // Unbound ranks default to the first chest (chest 0), if any.
    return chests_.empty() ? core::WindchestId{} : chests_.front().id();
}

void WindModel::registerDemand(core::WindchestId chest, float flowDemand) noexcept
{
    const std::ptrdiff_t idx = indexForChest(chest);
    if (idx >= 0)
        chests_[static_cast<std::size_t>(idx)].registerDemand(flowDemand);
}

void WindModel::setTremulantEnabled(std::size_t tremulantIndex, bool shouldBeEnabled) noexcept
{
    if (tremulantIndex < tremulants_.size())
        tremulants_[tremulantIndex].setEnabled(shouldBeEnabled);
}

void WindModel::setChestTremulantShape(core::WindchestId chest, float rateHz,
                                       float depthFraction) noexcept
{
    const std::ptrdiff_t index = indexForChest(chest);
    if (index < 0)
        return;

    const Windchest&   c         = chests_[static_cast<std::size_t>(index)];
    const std::int32_t tremulant = c.tremulantIndex();
    if (tremulant >= 0 && static_cast<std::size_t>(tremulant) < tremulants_.size())
        tremulants_[static_cast<std::size_t>(tremulant)]
            .setShape(rateHz, depthFraction, c.nominalPressurePa());
}

void WindModel::setChestTremulantEnabled(core::WindchestId chest,
                                         bool              shouldBeEnabled) noexcept
{
    const std::ptrdiff_t index = indexForChest(chest);
    if (index < 0)
        return;

    // A chest without a tremulant carries -1, and asking it to shake is a no-op
    // rather than an error: an organist reaching for a tremulant the division does
    // not have gets the same nothing a real console gives them.
    const std::int32_t tremulant = chests_[static_cast<std::size_t>(index)].tremulantIndex();
    if (tremulant >= 0)
        setTremulantEnabled(static_cast<std::size_t>(tremulant), shouldBeEnabled);
}

std::ptrdiff_t WindModel::indexForChest(core::WindchestId chest) const noexcept
{
    // Fast path: chests are usually stored at index == id value.
    if (chest.value < chests_.size() && chests_[chest.value].id() == chest)
        return static_cast<std::ptrdiff_t>(chest.value);

    // Fallback: linear search for a non-contiguous id layout.
    for (std::size_t i = 0; i < chests_.size(); ++i)
    {
        if (chests_[i].id() == chest)
            return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

} // namespace caecilia::wind
