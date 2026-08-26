// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstdint>

namespace caecilia::core
{

// ---------------------------------------------------------------------------
// Fundamental scalar aliases.
// ---------------------------------------------------------------------------

/// Audio sample rate in Hertz. Positive; set once per prepare().
using SampleRate = double;

/// A MIDI note number in [0, 127] (60 == middle C).
using MidiNote = std::uint8_t;

/// A MIDI velocity in [0, 127] (0 conventionally treated as note-off).
using Velocity = std::uint8_t;

// ---------------------------------------------------------------------------
// Stable identity keys. These are value types compared by content and are used
// as map keys and as the deterministic seed for per-pipe hand-voicing variance.
// ---------------------------------------------------------------------------

/// Identifies a rank (a homogeneous set of pipes) within a loaded organ.
struct RankId
{
    std::uint16_t value = 0;
    friend constexpr bool operator==(RankId, RankId) noexcept = default;
};

/// Identifies a drawstop within a loaded organ.
struct StopId
{
    std::uint16_t value = 0;
    friend constexpr bool operator==(StopId, StopId) noexcept = default;
};

/// Identifies a division / manual (Great, Swell, Pedal, ...).
struct DivisionId
{
    std::uint16_t value = 0;
    friend constexpr bool operator==(DivisionId, DivisionId) noexcept = default;
};

/**
 * @brief Stable identity of a single physical pipe: (division, rank, note).
 *
 * PipeId is the deterministic seed for the PerPipeVoicer, so a given pipe
 * always receives the same hand-voicing variance across runs and sessions.
 */
struct PipeId
{
    std::uint16_t rankId     = 0; ///< Owning rank (mirrors RankId::value).
    std::uint8_t  midiNote   = 0; ///< Pipe's nominal MIDI note.

    /// Owning division (mirrors DivisionId::value, truncated to 8 bits).
    ///
    /// Without this the SAME key on two manuals produced the same PipeId, and the
    /// consequences ran deep: a note-off on one manual released the note on every
    /// other one, a physical keyboard could only ever reach one division, couplers
    /// had nothing to address, and two voices sounding the "same" pipe were seeded
    /// identically -- so they were phase-coherent and summed at +6 dB instead of
    /// +3, which is the very hotspot the per-voice decorrelation exists to avoid.
    ///
    /// It costs nothing: the struct already had this byte as padding.
    std::uint8_t  divisionId = 0;

    friend constexpr bool operator==(PipeId, PipeId) noexcept = default;
};

// ---------------------------------------------------------------------------
// Semantic tonal taxonomy. Shared verbatim by synth, model, registration, ui.
// ---------------------------------------------------------------------------

/// The tonal family a stop belongs to; drives wind-response curves and selectors.
enum class TonalFamily : std::uint8_t
{
    Principal,   ///< Diapason / principal chorus.
    Flute,       ///< Open and stopped flutes.
    String,      ///< Narrow-scale strings (Salicional, Viole, ...).
    Reed,        ///< Beating and free reeds (Trumpet, Oboe, ...).
    Mixture,     ///< Multi-rank compound stops.
    Hybrid,      ///< Cross-family voices (e.g. string-toned principals).
    Percussion,  ///< Struck/plucked voices (chimes, harp, ...).
    Undefined
};

/// The role a stop plays when building a chorus / plenum.
enum class ChorusRole : std::uint8_t
{
    Foundation,  ///< 16'/8' body.
    Chorus,      ///< 4'/2' upperwork below the crown.
    MixtureCrown,///< Mixtures topping the chorus.
    Solo,        ///< Exposed melodic voice.
    Color,       ///< Characterful accompaniment colour.
    Effect,      ///< Zimbelstern, birdsong, etc.
    Undefined
};

/// The broad pitch class of a stop relative to unison (8').
enum class PitchClass : std::uint8_t
{
    Sub,      ///< Below unison (32', 16').
    Unison,   ///< 8'.
    Octave,   ///< 4'.
    Super,    ///< 2' and above octave-related.
    Mutation, ///< Non-octave harmonics (quints, tierces).
    Compound  ///< Multi-rank / mixture pitch.
};

/// The concrete synthesis engine backing a voice layer.
enum class EngineKind : std::uint8_t
{
    Sample,    ///< Streamed recorded samples (safe fallback tier).
    Additive,  ///< Wind-modulated partial bank.
    Waveguide, ///< Nonlinear jet + waveguide (flues).
    Modal      ///< Beating reed + modal resonator (reeds / percussion).
};

/// The quality tier a voice runs at. Demotion under CPU load is the design
/// intent but is not implemented; see VoiceScheduler::renderBatch().
enum class VoiceTier : std::uint8_t
{
    Modal,     ///< Cheapest partial/modal approximation.
    Additive,  ///< Mid-cost wind-modulated additive.
    Waveguide  ///< Full nonlinear physical model (most expensive).
};

// ---------------------------------------------------------------------------
// Footage: the sounding pitch of a rank expressed as an EXACT reduced rational
// relative to 8' unison. 8' == 8/1, 4' == 8/2 (reduced to 4/1), 2 2/3' == 8/3,
// 1 3/5' == 8/5. Keeping this exact is a correctness requirement so pitch-based
// selectors and build-plenum never pollute a principal chorus with quints or
// tierces.
// ---------------------------------------------------------------------------

/**
 * @brief Exact rational organ footage, stored reduced.
 *
 * The value is feet of the sounding pitch: @c num / @c den feet. It is always
 * held in lowest terms with a positive denominator. Construction reduces.
 */
struct Footage
{
    std::int32_t num = 8; ///< Numerator (feet), reduced.
    std::int32_t den = 1; ///< Denominator, reduced, > 0.

    constexpr Footage() noexcept = default;

    /// Construct from a raw fraction; reduces to lowest terms automatically.
    constexpr Footage(std::int32_t n, std::int32_t d) noexcept
    {
        if (d < 0) { n = -n; d = -d; }
        if (d == 0) { n = 8; d = 1; }
        const std::int32_t g = gcd(n < 0 ? -n : n, d);
        num = g ? n / g : n;
        den = g ? d / g : d;
    }

    /// @return Length in decimal feet (8' -> 8.0, 2 2/3' -> 2.6667).
    [[nodiscard]] constexpr double feet() const noexcept
    {
        return static_cast<double>(num) / static_cast<double>(den);
    }

    /**
     * @return true if this footage is a non-octave mutation (a quint, tierce,
     *         septième, ...). An octave-related footage reduces to a power of
     *         two ratio against 8'; anything else is a mutation.
     */
    [[nodiscard]] constexpr bool isMutation() const noexcept
    {
        // Ratio to 8' unison is (8 * den) / (num). Octave-related iff that ratio
        // is a power of two. Equivalently: num divides 8*den into a power of two.
        std::int32_t r_num = 8 * den; // ratio numerator vs unison
        std::int32_t r_den = num;     // ratio denominator vs unison
        const std::int32_t g = gcd(r_num < 0 ? -r_num : r_num, r_den < 0 ? -r_den : r_den);
        if (g) { r_num /= g; r_den /= g; }
        return !(isPowerOfTwo(r_num) && isPowerOfTwo(r_den));
    }

    /**
     * @brief Octave class relative to 8' unison for octave-related footages.
     * @return 0 for 8', +1 for 4', +2 for 2', -1 for 16', etc.; 0 for mutations.
     *
     * Sounding pitch of an N' rank is one octave higher than 8' for each halving
     * of length. This is exact only for octave-related footages; mutations map
     * to their nearest lower octave (return 0) and should be filtered via
     * isMutation() before relying on it.
     */
    [[nodiscard]] constexpr std::int32_t octaveClassFrom8() const noexcept
    {
        if (isMutation() || num == 0)
            return 0;
        // 8' has ratio 1; each octave up doubles the ratio (=> halves the feet).
        // Count signed power of two of (8*den)/num.
        std::int32_t r_num = 8 * den;
        std::int32_t r_den = num;
        const std::int32_t g = gcd(r_num < 0 ? -r_num : r_num, r_den < 0 ? -r_den : r_den);
        if (g) { r_num /= g; r_den /= g; }
        return log2Exact(r_num) - log2Exact(r_den);
    }

    friend constexpr bool operator==(Footage, Footage) noexcept = default;

private:
    static constexpr std::int32_t gcd(std::int32_t a, std::int32_t b) noexcept
    {
        while (b != 0) { const std::int32_t t = a % b; a = b; b = t; }
        return a;
    }
    static constexpr bool isPowerOfTwo(std::int32_t v) noexcept
    {
        return v > 0 && (v & (v - 1)) == 0;
    }
    static constexpr std::int32_t log2Exact(std::int32_t v) noexcept
    {
        std::int32_t e = 0;
        while (v > 1) { v >>= 1; ++e; }
        return e;
    }
};

// ---------------------------------------------------------------------------
// Common named ranks, as exact Footage constants. Grouped here so the whole
// codebase agrees on the canonical value for each rank.
// ---------------------------------------------------------------------------
namespace footage
{
    // Footage stores feet directly as num/den (e.g. 32' == 32/1, 2 2/3' == 8/3).
    inline constexpr Footage kThirtyTwo   {32, 1};   // 32'
    inline constexpr Footage kSixteen     {16, 1};   // 16'
    inline constexpr Footage kEight       {8, 1};    // 8'  (unison)
    inline constexpr Footage kFiveAndThird{16, 3};   // 5 1/3'  (= 16/3 feet)
    inline constexpr Footage kFour        {4, 1};    // 4'
    inline constexpr Footage kTwoAndTwoThird{8, 3};  // 2 2/3'  (= 8/3 feet)
    inline constexpr Footage kTwo         {2, 1};    // 2'
    inline constexpr Footage kOneAndThreeFifth{8, 5};// 1 3/5'  (= 8/5 feet)
    inline constexpr Footage kOneAndThird {4, 3};    // 1 1/3'  (= 4/3 feet)
    inline constexpr Footage kOne         {1, 1};    // 1'
} // namespace footage

} // namespace caecilia::core
