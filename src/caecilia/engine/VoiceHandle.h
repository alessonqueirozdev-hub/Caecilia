// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <cstdint>

namespace caecilia::core::engine
{

/**
 * @brief A generation-tagged reference to a slot in the VoicePool.
 *
 * A raw index alone is unsafe: after a voice is stolen and its slot recycled,
 * an old holder of that index would silently address a different note. Pairing
 * the index with a monotonically-bumped @ref generation lets @c VoicePool
 * detect and reject stale references — @c resolve returns @c nullptr once the
 * slot's generation has moved on.
 *
 * A generation of 0 is the reserved "never issued" sentinel, so a
 * value-initialised handle is always invalid.
 *
 * Trivially copyable POD; safe to pass by value on the audio thread.
 */
struct VoiceHandle
{
    /// Slot index into the pool, or @ref kInvalidIndex for an invalid handle.
    std::uint16_t index = kInvalidIndex;

    /// Occupancy generation of the slot at the moment this handle was issued.
    std::uint16_t generation = 0;

    /// Sentinel index meaning "no slot".
    static constexpr std::uint16_t kInvalidIndex = 0xFFFFu;

    /// @return true if this handle could refer to a live slot (generation != 0).
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return generation != 0 && index != kInvalidIndex;
    }

    /// @return The canonical invalid handle.
    [[nodiscard]] static constexpr VoiceHandle invalid() noexcept
    {
        return VoiceHandle{kInvalidIndex, 0};
    }

    friend constexpr bool operator==(VoiceHandle, VoiceHandle) noexcept = default;
};

} // namespace caecilia::core::engine
