// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/registration/RegistrationState.h"

#include <array>
#include <cstddef>
#include <type_traits>

namespace caecilia::registration
{

/**
 * @brief The atomic, fixed-capacity difference between two registration states —
 *        the ONLY registration object that crosses the audio seam.
 *
 * The registration brain runs entirely off the audio thread on allocating value
 * types (@c StopSet, @c RegistrationState). Every @c apply() / @c undo() reduces
 * to a StateDelta, which is deliberately a **trivially copyable POD**: no
 * pointers, no @c std::vector, just fixed-capacity arrays and counts. That lets
 * the engine drop it straight into the SPSC command ring and the audio thread
 * apply it as a click-free, wind-modelled stop crossfade without ever touching
 * the heap or a lock.
 *
 * If a single transition would change more stops/couplers than a delta can hold,
 * @ref truncated is set; the caller should split the transition into several
 * deltas (a fresh recompute against the partially-applied state). This never
 * happens for ordinary draws and is only reachable by a full-organ recall.
 *
 * @todo Nothing pushes a StateDelta across the seam yet, and the engine's
 *       @c ApplyStateDelta command is an empty case, so the crossfade described
 *       above is the design rather than current behaviour.
 */
struct StateDelta
{
    /// Max stop changes carried in one delta (per direction).
    static constexpr std::size_t kMaxStopChanges    = 128;
    /// Max coupler changes carried in one delta (per direction).
    static constexpr std::size_t kMaxCouplerChanges = 32;

    std::array<core::StopId, kMaxStopChanges> stopsToEngage{};
    std::size_t                               numEngage = 0;

    std::array<core::StopId, kMaxStopChanges> stopsToDisengage{};
    std::size_t                               numDisengage = 0;

    std::array<model::CouplerId, kMaxCouplerChanges> couplersToEngage{};
    std::size_t                                      numCouplerEngage = 0;

    std::array<model::CouplerId, kMaxCouplerChanges> couplersToDisengage{};
    std::size_t                                      numCouplerDisengage = 0;

    /// Suggested crossfade length for the engine's click-free stop transition.
    float crossfadeMs = 12.0f;

    /// Set if the transition overflowed the fixed capacity (see class docs).
    bool truncated = false;

    /// @return true if the delta changes nothing.
    [[nodiscard]] bool isEmpty() const noexcept
    {
        return numEngage == 0 && numDisengage == 0
               && numCouplerEngage == 0 && numCouplerDisengage == 0;
    }
};

static_assert(std::is_trivially_copyable_v<StateDelta>,
              "StateDelta must be trivially copyable to cross the SPSC ring RT-safely.");

/**
 * @brief Compute the delta that turns @p from into @p to.
 * @param from The currently-applied engine state.
 * @param to   The desired state the brain has derived.
 * @return A StateDelta listing the stops/couplers to engage and disengage.
 *
 * Off the audio thread only (reads allocating sets). Overflow past a delta's
 * fixed capacity sets @c StateDelta::truncated.
 */
[[nodiscard]] StateDelta computeDelta(const RegistrationState& from, const RegistrationState& to);

} // namespace caecilia::registration
