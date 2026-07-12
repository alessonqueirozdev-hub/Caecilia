/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/Rank.h"
#include "caecilia/model/Stop.h"
#include "caecilia/model/Windchest.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace caecilia::model
{

/**
 * @brief The compiled, immutable instrument: the single description the audio
 *        engine, the registration brain and the data-driven UI all read.
 *
 * An @c Organ owns the whole loaded instrument — its windchests, ranks, stops,
 * divisions, manuals and couplers — as dense, index-addressable arrays. It is
 * produced off the audio thread by @c OrganLoader::compile from an
 * @c OrganDefinition and is thereafter read-only, so its lookups are RT-safe.
 *
 * ## Identity convention
 * Every id (@c RankId, @c StopId, @c DivisionId, @c WindchestId, @c CouplerId)
 * equals the element's index in the corresponding array. That makes @ref rank,
 * @ref stop, @ref division, @ref windchest and @ref coupler O(1),
 * allocation-free lookups safe to call near (though not necessarily on) the
 * audio thread.
 *
 * Aliased as @c OrganSpec for modules that speak in "spec" terms.
 */
class Organ
{
public:
    Organ() = default;

    // --- Metadata -----------------------------------------------------------
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& builder() const noexcept { return builder_; }
    [[nodiscard]] std::int32_t       year() const noexcept { return year_; }

    // --- Bulk access --------------------------------------------------------
    [[nodiscard]] const std::vector<Windchest>& windchests() const noexcept { return windchests_; }
    [[nodiscard]] const std::vector<Rank>&      ranks() const noexcept { return ranks_; }
    [[nodiscard]] const std::vector<Stop>&      stops() const noexcept { return stops_; }
    [[nodiscard]] const std::vector<Division>&  divisions() const noexcept { return divisions_; }
    [[nodiscard]] const std::vector<Manual>&    manuals() const noexcept { return manuals_; }
    [[nodiscard]] const std::vector<Coupler>&   couplers() const noexcept { return couplers_; }

    // --- O(1) id lookups (nullptr if out of range). All RT-safe. ------------
    [[nodiscard]] const Windchest* windchest(core::WindchestId id) const noexcept;
    [[nodiscard]] const Rank*      rank(core::RankId id) const noexcept;
    [[nodiscard]] const Stop*      stop(core::StopId id) const noexcept;
    [[nodiscard]] const Division*  division(core::DivisionId id) const noexcept;
    [[nodiscard]] const Coupler*   coupler(CouplerId id) const noexcept;

    /**
     * @brief The concrete pipe a single drawn stop sounds for a key.
     * @param stopId The engaged stop.
     * @param note   The key pressed (in the stop's division compass).
     * @return The stop's rank's pipe for @p note, or nullptr if the stop/rank is
     *         unknown or the note is outside the rank compass. RT-safe.
     *
     * Note: for a compound (mixture) stop this returns the pipe of the stop's
     * primary rank only; the additional ranks are expanded by the synthesis
     * layer from @c Stop::mixtureComposition. // TODO(phase model): expand mixture
     * ranks here once compound ranks are modelled as separate Rank entries.
     */
    [[nodiscard]] const Pipe* pipeForKey(core::StopId stopId, core::MidiNote note) const noexcept;

    /**
     * @brief Rank -> PipeId activation mapping: the set of pipes a keypress
     *        sounds, given the stops currently engaged in one division.
     * @param division   The division the key belongs to.
     * @param note       The key pressed.
     * @param engaged    Stops the caller has resolved as engaged (couplers must be
     *                   pre-expanded by the registration engine into this set).
     * @param out        Caller-owned output buffer for the activated pipes.
     * @return Number of PipeIds written (clamped to @p out.size()).
     *
     * Allocation-free and @c noexcept: writes into the caller's buffer so it can
     * be called on/near the audio thread. Only stops belonging to @p division
     * whose rank covers @p note contribute a pipe.
     */
    [[nodiscard]] std::size_t collectPipesForKey(core::DivisionId division,
                                                 core::MidiNote note,
                                                 std::span<const core::StopId> engaged,
                                                 std::span<core::PipeId> out) const noexcept;

    // --- Off-thread construction (used by OrganLoader::compile) --------------
    /// @name Builders (NOT real-time safe; called only during load/compile).
    /// @{
    void setName(std::string name) { name_ = std::move(name); }
    void setBuilder(std::string builder) { builder_ = std::move(builder); }
    void setYear(std::int32_t year) noexcept { year_ = year; }
    void setWindchests(std::vector<Windchest> w) { windchests_ = std::move(w); }
    void setRanks(std::vector<Rank> r) { ranks_ = std::move(r); }
    void setStops(std::vector<Stop> s) { stops_ = std::move(s); }
    void setDivisions(std::vector<Division> d) { divisions_ = std::move(d); }
    void setManuals(std::vector<Manual> m) { manuals_ = std::move(m); }
    void setCouplers(std::vector<Coupler> c) { couplers_ = std::move(c); }
    /// @}

private:
    std::string            name_;
    std::string            builder_;
    std::int32_t           year_ = 0;
    std::vector<Windchest> windchests_;
    std::vector<Rank>      ranks_;
    std::vector<Stop>      stops_;
    std::vector<Division>  divisions_;
    std::vector<Manual>    manuals_;
    std::vector<Coupler>   couplers_;
};

/// Spec-vocabulary alias: the immutable, compiled instrument specification.
using OrganSpec = Organ;

} // namespace caecilia::model
