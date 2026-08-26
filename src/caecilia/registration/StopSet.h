// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace caecilia::registration
{

/**
 * @brief An ordered, duplicate-free set of @c core::StopId — the resolved result
 *        of a @c Selector / @c StopQuery and the atom of the registration set
 *        algebra.
 *
 * A StopSet is a pure off-thread value type. It keeps its ids sorted by
 * @c StopId::value and deduplicated so that:
 *   - membership tests are @c O(log n),
 *   - set algebra (union / intersection / difference) is linear and
 *     order-independent, and
 *   - two states that engage the same stops always compare equal, which makes
 *     @c StateDelta diffs deterministic.
 *
 * @note This type allocates (it wraps @c std::vector) and is therefore
 *       explicitly **not** real-time safe. The registration brain runs off the
 *       audio thread; only the fixed-capacity @c StateDelta it emits ever
 *       crosses the audio seam.
 */
class StopSet
{
public:
    using value_type     = core::StopId;
    using const_iterator = std::vector<core::StopId>::const_iterator;

    StopSet() = default;

    /// Construct from an arbitrary (possibly unordered / duplicated) range.
    explicit StopSet(std::span<const core::StopId> ids);

    // --- Observers ----------------------------------------------------------
    [[nodiscard]] bool        empty() const noexcept { return ids_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ids_.size(); }
    [[nodiscard]] bool        contains(core::StopId id) const noexcept;

    /// Read-only view of the sorted ids (stable until the next mutation).
    [[nodiscard]] std::span<const core::StopId> ids() const noexcept { return ids_; }

    [[nodiscard]] const_iterator begin() const noexcept { return ids_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return ids_.end(); }

    // --- Mutators (single element) -----------------------------------------
    /// Insert @p id; no-op if already present. @return true if newly inserted.
    bool insert(core::StopId id);
    /// Remove @p id if present. @return true if it was present.
    bool erase(core::StopId id);
    /// Toggle @p id; @return the new membership state (true == now present).
    bool toggle(core::StopId id);
    void clear() noexcept { ids_.clear(); }

    // --- Set algebra (in place) --------------------------------------------
    StopSet& uniteWith(const StopSet& other);       ///< this |= other
    StopSet& intersectWith(const StopSet& other);   ///< this &= other
    StopSet& subtract(const StopSet& other);        ///< this -= other

    // --- Bit-mask form ------------------------------------------------------
    //
    // A registration small enough to fit in a machine word is a registration the
    // audio thread can read without a lock, a copy or an allocation: one relaxed
    // load per host parameter and a single 64-bit compare against what is already
    // sounding. That is the whole reason the host stop-parameter pool is capped
    // at @ref kMaskCapacity rather than at some rounder, larger number.

    /// Stops representable in a mask. Bit N is @c StopId{N}.
    static constexpr std::size_t kMaskCapacity = 64;

    /**
     * @brief This set as a bit mask, one bit per @c StopId::value.
     * @return The mask. Ids at or beyond @ref kMaskCapacity are DROPPED — the
     *         mask cannot represent them, and silently widening it would break
     *         the audio thread's single-compare contract.
     *
     * RT-safe, @c noexcept: this is the form the audio thread reads.
     */
    [[nodiscard]] std::uint64_t toMask() const noexcept;

    /// The inverse of @ref toMask. Ids come out sorted ascending, as always.
    [[nodiscard]] static StopSet fromMask(std::uint64_t mask);

    /// @return true if every id in this set fits in a mask, i.e. @ref toMask is
    ///         lossless. Off-thread; the guard rail for an organ that outgrows the
    ///         reserved parameter pool.
    [[nodiscard]] bool fitsInMask() const noexcept;

    // --- Comparison ---------------------------------------------------------
    friend bool operator==(const StopSet& a, const StopSet& b) noexcept
    {
        return a.ids_ == b.ids_;
    }
    friend bool operator!=(const StopSet& a, const StopSet& b) noexcept
    {
        return !(a == b);
    }

    /// Strict-weak ordering on @c StopId used everywhere ids are sorted.
    [[nodiscard]] static constexpr bool less(core::StopId a, core::StopId b) noexcept
    {
        return a.value < b.value;
    }

private:
    std::vector<core::StopId> ids_; ///< Sorted ascending by value, unique.
};

// --- Free set-algebra operators (value-returning) --------------------------
[[nodiscard]] StopSet operator|(StopSet a, const StopSet& b); ///< union
[[nodiscard]] StopSet operator&(StopSet a, const StopSet& b); ///< intersection
[[nodiscard]] StopSet operator-(StopSet a, const StopSet& b); ///< difference

} // namespace caecilia::registration
