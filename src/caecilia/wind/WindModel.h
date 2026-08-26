// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/wind/Bellows.h"
#include "caecilia/wind/Tremulant.h"
#include "caecilia/wind/WindTap.h"
#include "caecilia/wind/WindTypes.h"
#include "caecilia/wind/Windchest.h"

#include <cstddef>
#include <vector>

namespace caecilia::wind
{

/**
 * @brief The shared wind supply for a loaded organ, implementing core::IWindSupply.
 *
 * WindModel owns the reservoirs (Bellows), the distribution nodes (Windchest),
 * the tremulants, and the rank→chest routing. Its owner is expected to call
 * step() once per audio block, which:
 *   1. sums each chest's registered demand into its feeding bellows,
 *   2. integrates every bellows' regulated pressure ODE (producing sag),
 *   3. advances each tremulant's phase for the block,
 *   4. latches each chest's per-frame sag trajectory.
 * The result is an immutable per-block snapshot that voices READ through the
 * IWindSupply accessors — computed once, then read-only, hence race-free.
 *
 * Wind is an audio-rate control signal, not an LFO: pressureAt() is
 * sample-accurate and already includes both polyphony sag and tremulant
 * modulation, so the same signal drives excitation, pitch, level and timbre
 * downstream via each voice's WindResponseCurve.
 *
 * ## Real-time contract
 * - prepare() and configure() are the ONLY methods that allocate; they run off
 *   the audio thread before rendering starts.
 * - step(), reset(), setTremulantEnabled() and every IWindSupply accessor are
 *   @c noexcept, allocation-free and lock-free.
 * - registerDemand() only accumulates into a writer-owned tally that feeds the
 *   NEXT block's integration, so it never disturbs the current snapshot.
 */
class WindModel final : public core::IWindSupply
{
public:
    WindModel() noexcept = default;
    ~WindModel() override = default;

    WindModel(const WindModel&)            = delete;
    WindModel& operator=(const WindModel&) = delete;

    /**
     * @brief Cache the audio format. Allocation/precompute happens here and in
     *        configure(). Not RT-safe.
     * @param sampleRate     Host sample rate in Hz (> 0).
     * @param maxBlockFrames Largest block step() will be asked to integrate.
     */
    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames);

    /**
     * @brief Build the runtime reservoir/chest/tremulant graph from a config.
     *        Allocates the fixed-capacity state arrays. Not RT-safe.
     */
    void configure(const WindModelConfig& config);

    /// Snap every reservoir and tremulant back to its rest state. RT-safe.
    void reset() noexcept;

    /**
     * @brief Integrate the wind ODE for one block and publish the snapshot.
     * @param numFrames Frames in the block (<= maxBlockFrames from prepare()).
     *
     * RT-safe: no allocation, no locks, no exceptions.
     */
    void step(std::size_t numFrames) noexcept override;

    // --- core::IWindSupply ------------------------------------------------
    [[nodiscard]] float nominalPressurePa(core::WindchestId chest) const noexcept override;
    [[nodiscard]] float pressureAt(core::WindchestId chest,
                                   std::size_t frameInBlock) const noexcept override;
    [[nodiscard]] float pressureDeviation(core::WindchestId chest,
                                          std::size_t frameInBlock) const noexcept override;
    [[nodiscard]] core::WindchestId chestForPipe(core::PipeId pipe) const noexcept override;
    void registerDemand(core::WindchestId chest, float flowDemand) noexcept override;

    // --- extras -----------------------------------------------------------
    /// @return Number of configured windchests.
    [[nodiscard]] std::size_t numChests() const noexcept { return chests_.size(); }

    /// @return Number of configured tremulants.
    [[nodiscard]] std::size_t numTremulants() const noexcept { return tremulants_.size(); }

    /// @return A WindTap bound to @p chest for cheap per-voice sampling.
    [[nodiscard]] WindTap tapFor(core::WindchestId chest) const noexcept
    {
        return WindTap(*this, chest);
    }

    /// Engage / disengage a tremulant by index. Ignored if out of range. RT-safe.
    void setTremulantEnabled(std::size_t tremulantIndex, bool shouldBeEnabled) noexcept;

    /// Engage / disengage the tremulant belonging to @p chest. A chest without one
    /// is ignored. RT-safe.
    void setChestTremulantEnabled(core::WindchestId chest,
                                  bool              shouldBeEnabled) noexcept override;

    /// Set a chest tremulant's rate and depth while it runs. RT-safe.
    void setChestTremulantShape(core::WindchestId chest, float rateHz,
                                float depthFraction) noexcept override;

private:
    /// Resolve a WindchestId to a chest index, or -1 if unknown. RT-safe.
    [[nodiscard]] std::ptrdiff_t indexForChest(core::WindchestId chest) const noexcept;

    core::SampleRate sampleRate_    = 48000.0;
    std::size_t      maxBlockFrames_ = 0;

    std::vector<Bellows>          bellows_;
    std::vector<Windchest>        chests_;
    std::vector<Tremulant>        tremulants_;
    std::vector<PipeChestBinding> pipeBindings_;
};

} // namespace caecilia::wind
