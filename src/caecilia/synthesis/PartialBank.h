// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

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
 * Holds both the persistent oscillator state (@ref oscX / @ref oscY) and the
 * per-block scratch (@ref cosInc, @ref sinInc, @ref blockGain) recomputed at
 * each block boundary so the inner sample loop stays branch-light and
 * allocation-free.
 */
struct Partial
{
    float ratioToF0       = 1.0f; ///< Frequency ratio to the fundamental.
    float amplitude       = 0.0f; ///< Linear steady-state amplitude.
    float windSensitivity = 0.0f; ///< Per-partial scaling of the wind response.
    float onsetSeconds    = 0.0f; ///< Staggered onset delay (chiff/speech emergence).

    // --- Recursive quadrature oscillator (replaces a per-sample std::sin) ------
    // The partial is a unit phasor advanced by a complex rotation each sample:
    //     x' = x*cosInc - y*sinInc      y' = x*sinInc + y*cosInc
    // with y == sin(phase), so the output is read straight off `oscY`. That is
    // ~4 multiplies and 2 adds instead of a transcendental, it vectorises, and it
    // is the difference between a full-organ Tutti being impossible and being
    // comfortable: the bank runs one oscillator per partial per voice, and a
    // 26-stop registration is ~291 partials.
    //
    // The rotation is only conditionally stable in floating point — |z| drifts —
    // so the magnitude is renormalised once per block by one Newton step, which
    // costs nothing at block rate. See recomputeBlockCoefficients().
    float oscX            = 1.0f; ///< cos(phase) — persistent oscillator state.
    float oscY            = 0.0f; ///< sin(phase) — persistent; this IS the output.
    float cosInc          = 1.0f; ///< cos(2*pi*f/sr) for this block (per-block scratch).
    float sinInc          = 0.0f; ///< sin(2*pi*f/sr) for this block (per-block scratch).

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
    std::uint32_t seed    = 0u;   ///< Stable identity from the spectrum (NOT the array index).
    float rankRatio       = 0.0f; ///< Pitch of the rank this partial belongs to; 0 = none.
    std::int8_t rankSeries = -1;  ///< That rank's index in kMixtureSeries, or -1.
    float soundingRatio   = 1.0f; ///< ratioToF0 after any break-back, resolved at note-on.
    float logRatio        = 0.0f; ///< log2(ratioToF0), precomputed at seed time.
    /// log2(soundingRatio). Not the same as @ref logRatio once a break has moved
    /// the rank, and it is the one anything about ABSOLUTE pitch has to use.
    float logSounding     = 0.0f;

    // --- Placement in the case ------------------------------------------------
    // Each RANK occupies its own position on the windchest, so the same key played
    // through two different ranks comes from two different points in the case. That
    // is why a Principal 8' and a Bourdon 8' reinforce rather than cancel, even
    // though their fundamentals are within a cent of each other: they are not one
    // coherent source. Collapsing every rank to a single point made drawing a
    // second unison stop measurably QUIETER than drawing one.
    float panL            = 0.70710678f; ///< Left  gain for this partial's rank.
    float panR            = 0.70710678f; ///< Right gain for this partial's rank.
    float bloomSeconds    = 0.010f; ///< Per-partial attack bloom time (derived at seed/trigger).

    // --- Per-note voicing baked at note-on (off the hot loop) ----------------
    int   harmonicIndex   = 1;     ///< Nearest integer harmonic number (>=1).
    float logHarmonic     = 0.0f;  ///< log2(harmonicIndex), precomputed off the hot path.
    float noteLevelScale  = 1.0f;  ///< Per-note treble tilt (upper partials voiced down on high notes).
    float formantGain     = 1.0f;  ///< Fixed-formant boost at this partial's absolute Hz (reeds).
};

/**
 * @brief How fast a pipe speaks and how fast it stops, as a function of size.
 *
 * A real rank's speech time is dominated by how long the wind takes to fill the
 * pipe and set the air column oscillating, so it scales with pipe LENGTH: a 16'
 * Bourdon takes a substantial fraction of a second to come fully on speech,
 * while a small mixture pipe is essentially instantaneous. The release behaves
 * the same way — a big pipe keeps sounding as its column collapses.
 *
 * A single fixed attack/release for every rank and every note (which is what a
 * one-envelope-per-registration design forces) is the single most audible
 * departure from a real instrument after registration dynamics, so the times
 * are interpolated per note between a bass and a treble anchor.
 */
struct SpeechProfile
{
    float attackAtC2Sec  = 0.055f; ///< Attack for a ~65 Hz pipe (16'/8' bass).
    float attackAtC7Sec  = 0.010f; ///< Attack for a ~2 kHz pipe (small treble).
    float releaseAtC2Sec = 0.300f; ///< Release for a ~65 Hz pipe.
    float releaseAtC7Sec = 0.110f; ///< Release for a ~2 kHz pipe.
};

/**
 * @brief An additive/modal partial bank — the modeled sustain tier, and the
 *        only voice layer the shipping plugin renders.
 *
 * PartialBank implements @ref IModeledSustain: it is seeded from a
 * @ref SpectralModel (today a hand-authored recipe built by the @c model
 * module) and, when a wind supply is wired, the pitch and level of every partial
 * track pressure deviation through a per-family @ref WindResponseCurve rather
 * than a stored table. This is the tier that carries plenum polyphony.
 *
 * @todo Nothing wires a wind supply in the plugin's signal path — the
 *       @c VoiceContext it builds carries a null @c IWindSupply — so
 *       @ref setWindCoupling receives a null source, the deviation reads 0 and
 *       the breathing is inert. The curve's brightness and attack axes are not
 *       consumed at all, and there is no attack-splice or tier demotion for this
 *       bank to be the target of.
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

    /// Stop dead: envelope to zero, stage to Idle, every partial's gain cleared.
    /// The oscillator phasors are left turning -- they cost nothing while the
    /// gains are zero, and re-randomising them would only undo the per-voice
    /// decorrelation the next note-on relies on. RT-safe.
    void silence() noexcept;

    /// This block's swell-shoe ramp. See @c core::IVoice::setExpression. RT-safe.
    void setExpression(float startGain, float incPerSample) noexcept
    {
        exprGain_ = startGain;
        exprInc_  = incPerSample;
    }
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
     * @brief Set the per-family speech timing curve (see @ref SpeechProfile).
     *
     * @ref trigger interpolates the envelope times from this by the note's own
     * pitch, so a 16' pedal note and a treble mixture no longer share one
     * envelope. Off-thread configuration; RT-safe to call.
     */
    void setSpeechProfile(const SpeechProfile& profile) noexcept { speech_ = profile; }

    /**
     * @brief Set the stereo spread of the chest layout, 0 = mono, 1 = widest.
     *
     * Real windchests are laid out C-side / C#-side: consecutive semitones sit on
     * OPPOSITE halves of the case. Panning by note parity reproduces that, and is
     * what turns a dead-centre additive stack into an instrument with width.
     * Applied per voice at @ref trigger from the pipe's own identity, so it is
     * stable for a given pipe across runs.
     */
    void setStereoSpread(float spread) noexcept;

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

    /// @return Current audible level of the bank, linear, roughly 0..1. Used by
    ///         the pool to pick the least audible voice to steal. RT-safe.
    [[nodiscard]] float levelEstimate() const noexcept
    {
        // Every gain the render applies, in the same order renderAdd applies them
        // -- because this number's whole job is to answer "how much would be lost
        // if this voice stopped", and a factor left out of it is a factor the
        // answer is wrong by.
        //
        // The expression term is what makes this worth stating: it used to be
        // missing, and a pipe behind a shut swell box is thirteen decibels down
        // and the first thing an organist would give up. Without it the box was
        // invisible to both the pool's victim choice and the CPU governor's.
        //
        // Two deliberate omissions. The envelope's smoothstep shaping is not
        // duplicated here: it is monotonic in envGain_, so it cannot change the
        // ORDER this is used to establish, and it is within two decibels of the
        // raw value at every point. And exprGain_ is where the last block's ramp
        // left it rather than where this block's will end, because the scheduler
        // sets the ramp while rendering and this is read before the render -- one
        // block of lag on a shoe that takes a second to travel.
        return stage_ == Stage::Idle
                 ? 0.0f
                 : envGain_ * masterGain_ * velocityGain_ * exprGain_;
    }

    /// How a stop's ranks move when they break back.
    enum class BreakMode : std::uint8_t
    {
        None,   ///< Nothing here breaks.
        Series, ///< A COMPOUND stop: one row down kMixtureSeries per break.
        Octave  ///< A SINGLE rank: an octave per break, so a Tierce stays a tierce.
    };

    /// @return How this bank's ranks break, decided in @ref seedFrom from the
    ///         spectrum it was given.
    [[nodiscard]] BreakMode breakMode() const noexcept { return breakMode_; }

    /// @return The most breaks this composition can take before its lowest rank
    ///         would reach the unison, which is where breaking stops.
    [[nodiscard]] int maxBreakShift() const noexcept { return breakMaxShift_; }

    /**
     * @brief How far this bank's ranks have broken back at @p note.
     * @return Rows down the series (@c Series) or octaves (@c Octave); 0 below the
     *         first break and never more than @ref maxBreakShift.
     *
     * Public because it is the thing worth testing, and because a tool that wants
     * to print a stop's break scheme should not have to sound a note to get it.
     * Pure: depends on the note number and the bank's composition, and on nothing
     * else -- not on the tuning, not on the sample rate. A break is a fact about
     * the instrument's compass, and moving it when the organist changes
     * temperament would be absurd.
     */
    [[nodiscard]] int breakShiftForNote(core::MidiNote note) const noexcept;

    /**
     * @brief What a rank at @p rankRatio sounds at once it has broken @p shift.
     * @return The rank's new pitch as a ratio to 8' unison.
     */
    [[nodiscard]] float brokenRankRatio(float rankRatio, int seriesIndex,
                                        int shift) const noexcept;

    /// @return Number of partials currently seeded.
    [[nodiscard]] std::size_t activePartialCount() const noexcept { return partialCount_; }

private:
    enum class Stage : std::uint8_t { Idle, Attack, Sustain, Release };

    /// @param frames Block length, so the control-rate drift filter can be given a
    ///        coefficient in TIME rather than in blocks (see the .cpp).
    void recomputeBlockCoefficients(std::size_t frames) noexcept;

    std::vector<Partial> partials_;                 ///< Reserved in prepare(); never re-allocated on the hot path.

    /// Frames a single render chunk covers.
    ///
    /// A fixed 512 rather than the host's block size, because this is memory
    /// multiplied by the POOL: one voice per rank means 512 banks, and three
    /// buffers each at a 2048-frame host buffer would be 12.6 MB — touched every
    /// block, so a cache figure and not merely a memory one. renderAdd loops.
    static constexpr std::size_t kChunkFrames = 512;

    /// Per-chunk accumulators: the partials sum here before the bank envelope is
    /// applied, which is what lets the inner loop run partial-outer and vectorise.
    std::vector<float> chunkL_;
    std::vector<float> chunkR_;
    std::vector<float> chunkEnv_;
    std::size_t          maxPartials_  = kDefaultMaxPartials;
    std::size_t          partialCount_ = 0;         ///< Seeded partials in [0, partials_.size()].

    double sampleRate_    = 0.0;
    double fundamentalHz_ = 0.0;
    float  masterGain_    = 0.25f; ///< Base level (owner-set; e.g. per-pipe level trim).

    // Swell shoe, as a per-sample ramp. Folded into the envelope multiply that was
    // already there, so an enclosed division costs one extra multiply-add per
    // sample and an unenclosed one costs the same with inc == 0.
    float  exprGain_      = 1.0f;
    float  exprInc_       = 0.0f;
    float  velocityGain_  = 1.0f;  ///< Note-velocity gain, set on trigger().

    /// What the seeded spectrum said about breaking. Derived once per re-voicing.
    BreakMode breakMode_      = BreakMode::None;
    float     breakTopRatio_  = 0.0f; ///< Highest rank pitch in the stop.
    int       breakTopSeries_ = -1;   ///< Its series index (Series mode only).
    int       breakMaxShift_  = 0;    ///< Breaks available before the unison.

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

    // --- Per-family speech timing and chest placement -------------------------
    SpeechProfile speech_{};           ///< Attack/release anchors, interpolated per note.
    float  stereoSpread_ = 0.55f;      ///< 0 = mono, 1 = widest C-side / C#-side split.
    float  panL_         = 0.70710678f;///< Left  gain for this voice (equal power).
    float  panR_         = 0.70710678f;///< Right gain for this voice (equal power).

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
