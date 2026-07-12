/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/synthesis/IVoiceLayer.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/wind/WindResponseCurve.h"

#include <cstddef>
#include <vector>

namespace caecilia::synth
{

/**
 * @brief One partial in a @ref PartialBank at render time.
 *
 * Holds both the persistent oscillator state (@ref phase) and per-block scratch
 * (@ref increment, @ref blockGain) recomputed at each block boundary so the
 * inner sample loop stays branch-light and allocation-free.
 */
struct Partial
{
    float ratioToF0       = 1.0f; ///< Frequency ratio to the fundamental.
    float amplitude       = 0.0f; ///< Linear steady-state amplitude.
    float windSensitivity = 0.0f; ///< Per-partial scaling of the wind response.
    float onsetSeconds    = 0.0f; ///< Staggered onset delay (chiff/speech emergence).
    float phase           = 0.0f; ///< Current oscillator phase in radians (persistent).
    float increment       = 0.0f; ///< Phase increment per sample (per-block scratch).
    float blockGain       = 0.0f; ///< Effective gain for the current block (per-block scratch).
};

/**
 * @brief A wind-modulated additive/modal partial bank — the CPU-cheap modeled
 *        sustain tier, and the reconstruction target of the attack-splice.
 *
 * PartialBank implements @ref IModeledSustain: it is seeded from a
 * @ref SpectralModel so its timbre matches a recorded attack, and every partial
 * is a function of the wind supply (pitch/level/brightness track pressure
 * deviation through a per-family @ref WindResponseCurve) rather than a stored
 * table. This is the tier used for plenum polyphony and the demotion target when
 * the deadline budget sheds the nonlinear physical tier.
 *
 * ## Real-time contract
 * - @ref prepare reserves the partial storage; it is the ONLY allocating method.
 * - @ref seedFrom, @ref trigger, @ref release, @ref renderAdd and @ref isActive
 *   are @c noexcept, allocation-free and lock-free.
 * - @ref renderAdd accumulates into the block (+=), never overwrites.
 */
class PartialBank final : public IModeledSustain
{
public:
    /// Default maximum number of partials reserved by @ref prepare.
    static constexpr std::size_t kDefaultMaxPartials = 64;

    PartialBank() = default;

    // ---- IVoiceLayer / IModeledSustain --------------------------------------

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames) override;
    void seedFrom(const SpectralModel& model, float phaseAlignSeconds) noexcept override;
    void trigger(core::PipeId pipe, core::Velocity velocity, double frequencyHz) noexcept override;
    void release() noexcept override;
    void renderAdd(core::AudioBlock& block) noexcept override;
    [[nodiscard]] bool isActive() const noexcept override;

    // ---- Synthesis-side configuration (off the audio thread) ----------------

    /// Set the maximum partial capacity honoured by the next @ref prepare.
    void setMaxPartials(std::size_t maxPartials) noexcept { maxPartials_ = maxPartials; }

    /**
     * @brief Wire the wind coupling for this bank.
     * @param wind  Non-owning wind snapshot source (may be null to disable).
     * @param chest Windchest feeding this bank's pipe.
     * @param curve Per-tonal-family wind-response curve (owned by the @c wind module).
     */
    void setWindCoupling(const core::IWindSupply* wind,
                         core::WindchestId chest,
                         wind::WindResponseCurve curve) noexcept;

    /// Set the linear output gain applied to the whole bank. RT-safe.
    void setMasterGain(float gain) noexcept { masterGain_ = gain; }

    /// Set the attack/release ramp times in seconds. RT-safe.
    void setEnvelopeTimes(float attackSeconds, float releaseSeconds) noexcept;

    /// @return Number of partials currently seeded.
    [[nodiscard]] std::size_t activePartialCount() const noexcept { return partialCount_; }

private:
    enum class Stage : std::uint8_t { Idle, Attack, Sustain, Release };

    void recomputeBlockCoefficients() noexcept;

    std::vector<Partial> partials_;                 ///< Reserved in prepare(); never re-allocated on the hot path.
    std::size_t          maxPartials_  = kDefaultMaxPartials;
    std::size_t          partialCount_ = 0;         ///< Seeded partials in [0, partials_.size()].

    double sampleRate_    = 0.0;
    double fundamentalHz_ = 0.0;
    float  masterGain_    = 0.25f; ///< Base level (owner-set; e.g. per-pipe level trim).
    float  velocityGain_  = 1.0f;  ///< Note-velocity gain, set on trigger().

    // Simple linear attack/release envelope gating the whole bank.
    Stage  stage_          = Stage::Idle;
    float  envGain_        = 0.0f;
    float  attackStep_     = 1.0f;  ///< Per-sample gain increment during attack.
    float  releaseStep_    = 1.0f;  ///< Per-sample gain decrement during release.
    float  attackSeconds_  = 0.005f;
    float  releaseSeconds_ = 0.150f;
    double noteTimeSeconds_ = 0.0;  ///< Elapsed time since trigger (for staggered onsets).

    // Wind coupling.
    const core::IWindSupply* wind_ = nullptr;
    core::WindchestId        chest_{};
    wind::WindResponseCurve  windCurve_{};
};

} // namespace caecilia::synth
