/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/synthesis/ArEnvelope.h"
#include "caecilia/synthesis/IExcitation.h"
#include "caecilia/synthesis/IResonator.h"
#include "caecilia/synthesis/ReleaseSpec.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/synthesis/VoicingProfile.h"

#include <cstddef>
#include <memory>

namespace caecilia::synth
{

/**
 * @brief The expensive nonlinear physical voice tier: a wind-driven excitation
 *        feeding a tuned resonator, with emergent chiff/speech.
 *
 * PhysicalPipeVoice composes an @ref IExcitation (a @ref NonlinearJet for flues,
 * a @ref BeatingReed for reeds) with an @ref IResonator (a @ref Waveguide or a
 * @ref ModalResonator). The excitation is driven by the per-block wind snapshot,
 * so pitch/level/brightness and the onset transient all breathe with the wind —
 * the realism moat neither GrandOrgue nor Aeolus reaches. Under CPU pressure the
 * engine demotes the rank to an @ref AdditiveVoice via a phase-aligned crossfade.
 *
 * ## Real-time contract
 * - @ref prepare and @ref setComponents allocate / wire; not RT-safe.
 * - @ref noteOn, @ref noteOff, @ref renderAdd and the query methods are
 *   @c noexcept, allocation-free and lock-free.
 */
class PhysicalPipeVoice final : public core::IVoice
{
public:
    PhysicalPipeVoice() = default;

    // ---- core::IVoice -------------------------------------------------------

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void noteOn(core::PipeId pipe, core::Velocity velocity) noexcept override;
    void noteOff() noexcept override;
    void renderAdd(core::AudioBlock& block) noexcept override;
    [[nodiscard]] bool isActive() const noexcept override;
    [[nodiscard]] core::EngineKind kind() const noexcept override { return engineKind_; }
    [[nodiscard]] core::VoiceTier tier() const noexcept override { return tier_; }
    [[nodiscard]] float cpuCostEstimate() const noexcept override;

    // ---- Synthesis-side configuration (off the audio thread) ----------------

    /**
     * @brief Install the excitation + resonator pair and declare the reported
     *        engine kind / tier.
     *
     * Ownership transfers to the voice. Allocation-free at the call site beyond
     * the moves; the components themselves were allocated in their own prepare().
     */
    void setComponents(std::unique_ptr<IExcitation> excitation,
                       std::unique_ptr<IResonator> resonator,
                       core::EngineKind engineKind,
                       core::VoiceTier tier) noexcept;

    /// Inject the shared render environment.
    void setContext(const VoiceContext& context) noexcept { context_ = context; }

    /// Apply the resolved per-pipe voicing variance.
    void setVoicing(const VoicingProfile& voicing) noexcept { voicing_ = voicing; }

    /// Set the multi-stage release behaviour.
    void setReleaseSpec(const ReleaseSpec& spec) noexcept { release_ = spec; }

private:
    std::unique_ptr<IExcitation> excitation_;
    std::unique_ptr<IResonator>  resonator_;
    core::EngineKind             engineKind_ = core::EngineKind::Waveguide;
    core::VoiceTier              tier_       = core::VoiceTier::Waveguide;

    VoiceContext   context_{};
    VoicingProfile voicing_{};
    ReleaseSpec    release_{};
    ArEnvelope     envelope_{};

    core::SampleRate sampleRate_  = 0.0;
    core::WindchestId chest_{};
    double           heldSeconds_ = 0.0;
    float            masterGain_  = 0.5f;
};

} // namespace caecilia::synth
