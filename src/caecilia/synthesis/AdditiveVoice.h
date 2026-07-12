/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/synthesis/VoicingProfile.h"

#include <cstddef>

namespace caecilia::synth
{

/**
 * @brief The CPU-cheap, wind-modulated additive voice — the plenum-polyphony
 *        tier and the demotion target for the physical tier.
 *
 * AdditiveVoice is a thin composition over a @ref PartialBank (its modeled
 * sustain layer). Seeded from a @ref SpectralModel, every partial tracks the
 * wind supply, so even the cheap tier breathes and sags. A pure-additive rank is
 * a fully shippable degenerate configuration of the layered pipeline (no
 * recorded attack layer), which is exactly this class.
 *
 * ## Real-time contract
 * - @ref prepare is the only allocating method.
 * - @ref noteOn, @ref noteOff, @ref renderAdd and the query methods are
 *   @c noexcept, allocation-free and lock-free.
 */
class AdditiveVoice final : public core::IVoice
{
public:
    AdditiveVoice() = default;

    // ---- core::IVoice -------------------------------------------------------

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void noteOn(core::PipeId pipe, core::Velocity velocity) noexcept override;
    void noteOff() noexcept override;
    void renderAdd(core::AudioBlock& block) noexcept override;
    [[nodiscard]] bool isActive() const noexcept override;
    [[nodiscard]] core::EngineKind kind() const noexcept override { return core::EngineKind::Additive; }
    [[nodiscard]] core::VoiceTier tier() const noexcept override { return core::VoiceTier::Additive; }
    [[nodiscard]] float cpuCostEstimate() const noexcept override;

    // ---- Synthesis-side configuration (off the audio thread) ----------------

    /// Inject the shared render environment (wind/tuning/rank identity).
    void setContext(const VoiceContext& context) noexcept;

    /// Apply the resolved per-pipe voicing variance.
    void setVoicing(const VoicingProfile& voicing) noexcept { voicing_ = voicing; }

    /// Seed the timbre from an analysed spectral model. RT-safe (fills reserved storage).
    void seedFrom(const SpectralModel& model, float phaseAlignSeconds = 0.0f) noexcept;

    /// @return The underlying partial bank (for tests / inspection).
    [[nodiscard]] PartialBank& bank() noexcept { return bank_; }

private:
    PartialBank    bank_;
    VoiceContext   context_{};
    VoicingProfile voicing_{};
    core::PipeId   pipe_{};
};

} // namespace caecilia::synth
