/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/wind/WindTypes.h"

#include <cstddef>

namespace caecilia::wind
{

/**
 * @brief A tremulant modelled as periodic modulation of the wind PRESSURE.
 *
 * Crucially the tremulant modulates the driver, not an output gain: it adds a
 * signed pressure delta (in pascals) to the chest pressure, so downstream each
 * voice's WindResponseCurve turns it into coupled amplitude, pitch and timbral
 * movement — the physically correct behaviour a gain LFO cannot reproduce.
 *
 * The phase advances once per block; per-frame pressure deltas are then
 * evaluated analytically at the exact sub-block phase, giving sample-accurate
 * modulation with no per-sample state and no allocation.
 *
 * ## Real-time contract
 * advanceBlock(), pressureDeltaPa(), setEnabled() and the accessors are all
 * @c noexcept, allocation-free and lock-free. configure() is off-thread setup.
 */
class Tremulant
{
public:
    Tremulant() noexcept = default;

    /**
     * @brief Configure from a spec plus the nominal pressure of the chest it
     *        modulates (used to turn @c depthFraction into an absolute swing).
     */
    void configure(const TremulantConfig& config, float chestNominalPa) noexcept;

    /// Reset phase to zero and restore the configured enable state.
    void reset() noexcept;

    /// Engage / disengage the tremulant (click-free ramping is applied per block).
    void setEnabled(bool shouldBeEnabled) noexcept;

    /// @return true while the tremulant is drawn (target state).
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /**
     * @brief Advance the modulation phase across one block.
     * @param numFrames  Frames in the block.
     * @param sampleRate Host sample rate in Hz.
     *
     * Captures the block-start phase and per-frame increment so that
     * pressureDeltaPa() can be queried at any offset within the block. RT-safe.
     */
    void advanceBlock(std::size_t numFrames, core::SampleRate sampleRate) noexcept;

    /**
     * @brief Signed pressure modulation (Pa) at a sample offset in the block.
     * @param frameInBlock Offset in [0, numFrames) from the last advanceBlock().
     * @return Pressure delta to ADD to the chest's sagged pressure. RT-safe.
     */
    [[nodiscard]] float pressureDeltaPa(std::size_t frameInBlock) const noexcept;

    /// @return Configured modulation rate in Hz.
    [[nodiscard]] float rateHz() const noexcept { return config_.rateHz; }

private:
    /// Evaluate the configured waveform at normalised phase in [0,1) → [-1, 1].
    [[nodiscard]] float evaluateWaveform(double phase01) const noexcept;

    TremulantConfig config_{};
    float  depthPa_    = 0.0f; ///< Absolute peak swing = depthFraction * chestNominal.
    double phase_      = 0.0;  ///< Running phase in [0,1).
    double phaseStart_ = 0.0;  ///< Phase captured at the start of the current block.
    double phaseInc_   = 0.0;  ///< Per-frame phase increment.
    bool   enabled_    = false;///< Target engage state.
    float  gain_       = 0.0f; ///< Smoothed 0→1 depth gain for click-free on/off.
};

} // namespace caecilia::wind
