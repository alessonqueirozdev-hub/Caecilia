/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/midi/MidiTypes.h"
#include "caecilia/midi/RegistrationCommandTemplate.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace caecilia::midi
{

/**
 * @brief Maps MIDI Program Change to a registration recall.
 *
 * The user's real workflow is PC -> Generals: sending program @c p recalls
 * general combination @c p (offset by @ref generalBase). A channel bound to a
 * division instead recalls that division's divisional @c p, so a controller that
 * emits PC per manual drives divisionals directly.
 *
 * Edits happen off the audio thread; @ref resolve is a @c noexcept pure lookup
 * the router calls while rendering. Trivially copyable.
 */
class ProgramChangeMap
{
public:
    static constexpr std::size_t kNumChannels = 16;

    /// How a program-change on a given channel is interpreted.
    enum class Mode : std::uint8_t
    {
        Ignore,           ///< Drop the program change.
        RecallGeneral,    ///< Recall a general combination.
        RecallDivisional  ///< Recall a divisional within @c division.
    };

    ProgramChangeMap() noexcept = default;

    // --- Off-thread configuration (NOT real-time safe) ----------------------

    /// Set the global default interpretation applied to unconfigured channels.
    void setDefaultMode(Mode mode) noexcept { defaultMode_ = mode; }

    /// Bias applied to the program number when recalling a general (0-based).
    void setGeneralBase(std::uint16_t base) noexcept { generalBase_ = base; }

    /// Bind @p channel to recall divisionals in @p division for its program changes.
    void mapChannelToDivision(MidiChannel channel, core::DivisionId division) noexcept
    {
        if (channel >= kNumChannels)
            return;
        auto& e    = channels_[channel];
        e.active   = true;
        e.mode     = Mode::RecallDivisional;
        e.division = division;
    }

    /// Clear any per-channel override for @p channel (falls back to the default).
    void clearChannel(MidiChannel channel) noexcept
    {
        if (channel < kNumChannels)
            channels_[channel] = ChannelRule{};
    }

    // --- Real-time-safe query -----------------------------------------------

    /**
     * @brief Resolve a program change into a registration recall template.
     * @param channel Source channel.
     * @param program Program number [0, 127].
     * @return A recall template, or a no-op template when the channel is set to
     *         @ref Mode::Ignore.
     */
    [[nodiscard]] RegistrationCommandTemplate resolve(MidiChannel channel,
                                                      std::uint8_t program) const noexcept
    {
        Mode mode = defaultMode_;
        core::DivisionId division{};
        if (channel < kNumChannels && channels_[channel].active)
        {
            mode     = channels_[channel].mode;
            division = channels_[channel].division;
        }

        switch (mode)
        {
            case Mode::RecallGeneral:
                return RegistrationCommandTemplate::recallGeneral(
                    static_cast<std::uint16_t>(program + generalBase_));
            case Mode::RecallDivisional:
                return RegistrationCommandTemplate::recallDivisional(division, program);
            case Mode::Ignore:
            default:
                return RegistrationCommandTemplate{};
        }
    }

private:
    struct ChannelRule
    {
        bool             active = false;
        Mode             mode   = Mode::RecallGeneral;
        core::DivisionId division{};
    };

    Mode          defaultMode_ = Mode::RecallGeneral;
    std::uint16_t generalBase_ = 0;
    std::array<ChannelRule, kNumChannels> channels_{};
};

} // namespace caecilia::midi
