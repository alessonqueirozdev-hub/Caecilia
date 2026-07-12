/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ceciliae::synth
{

/**
 * @brief One tracked partial of a pipe's steady-state timbre.
 *
 * A SpectralModel is produced OFF the audio thread by the proprietary offline
 * analysis toolchain (partial tracking on reference recordings). Each track
 * carries not just a static amplitude but a @ref windSensitivity so the modeled
 * sustain can breathe with the wind supply, and an @ref onsetSeconds so the
 * chiff/speech transient can EMERGE from staggered per-partial onsets instead of
 * being a canned, identical attack.
 */
struct PartialTrack
{
    float ratioToF0        = 1.0f; ///< Frequency as a ratio to the fundamental (1 = fundamental).
    float ampDb            = 0.0f; ///< Steady-state level in decibels (0 dB = reference).
    float phase            = 0.0f; ///< Initial phase in radians (used for phase-aligned splices).
    float windSensitivity  = 0.0f; ///< How strongly this partial tracks wind-pressure deviation.
    float onsetSeconds     = 0.0f; ///< Delay before this partial speaks (staggered chiff emergence).
    float brightnessTrack  = 0.0f; ///< Extra HF development gained as wind pressure rises.
};

/// A single resonant formant peak of the steady-state spectral envelope.
struct FormantPeak
{
    float centerHz   = 0.0f; ///< Formant centre frequency in Hz.
    float gainDb     = 0.0f; ///< Peak gain in decibels.
    float bandwidthHz = 0.0f;///< -3 dB bandwidth in Hz.
};

/// Maximum number of formant peaks a steady-state envelope carries.
inline constexpr std::size_t kMaxFormants = 6;

/**
 * @brief Fixed-capacity steady-state formant envelope.
 *
 * Kept allocation-free (a @c std::array) so a SpectralModel is cheap to copy and
 * so the envelope can be handed to a voice without touching the heap.
 */
struct FormantEnvelope
{
    std::array<FormantPeak, kMaxFormants> peaks{};
    std::size_t                           peakCount = 0; ///< Active peaks in @ref peaks.
};

/**
 * @brief The analysis bridge that lets a sampled attack and a modeled sustain
 *        share one timbre.
 *
 * The SpectralModel seeds an @ref IModeledSustain (see @ref PartialBank) so the
 * loop-free sustain reconstructs the same partial structure the recorded attack
 * fades out of, making the attack-splice spectrum-continuous. It is a setup-time
 * descriptor produced off the audio thread; the RT path only ever reads a
 * seeded, pre-sized partial bank derived from it, never this vector directly.
 */
struct SpectralModel
{
    std::vector<PartialTrack> partials;             ///< Tracked partials (off-thread; may allocate).
    FormantEnvelope           steadyFormants{};     ///< Steady-state formant envelope.
    float                     fundamentalHz = 0.0f; ///< Fundamental the analysis was taken at.

    /// @return Number of partials in the model.
    [[nodiscard]] std::size_t size() const noexcept { return partials.size(); }

    /// @return true if the model carries no usable partial data.
    [[nodiscard]] bool isEmpty() const noexcept { return partials.empty(); }
};

} // namespace ceciliae::synth
