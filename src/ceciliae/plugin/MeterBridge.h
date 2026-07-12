/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/engine/AudioEngine.h"
#include "ceciliae/engine/MeterSnapshot.h"

namespace ceciliae::plugin
{

/**
 * @brief Read-only view that hands the engine's lock-free metering snapshot to
 *        the editor and UI.
 *
 * The audio thread already publishes a double-buffered @c MeterSnapshot inside
 * @c AudioEngine; this bridge is the thin, JUCE-adjacent accessor the UI polls at
 * frame rate — the audio thread never touches a @c juce::Component and the UI
 * never blocks the audio thread. It adds nothing but binding + a couple of
 * convenience conversions, so there is exactly one source of metering truth.
 */
class MeterBridge
{
public:
    MeterBridge() noexcept = default;

    /**
     * @brief Bind the engine whose snapshots this bridge exposes.
     * @param engine The processor's engine. Must outlive this bridge.
     *
     * Off-thread wiring; safe to call from prepareToPlay.
     */
    void connect(const core::engine::AudioEngine& engine) noexcept { engine_ = &engine; }

    /**
     * @brief The latest consistent metering frame.
     * @return A non-torn snapshot; an empty default if not yet connected. Wait-free.
     */
    [[nodiscard]] core::engine::MeterSnapshot snapshot() const noexcept
    {
        return engine_ != nullptr ? engine_->latestMeters() : core::engine::MeterSnapshot{};
    }

    /// @return Post-master-chain peak level in dBFS (-inf clamped to @p floorDb).
    [[nodiscard]] float masterPeakDb(float floorDb = -100.0f) const noexcept
    {
        return linearToDb(snapshot().master.peak, floorDb);
    }

    /// @return Voices sounding after the most recent block.
    [[nodiscard]] unsigned activeVoices() const noexcept { return snapshot().activeVoices; }

    /// Convert a linear magnitude to dBFS, clamping silence to @p floorDb.
    [[nodiscard]] static float linearToDb(float linear, float floorDb) noexcept;

private:
    const core::engine::AudioEngine* engine_ = nullptr;
};

} // namespace ceciliae::plugin
