/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/IReverb.h"
#include "caecilia/dsp/OnePole.h"

#include <array>
#include <cstddef>
#include <vector>

/**
 * @file FdnReverb.h
 * @brief A 16-line Feedback Delay Network reverberator.
 *
 * Written fresh from the public principles of Jot's unitary-feedback FDN (1991):
 * a bank of mutually-incommensurate delay lines whose outputs are recombined
 * every sample through a lossless (orthonormal Hadamard) mixing matrix and fed
 * back with per-line gains that set the decay time, plus a one-pole damper per
 * line so high frequencies decay faster than lows. No GPL DSP source (GrandOrgue
 * or otherwise) is referenced; the Hadamard mixing and RT60-to-gain mapping are
 * standard public mathematics.
 *
 * A short cathedral-friendly set of presets is provided.
 */

namespace caecilia::dsp
{

/// Ready-made voicings covering common organ acoustics.
enum class ReverbPreset
{
    Room,       ///< Small, dry practice room.
    Chamber,    ///< Medium chamber / large room.
    Hall,       ///< Concert hall.
    Cathedral,  ///< Long, bright stone reverberation.
    Plate       ///< Dense, fast-onset plate-like tail.
};

/**
 * @brief 16-line Jot-style FDN reverb implementing @ref core::IReverb.
 *
 * ## Real-time contract
 * - @ref prepare allocates the delay lines and pre-delay buffer and precomputes
 *   coefficients. Not RT-safe.
 * - @ref setParams / @ref setPreset recompute feedback gains and damper cutoffs
 *   in place (a handful of @c pow / @c exp calls, no allocation). RT-safe.
 * - @ref process runs the network in place with denormal-flushed feedback.
 *   RT-safe, @c noexcept.
 * - @ref reset clears every delay line and filter. RT-safe.
 */
class FdnReverb final : public core::IReverb
{
public:
    /// Number of delay lines in the network (fixed by the design).
    static constexpr std::size_t kLines = 16;

    FdnReverb() = default;
    ~FdnReverb() override = default;

    // --- core::IReverb -----------------------------------------------------
    void        prepare(core::SampleRate sampleRate,
                        std::size_t      maxBlockFrames,
                        std::size_t      numChannels) override;
    void        setParams(const core::ReverbParams& params) noexcept override;
    void        process(core::AudioBlock& block) noexcept override;
    void        reset() noexcept override;
    [[nodiscard]] std::size_t latencySamples() const noexcept override { return 0; }

    // --- FDN-specific ------------------------------------------------------

    /// @return The default parameter set for a named preset.
    [[nodiscard]] static core::ReverbParams presetParams(ReverbPreset preset) noexcept;

    /// Apply a preset's parameters. RT-safe (forwards to @ref setParams).
    void setPreset(ReverbPreset preset) noexcept { setParams(presetParams(preset)); }

    /// @return The parameters currently in effect.
    [[nodiscard]] const core::ReverbParams& params() const noexcept { return params_; }

private:
    /// Recompute per-line feedback gains from the current decay time.
    void updateDecay() noexcept;
    /// Recompute per-line damper cutoffs from the current damping corner.
    void updateDamping() noexcept;
    /// Recompute stereo output-tap weights from the current width.
    void updateWidth() noexcept;
    /// (Re)initialise the per-line modulation LFO increments for the sample rate.
    void updateModulation() noexcept;
    /// (Re)compute the early-reflection tap delays and stereo gains.
    void updateEarlyReflections() noexcept;

    core::SampleRate   sampleRate_    = 44100.0;
    std::size_t        maxBlockFrames_ = 0;
    std::size_t        numChannels_   = 2;
    core::ReverbParams params_{};

    std::array<std::vector<float>, kLines> lines_{};      ///< Circular delay buffers.
    std::array<std::size_t, kLines>        lengths_{};    ///< Delay length per line (samples).
    std::array<std::size_t, kLines>        positions_{};  ///< Read/write cursor per line.
    std::array<OnePole, kLines>            dampers_{};     ///< High-frequency damper per line.
    std::array<float, kLines>              feedback_{};    ///< Feedback gain per line.
    std::array<float, kLines>              inject_{};      ///< Input injection sign per line.
    std::array<float, kLines>              tapLeft_{};     ///< Output tap weight -> L.
    std::array<float, kLines>              tapRight_{};    ///< Output tap weight -> R.

    std::vector<float> preDelay_{};                       ///< Pre-delay ring buffer.
    std::size_t        preDelayLen_   = 1;                 ///< Active pre-delay length.
    std::size_t        preDelayPos_   = 0;                 ///< Pre-delay cursor.

    // --- Light tail modulation --------------------------------------------
    // A slow, low-depth per-line LFO jitters each delay-line read tap by a
    // fraction of a sample. This time-varies the modal frequencies just enough
    // to break up the metallic ringing / flutter of a static FDN without
    // audibly detuning the tail. Standard public reverb practice (Dattorro).
    std::array<float, kLines> modPhase_{};   ///< Per-line LFO phase in [0, 1).
    std::array<float, kLines> modInc_{};     ///< Per-line LFO phase step / sample.
    float                     modDepth_ = 0.0f; ///< Modulation depth (samples).

    // --- Early reflections -------------------------------------------------
    // A sparse pattern of discrete, stereo-panned taps of the (mono) input read
    // BEFORE the diffuse tail. This is the cue the ear uses to place itself in a
    // large stone room; without it, a pure FDN tail reads as a synthetic "reverb
    // effect" rather than a real acoustic. The taps also feed the tank so the
    // dense tail is excited by the early pattern (a more natural build-up).
    static constexpr std::size_t kErTaps = 9;
    std::vector<float>                erBuffer_{};      ///< Mono early-reflection ring buffer.
    std::size_t                       erPos_ = 0;       ///< ER buffer write cursor.
    std::array<std::size_t, kErTaps>  erDelay_{};       ///< Tap delays (samples).
    std::array<float, kErTaps>        erGainL_{};       ///< Tap gain -> L.
    std::array<float, kErTaps>        erGainR_{};       ///< Tap gain -> R.
    float                             erSendToTank_ = 0.55f; ///< ER energy folded into the tail.
};

} // namespace caecilia::dsp
