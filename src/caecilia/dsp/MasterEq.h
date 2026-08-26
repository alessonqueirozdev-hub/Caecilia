// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/dsp/Biquad.h"

#include <array>
#include <cstddef>

/**
 * @file MasterEq.h
 * @brief A 5-band stereo master EQ voiced for pipe organ.
 *
 * A post-reverb, pre-limiter tone-voicing stage: low shelf (warmth), three
 * parametric peaks (tame boxiness, body, presence) and a high shelf (air). Built
 * on the existing transposed-direct-form-II @ref Biquad, so it is click-free and
 * a band at 0 dB is a true mathematical pass-through (no bypass branch needed).
 *
 * ## Real-time contract
 * - @ref prepare allocates nothing beyond the fixed filter array. Not RT-safe only
 *   because it touches the sample rate.
 * - @ref setBand / @ref setBandGain / @ref setParams store targets; coefficients
 *   are redesigned once per block from a block-rate-smoothed GAIN, so gain moves
 *   never click. @ref setEnabled is smoothed too -- it glides every band to 0 dB,
 *   which is unity here. Frequency and Q are not smoothed. RT-safe.
 * - @ref process filters in place. RT-safe, @c noexcept, allocation-free.
 */
namespace caecilia::dsp
{

class MasterEq
{
public:
    static constexpr std::size_t kBands = 5;

    /// Band roles (fixed filter type per index).
    enum Band { Warmth = 0, Boxiness = 1, Body = 2, Presence = 3, Air = 4 };

    /**
     * @brief A complete EQ setting.
     *
     * Exists so the stage can be snapshotted and restored in one move rather than
     * through five separate setters — which is what the host parameter layer and
     * the saved document both want, and what makes it impossible to restore a
     * gain without its frequency.
     */
    struct Params
    {
        struct BandParams
        {
            float freqHz = 1000.0f;
            float q      = 0.7f;
            float gainDb = 0.0f;
        };

        std::array<BandParams, kBands> bands{};
        bool                           enabled = true;
    };

    MasterEq() = default;

    void prepare(core::SampleRate sampleRate, std::size_t maxBlockFrames, std::size_t numChannels);

    /// Apply a complete setting. Gains glide as usual; frequencies, Qs and the
    /// enable flag take effect from the next block. RT-safe.
    void setParams(const Params& params) noexcept;

    /// @return The setting currently in force, for state saving. RT-safe.
    [[nodiscard]] Params params() const noexcept;

    /// @return The factory pipe-organ voicing, without needing an instance. This
    ///         is the single definition of that curve; @ref setOrganBandShapes and
    ///         @ref setOrganDefaults are two halves of it.
    [[nodiscard]] static Params organDefaults() noexcept;

    /// Set a band's frequency, Q and target gain (dB). Only the GAIN glides; the
    /// frequency and Q take effect at the next block's redesign. RT-safe.
    void setBand(std::size_t index, float freqHz, float q, float gainDb) noexcept;
    /// Convenience: change only a band's gain (keeps its freq/Q). RT-safe.
    void setBandGain(std::size_t index, float gainDb) noexcept;

    void setEnabled(bool on) noexcept
    {
        if (on && !enabled_)
            bypassSettled_ = false; // wake the glide back up
        enabled_ = on;
    }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// @return Band @p index's target gain in dB, for state saving. RT-safe.
    [[nodiscard]] float bandGain(std::size_t index) const noexcept
    {
        return index < kBands ? bands_[index].targetDb : 0.0f;
    }

    /// Install the band centre frequencies and Qs -- the SHAPE of the five
    /// sections. Always safe to call; it never touches a gain.
    ///
    /// This is deliberately separate from @ref setOrganDefaults. Folding the two
    /// together meant the shapes were installed only when the gains were, and a
    /// host that restored a saved gain before calling prepare() therefore got all
    /// five sections sitting at the BandState fallback of 1 kHz / Q 0.7 -- five
    /// stacked 1 kHz filters, no crash, no warning, and an organ that sounded
    /// wrong for reasons nothing reported.
    void setOrganBandShapes() noexcept;

    /// Install the default pipe-organ gain voicing (gentle, <= +/-2.5 dB).
    /// Gains only; @ref setOrganBandShapes owns the frequencies and Qs.
    void setOrganDefaults() noexcept;

    void process(core::AudioBlock& block) noexcept;
    void reset() noexcept;

private:
    struct BandState
    {
        float freq     = 1000.0f;
        float q        = 0.7f;
        float targetDb = 0.0f;
        float curDb    = 0.0f;
    };

    /// How far a band's smoothed gain may sit from its target and still count as
    /// arrived, in dB. Well below anything audible; it exists so the glide
    /// terminates instead of asymptoting forever.
    static constexpr float kGlideEpsilonDb = 1.0e-4f;

    /// Frames between coefficient redesigns while a gain is travelling.
    static constexpr std::size_t kGlideChunk = 64;

    core::SampleRate sr_       = 48000.0;
    bool             enabled_  = true;
    /// True once anything has voiced the EQ, so a later prepare() (a sample-rate
    /// or block-size change) does not silently reset the user's settings back to
    /// the factory curve mid-session.
    bool             voiced_   = false;
    /// The same protection for the band SHAPES. prepare() installs the organ
    /// frequencies and Qs unconditionally, because leaving them at the BandState
    /// fallback of 1 kHz / Q 0.7 is a silent disaster — but that is only right
    /// while nobody has chosen their own. Without this flag a custom frequency
    /// set through setBand or setParams was reverted by the next sample-rate
    /// change, with the gain that went with it left in place.
    bool             shaped_   = false;
    /// Set once a bypass glide has run to completion and the filter memory has
    /// been cleared, so a disabled EQ goes back to costing nothing.
    bool             bypassSettled_ = false;

    std::array<BandState, kBands> bands_{};
    std::array<Biquad, kBands>    filtL_{};
    std::array<Biquad, kBands>    filtR_{};

    [[nodiscard]] BiquadCoeffs design(std::size_t band, float gainDb) const noexcept;
};

} // namespace caecilia::dsp
