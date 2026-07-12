/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/AudioBlock.h"
#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/IVoice.h"
#include "ceciliae/synthesis/ArEnvelope.h"
#include "ceciliae/synthesis/IVoiceLayer.h"
#include "ceciliae/synthesis/ReleaseSpec.h"
#include "ceciliae/synthesis/VoiceContext.h"
#include "ceciliae/synthesis/VoicingProfile.h"

#include <cstddef>

namespace ceciliae::synth
{

/**
 * @brief Multi-sample playback voice — the safe, fully-shippable fallback tier.
 *
 * SampleVoice streams a recorded pipe from an @ref ISampleSource and pitch-shifts
 * it to the played note by resampling, looping the sustain region (or running as
 * a one-shot when the source declares no loop, which is Ceciliae's preferred
 * loop-free path handing off to a modeled sustain). It is a degenerate
 * configuration of the layered pipeline where the attack AND sustain come from
 * the recording.
 *
 * The scaffold resamples with linear interpolation; the production path delegates
 * to the @c dsp module's 16-point Kaiser-windowed-sinc interpolator once that
 * module lands (see the TODO in @c renderAdd).
 *
 * ## Real-time contract
 * - @ref prepare precomputes; @ref setSampleSource wires a (pre-loaded) source.
 * - @ref noteOn, @ref noteOff, @ref renderAdd and the query methods are
 *   @c noexcept, allocation-free and lock-free.
 */
class SampleVoice final : public core::IVoice
{
public:
    SampleVoice() = default;

    // ---- core::IVoice -------------------------------------------------------

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void noteOn(core::PipeId pipe, core::Velocity velocity) noexcept override;
    void noteOff() noexcept override;
    void renderAdd(core::AudioBlock& block) noexcept override;
    [[nodiscard]] bool isActive() const noexcept override;
    [[nodiscard]] core::EngineKind kind() const noexcept override { return core::EngineKind::Sample; }
    // Sample voices are the cheapest fallback and do not participate in tier
    // demotion; they report the lowest tier for scheduling purposes.
    [[nodiscard]] core::VoiceTier tier() const noexcept override { return core::VoiceTier::Modal; }
    [[nodiscard]] float cpuCostEstimate() const noexcept override;

    // ---- Synthesis-side configuration (off the audio thread) ----------------

    /// Inject the shared render environment.
    void setContext(const VoiceContext& context) noexcept { context_ = context; }

    /// Apply the resolved per-pipe voicing variance.
    void setVoicing(const VoicingProfile& voicing) noexcept { voicing_ = voicing; }

    /// Wire the (already-loaded, non-owning) recorded sample source.
    void setSampleSource(const ISampleSource* source) noexcept { source_ = source; }

    /// Set the multi-stage release behaviour.
    void setReleaseSpec(const ReleaseSpec& spec) noexcept { release_ = spec; }

private:
    const ISampleSource* source_ = nullptr;
    VoiceContext         context_{};
    VoicingProfile       voicing_{};
    ReleaseSpec          release_{};
    ArEnvelope           envelope_{};

    core::SampleRate sampleRate_ = 0.0;
    double           cursor_     = 0.0;  ///< Fractional read position in source frames.
    double           step_       = 1.0;  ///< Frames advanced per output sample (pitch + SR ratio).
    double           heldSeconds_ = 0.0; ///< Time held, for hold-dependent release.
    float            levelGain_  = 1.0f; ///< Velocity + per-pipe level trim.
    bool             oneShotDone_ = false;
};

} // namespace ceciliae::synth
