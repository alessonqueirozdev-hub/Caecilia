// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file ParameterIds.h
 * @brief The host-visible parameter contract, in pure core.
 *
 * A parameter ID is the most permanent string a plugin owns. Hosts key automation
 * lanes and saved sessions on it, so changing one silently breaks every project a
 * user has ever saved — there is no migration, because the host simply does not
 * find the parameter any more and drops the lane.
 *
 * They lived in @c plugin::ParameterLayout, behind a JUCE include, where nothing
 * could check them without a host framework. Here they are ordinary constants a
 * test can enumerate, hash and pin — see @c ParameterIdHashTest, which asserts the
 * exact published count and the hashes of the IDs at the edges of the set, so a
 * rename fails the build rather than a user's project.
 *
 * @c plugin::ParameterLayout now aliases these rather than restating them. Two
 * copies of a permanent contract is one copy too many.
 */
namespace caecilia::core::params
{

// --- reserved capacities ----------------------------------------------------

/// Boolean stop parameters, one per possible @c StopId.
///
/// Exactly @c registration::StopSet::kMaskCapacity: a registration that fits in a
/// machine word is one the audio thread reads with a single 64-bit compare. Not
/// one slot more — every slot past the instrument's real stop count is a control a
/// host displays, stores automation for, and can never make audible.
inline constexpr std::size_t kMaxStopParameters = 64;

// --- global parameter IDs ---------------------------------------------------

inline constexpr const char* kMasterGainDb     = "master_gain_db";
inline constexpr const char* kReverbMix        = "reverb_mix";
inline constexpr const char* kReverbDecaySec   = "reverb_decay_s";
inline constexpr const char* kReverbPreDelayMs = "reverb_predelay_ms";
inline constexpr const char* kReverbDampingHz  = "reverb_damping_hz";
inline constexpr const char* kReverbWidth      = "reverb_width";
inline constexpr const char* kTremulantOn      = "tremulant_on";
inline constexpr const char* kTremulantRateHz  = "tremulant_rate_hz";
inline constexpr const char* kTremulantDepth   = "tremulant_depth";
inline constexpr const char* kTemperament      = "temperament";
inline constexpr const char* kTuningA4Hz       = "tuning_a4_hz";

// Master EQ: five band gains plus an enable, index-aligned with dsp::MasterEq::Band
// so a loop walks them without a translation table.
inline constexpr const char* kEqOn         = "eq_on";
inline constexpr const char* kEqWarmthDb   = "eq_warmth_db";
inline constexpr const char* kEqBoxinessDb = "eq_boxiness_db";
inline constexpr const char* kEqBodyDb     = "eq_body_db";
inline constexpr const char* kEqPresenceDb = "eq_presence_db";
inline constexpr const char* kEqAirDb      = "eq_air_db";

/// The five band-gain IDs in @c dsp::MasterEq::Band order.
inline constexpr std::array<const char*, 5> kEqBandIds{
    kEqWarmthDb, kEqBoxinessDb, kEqBodyDb, kEqPresenceDb, kEqAirDb
};

/// Every non-stop parameter, in published order.
///
/// The EQ sits INSIDE the globals rather than appended after the stop pool. It was
/// appended, with the reasoning that existing ordinals would not move — but that
/// argument did not survive shrinking the pool from 256 to 64, which moves an
/// appended block regardless. Putting it here means the EQ ordinals never move
/// again, and it cost nothing at the time because the whole pool was inert.
inline constexpr std::array<const char*, 17> kGlobalIds{
    kMasterGainDb, kReverbMix, kReverbDecaySec, kReverbPreDelayMs,
    kReverbDampingHz, kReverbWidth, kTremulantOn, kTremulantRateHz,
    kTremulantDepth, kTemperament, kTuningA4Hz,
    kEqOn, kEqWarmthDb, kEqBoxinessDb, kEqBodyDb, kEqPresenceDb, kEqAirDb
};

/// Total parameters the host sees: 17 globals + 64 stops.
inline constexpr std::size_t kParameterCount = kGlobalIds.size() + kMaxStopParameters;

// --- stop-pool IDs ----------------------------------------------------------

/// One stop parameter's ID, as storage rather than a pointer to storage.
///
/// Returned by value so @ref stopParamId can be @c constexpr and allocation-free:
/// the old implementation returned a @c std::string built by @c snprintf, which is
/// neither, and which therefore could not be used to enumerate the contract at
/// compile time.
struct StopParamId
{
    std::array<char, 9> text{}; ///< "stop_063" and a NUL.

    [[nodiscard]] constexpr const char*      c_str() const noexcept { return text.data(); }
    [[nodiscard]] constexpr std::string_view view()  const noexcept
    {
        return std::string_view(text.data(), 8);
    }
};

/**
 * @brief Deterministic parameter ID for the @p index-th reserved stop slot.
 * @param index Slot number.
 * @return A stable ID such as @c "stop_007".
 *
 * Three digits, and the index is taken modulo 1000 — which reproduces exactly what
 * @c snprintf("stop_%03zu") produced for every index below 1000, and the pool is 64.
 * Above 1000 the two would differ, and the modulo is the safer of the two: it
 * cannot overrun the buffer.
 */
[[nodiscard]] constexpr StopParamId stopParamId(std::size_t index) noexcept
{
    const std::size_t n = index % 1000;

    StopParamId out{};
    out.text[0] = 's';
    out.text[1] = 't';
    out.text[2] = 'o';
    out.text[3] = 'p';
    out.text[4] = '_';
    out.text[5] = static_cast<char>('0' + (n / 100) % 10);
    out.text[6] = static_cast<char>('0' + (n / 10) % 10);
    out.text[7] = static_cast<char>('0' + n % 10);
    out.text[8] = '\0';
    return out;
}

// --- collision detection ----------------------------------------------------

/**
 * @brief The classic `31 * h + byte` string hash, over a parameter ID.
 *
 * Not a security hash and not a fast one: it exists so a test can pin a handful of
 * IDs to a NUMBER. A pinned hash fails on a rename that a "does the set still have
 * 81 entries" check would sail straight past — swap two IDs' spellings and the
 * count is unchanged while every host session breaks.
 */
[[nodiscard]] constexpr std::uint32_t parameterIdHash(std::string_view id) noexcept
{
    std::uint32_t h = 0;
    for (const char c : id)
        h = 31u * h + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    return h;
}

} // namespace caecilia::core::params
