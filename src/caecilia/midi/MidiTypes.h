/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace caecilia::midi
{

// ---------------------------------------------------------------------------
// Shared, trivially-copyable value types for the MIDI module.
//
// Everything here is deliberately POD-ish so it can be relocated by value
// through the engine's SPSC command ring and stored in fixed-capacity binding
// tables that the audio thread reads without locking or allocating.
// ---------------------------------------------------------------------------

/// A raw MIDI channel in [0, 15]. 0xFF is the "any channel" wildcard.
using MidiChannel = std::uint8_t;

/// Sentinel meaning "match on any MIDI channel".
inline constexpr MidiChannel kAnyChannel = 0xFF;

/// Direction a sequencer step request moves the pointer.
enum class SequencerDirection : std::uint8_t
{
    None,      ///< Not a navigation control.
    Previous,  ///< Step one combination back.
    Next,      ///< Step one combination forward.
    First,     ///< Jump to the first step.
    Last       ///< Jump to the last step.
};

/**
 * @brief A fixed-capacity, allocation-free selector expression string.
 *
 * Holds the SAME textual selector grammar the UI omnibar, OSC and JSON-RPC use
 * (e.g. `family:reed & pitch:8 & div:swell`). A MIDI-learned control therefore
 * carries the identical semantic identity as every other control surface; the
 * registration module parses this text later with its shared @c SelectorParser.
 *
 * Storing the expression inline (rather than a @c std::string) keeps
 * @ref SelectorExpr trivially copyable so bindings can travel through the SPSC
 * ring and live in RT-read binding tables.
 */
struct SelectorExpr
{
    static constexpr std::size_t kCapacity = 96; ///< Max stored characters.

    std::array<char, kCapacity> chars{}; ///< UTF-8 bytes; not NUL-terminated by contract.
    std::uint8_t length = 0;             ///< Bytes in use, <= kCapacity.

    /// @return true if no expression is set.
    [[nodiscard]] constexpr bool empty() const noexcept { return length == 0; }

    /// @return A non-owning view of the stored expression (RT-safe, no alloc).
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(chars.data(), length);
    }

    /**
     * @brief Overwrite the expression from a view. Truncates past @c kCapacity.
     *
     * Off-thread only (called while editing bindings), though it neither
     * allocates nor throws. Content is copied byte-for-byte.
     */
    void assign(std::string_view text) noexcept
    {
        const std::size_t n = text.size() < kCapacity ? text.size() : kCapacity;
        for (std::size_t i = 0; i < n; ++i)
            chars[i] = text[i];
        length = static_cast<std::uint8_t>(n);
    }

    /// Convenience factory mirroring @ref assign.
    [[nodiscard]] static SelectorExpr from(std::string_view text) noexcept
    {
        SelectorExpr e;
        e.assign(text);
        return e;
    }

    friend bool operator==(const SelectorExpr&, const SelectorExpr&) noexcept = default;
};

} // namespace caecilia::midi
