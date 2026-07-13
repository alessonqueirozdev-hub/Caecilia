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
#include <cstdint>
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
    float blockGain       = 0.0f; ///< Target gain for the current block (per-block scratch).
    float curGain         = 0.0f; ///< Actual gain, ramped per-sample toward blockGain (persistent).
    float gainInc         = 0.0f; ///< Per-sample gain ramp step for the current block (scratch).

    // --- "Living pipe" state (Aeolus-inspired) -------------------------------
    // These turn a sterile, perfectly-harmonic Fourier stack into an organic
    // pipe: a slow independent pitch drift per partial (the beating that reads as
    // a chorus of real pipes), and a per-partial attack bloom time so upper
    // partials speak later than the fundamental instead of all clicking at once.
    float driftCents      = 0.0f; ///< Current slow random-walk detune, in cents (persistent).
    std::uint32_t rng     = 0x2545F491u; ///< Per-partial PRNG state for the drift walk.
    float bloomSeconds    = 0.010f; ///< Per-partial attack bloom time (derived at seed/trigger).

    // --- Per-note voicing baked at note-on (off the hot loop) ----------------
    int   harmonicIndex   = 1;     ///< Nearest integer harmonic number (>=1).
    float noteLevelScale  = 1.0f;  ///< Per-note treble tilt (upper partials voiced down on high notes).
    float formantGain     = 1.0f;  ///< Fixed-formant boost at this partial's absolute Hz (reeds).
    bool  active          = true;  ///< False when blockGain is ~0 (skip the sinf, keep phase coherent).
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

    /**
     * @brief Tune the "living pipe" character (Aeolus-inspired liveliness).
     *
     * @param instabilityCents Peak amplitude of the slow per-partial pitch drift
     *        (the beating that reads as a chorus of real pipes; 0 = sterile).
     * @param attackGlideCents How far below pitch the note starts before gliding
     *        up as the pipe speaks (a labial pipe "pulls" into tune).
     * @param maxBloomSeconds  Longest attack-bloom of the highest partials (the
     *        fundamental always blooms fast; uppers speak progressively later).
     * @param hfRolloffHz      Corner of the gentle per-note top-octave tilt that
     *        tames metallic upper partials (higher = brighter).
     */
    void setLiveliness(float instabilityCents,
                       float attackGlideCents,
                       float maxBloomSeconds,
                       float hfRolloffHz,
                       float trebleTiltDb = 6.0f) noexcept;

    /**
     * @brief Set the wind-attack "chiff" amount (the breathy speech transient a
     *        real pipe makes as it starts to sound). 0 disables it.
     * @param amount Linear level of the filtered-noise burst (subtle; ~0.02–0.08).
     */
    void setChiff(float amount) noexcept { chiffAmount_ = amount < 0.0f ? 0.0f : amount; }

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
    float  attackSeconds_  = 0.016f;
    float  releaseSeconds_ = 0.150f;
    double noteTimeSeconds_ = 0.0;  ///< Elapsed time since trigger (for staggered onsets).

    // --- "Living pipe" voicing (Aeolus-inspired) -----------------------------
    float  instabilityCents_ = 5.5f;   ///< Peak slow per-partial pitch drift.
    float  attackGlideCents_ = -18.0f; ///< Speech pitch glide during the attack.
    float  glideSeconds_     = 0.055f; ///< Duration of the attack pitch glide.
    float  maxBloomSeconds_  = 0.060f; ///< Longest per-partial attack bloom (uppers).
    float  hfRolloffHz_      = 7000.0f;///< Corner of the gentle absolute-Hz top tilt (raised; per-note tilt does the work).
    float  trebleTiltDb_     = 6.0f;   ///< Per-octave upper-harmonic roll on high notes (Aeolus _h_lev).

    // Fixed-formant envelope (reeds); flues carry peakCount==0 and are unaffected.
    FormantEnvelope steadyFormants_{};

    // --- Wind-attack chiff (breathy speech transient) ------------------------
    float         chiffAmount_    = 0.0f;        ///< Linear level of the chiff burst (0 = off).
    float         chiffEnv_       = 0.0f;        ///< Current chiff envelope value.
    float         chiffLp_        = 0.0f;        ///< One-pole low-pass state for the noise.
    float         chiffLpCoef_    = 0.3f;        ///< One-pole coefficient (from pitch, per note).
    float         chiffDecayMul_  = 0.999f;      ///< Per-sample exponential decay after the peak.
    int           chiffSamp_      = 0;           ///< Samples elapsed in the chiff burst.
    int           chiffAtkSamp_   = 96;          ///< Chiff attack length in samples.
    int           chiffLenSamp_   = 0;           ///< Total chiff burst length (0 => inactive).
    std::uint32_t chiffRng_       = 0x1234567u;  ///< PRNG state for the chiff noise.

    // Wind coupling.
    const core::IWindSupply* wind_ = nullptr;
    core::WindchestId        chest_{};
    wind::WindResponseCurve  windCurve_{};
};

} // namespace caecilia::synth
