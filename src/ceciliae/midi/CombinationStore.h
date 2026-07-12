/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/midi/MidiTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ceciliae::midi
{

/// How a stored combination expresses its registration.
enum class CombinationKind : std::uint8_t
{
    Empty,    ///< No combination captured in this slot.
    Snapshot, ///< A literal engaged-stop set, resolved to a StateDelta handle.
    Intent    ///< A portable semantic selector expression (organ-independent).
};

/**
 * @brief One captured combination (piston memory).
 *
 * A @ref CombinationKind::Snapshot references an off-thread-published
 * @c StateDelta by @ref stateDeltaId — the literal stops to draw. A
 * @ref CombinationKind::Intent instead stores a portable @ref SelectorExpr the
 * registration engine re-resolves against whatever organ is loaded (the
 * "portability" win), so the same piston can be carried between instruments.
 */
struct Combination
{
    CombinationKind kind         = CombinationKind::Empty;
    std::uint32_t   stateDeltaId = 0;  ///< Resolved StateDelta handle (Snapshot).
    SelectorExpr    intent{};          ///< Portable selector text (Intent).

    /// @return true if the slot holds a captured combination.
    [[nodiscard]] constexpr bool isSet() const noexcept
    {
        return kind != CombinationKind::Empty;
    }

    friend bool operator==(const Combination&, const Combination&) noexcept = default;
};

/**
 * @brief Piston memory: general and per-division divisional combinations.
 *
 * Generals apply to the whole instrument; divisionals are scoped to one manual.
 * MIDI program-change (PC->generals) and learned piston controls recall these;
 * the sequencer's steps are typically references to stored generals.
 *
 * ## Real-time contract
 * Capturing (writing) a combination is OFF-thread work (it snapshots live state
 * into a published StateDelta first). Recall is a @c noexcept lookup returning
 * the stored @ref Combination, which the registration bridge turns into an
 * @c ApplyStateDelta engine command. Fixed-capacity: never allocates.
 */
class CombinationStore
{
public:
    static constexpr std::size_t kNumGenerals              = 64;
    static constexpr std::size_t kNumDivisions             = 16;
    static constexpr std::size_t kNumDivisionalsPerDivision = 32;

    CombinationStore() noexcept = default;

    // --- Off-thread capture (NOT real-time safe) ----------------------------

    /// Store @p combo as general @p index (no-op if out of range).
    void captureGeneral(std::size_t index, const Combination& combo) noexcept;

    /// Store @p combo as divisional @p index within @p division (no-op if OOR).
    void captureDivisional(core::DivisionId division, std::size_t index,
                           const Combination& combo) noexcept;

    /// Clear general @p index. NOT real-time safe.
    void clearGeneral(std::size_t index) noexcept;

    /// Clear divisional @p index within @p division. NOT real-time safe.
    void clearDivisional(core::DivisionId division, std::size_t index) noexcept;

    /// Clear every stored combination. NOT real-time safe.
    void clearAll() noexcept;

    // --- Recall (real-time-safe) --------------------------------------------

    /// @return General @p index, or an @c Empty combination if out of range.
    [[nodiscard]] Combination recallGeneral(std::size_t index) const noexcept;

    /// @return Divisional @p index in @p division, or @c Empty if out of range.
    [[nodiscard]] Combination recallDivisional(core::DivisionId division,
                                               std::size_t index) const noexcept;

    // --- Queries ------------------------------------------------------------

    [[nodiscard]] static constexpr std::size_t generalCapacity() noexcept { return kNumGenerals; }
    [[nodiscard]] static constexpr std::size_t divisionalCapacity() noexcept
    {
        return kNumDivisionalsPerDivision;
    }

private:
    [[nodiscard]] static bool divisionInRange(core::DivisionId division) noexcept
    {
        return division.value < kNumDivisions;
    }

    std::array<Combination, kNumGenerals> generals_{};
    std::array<std::array<Combination, kNumDivisionalsPerDivision>, kNumDivisions> divisionals_{};
};

} // namespace ceciliae::midi
