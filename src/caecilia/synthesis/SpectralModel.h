// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace caecilia::synth
{

/**
 * @brief One tracked partial of a pipe's steady-state timbre.
 *
 * A SpectralModel is built OFF the audio thread. The models that ship today are
 * hand-authored recipes from the @c model module; the offline analysis toolchain
 * meant to produce them by partial-tracking real recordings is still a stub.
 * Each track carries not just a static amplitude but a @ref windSensitivity, so
 * the modeled sustain can breathe once a wind supply is wired, and an
 * @ref onsetSeconds so the speech transient EMERGES from staggered per-partial
 * onsets instead of being a canned, identical attack.
 */
struct PartialTrack
{
    float ratioToF0        = 1.0f; ///< Frequency as a ratio to the fundamental (1 = fundamental).

    /// Ratio of the fundamental OF THIS PARTIAL'S OWN RANK.
    ///
    /// A spectrum is 8'-referenced, so a 2' rank's fundamental arrives as ratio 4.
    /// Anything that reasons about harmonic NUMBER — which upper partial this is,
    /// and therefore how hard to voice it down in the treble — has to divide by
    /// this first, or it treats the rank's fundamental as its fourth harmonic and
    /// voices the whole rank away up the compass.
    ///
    /// 1.0 means "unison, or unknown", which is the right answer for a hand-built
    /// model in a test or tool.
    float rankBaseRatio    = 1.0f;
    float ampDb            = 0.0f; ///< Steady-state level in decibels (0 dB = reference).
    /// Initial phase in radians. @ref PartialBank::seedFrom starts the
    /// oscillator here, but note-on then scatters the phase per voice for
    /// decorrelation, so nothing phase-aligned reads it today.
    float phase            = 0.0f;
    float windSensitivity  = 0.0f; ///< How strongly this partial tracks wind-pressure deviation.
    float onsetSeconds     = 0.0f; ///< Delay before this partial speaks (staggered chiff emergence).
    /// How strongly THIS partial follows the wind's brightness axis, on top of
    /// its tonal family's curve.
    ///
    /// The family coefficient (@c wind::WindResponseCurve::brightnessPerDeviation)
    /// says how much a Reed or a Flute dulls as the wind gives way; this says how
    /// much more, or less, one partial of it does. Exactly the role
    /// @ref windSensitivity plays on the pitch axis, and applied the same way, as
    /// @c (1 + track).
    float brightnessTrack  = 0.0f;

    /// Stable identity of this partial, independent of where it lands in a
    /// composite spectrum. Voices derive their start phase and drift stream from
    /// THIS rather than from the partial's array index, so the sound of a
    /// registration does not change with the order the stops were drawn in.
    std::uint32_t seed     = 0u;

    /// The sounding pitch of the RANK this partial belongs to, as a ratio to 8'
    /// unison. Zero means "this partial does not belong to a breaking rank".
    ///
    /// Mixtures and mutations REPEAT: at fixed points up the compass their ranks
    /// BREAK BACK, so the upperwork never climbs out of the audible band. Without
    /// that, a Fourniture's top ranks pass Nyquist in the treble and are muted --
    /// the plenum grows darker up the keyboard, the exact opposite of what a
    /// mixture exists to do, and the timbre changes with the project's sample rate.
    ///
    /// Naming the RANK rather than marking each partial is the whole point. A
    /// partial at ratio 10 might be a 4/5' rank or the second harmonic of a 1 3/5'
    /// Tierce, and those two move differently when the stop breaks. Letting each
    /// partial fold on its own schedule -- which is all a bare flag can express --
    /// let two ranks of a mixture converge onto one pitch, let ranks descend below
    /// the unison, and pulled a mutation's own harmonics apart until the rank was
    /// no longer a harmonic series.
    ///
    /// So a partial that is a HARMONIC of a rank carries the RANK's pitch here,
    /// not its own: it is that pipe speaking, and it moves when the pipe does.
    float rankRatioToF0 = 0.0f;
};

/**
 * @brief The pitches a mixture rank is drawn from, as ratios to 8' unison.
 *
 * 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 -- unison, octave, twelfth, fifteenth,
 * nineteenth, twenty-second, and so on. Every rank of every mixture ever built
 * sits on one of these, and a mixture's composition is always a run of CONSECUTIVE
 * entries: 15-19-22-26 is {4, 6, 8, 12}, indices 3 to 6.
 *
 * That is what makes a break expressible as arithmetic. A break moves the whole
 * composition one entry down the series -- 15-19-22-26 becomes 12-15-19-22 becomes
 * 8-12-15-19 becomes 1-8-12-15 -- so the ranks stay distinct, the interval shape
 * survives, and the window stops when it reaches the bottom, which is the unison.
 * Note that a step down the series is a fourth or a fifth, not an octave; that is
 * what a mixture break actually sounds like.
 */
inline constexpr std::array<float, 10> kMixtureSeries = {
    1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f
};

/**
 * @brief Where @p ratioToUnison sits in @ref kMixtureSeries.
 * @return The index, or -1 if this pitch is not a series member (in which case it
 *         is not a mixture rank and does not break).
 */
[[nodiscard]] inline int mixtureSeriesIndex(float ratioToUnison) noexcept
{
    for (std::size_t i = 0; i < kMixtureSeries.size(); ++i)
        if (ratioToUnison > kMixtureSeries[i] * 0.995f
            && ratioToUnison < kMixtureSeries[i] * 1.005f)
            return static_cast<int>(i);
    return -1;
}

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
 * @brief The timbre a modeled sustain is seeded from — and the bridge a
 *        sampled attack is meant to share it with.
 *
 * The SpectralModel seeds an @ref IModeledSustain (see @ref PartialBank). The
 * intent is that the loop-free sustain reconstructs the same partial structure a
 * recorded attack fades out of, making the splice spectrum-continuous; no attack
 * layer exists yet, so today the model simply IS the whole timbre. It is a
 * setup-time descriptor produced off the audio thread; the RT path only ever
 * reads a seeded, pre-sized partial bank derived from it, never this vector
 * directly.
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

} // namespace caecilia::synth
