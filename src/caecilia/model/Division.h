// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/IWindSupply.h"

#include <cstdint>
#include <string>
#include <vector>

namespace caecilia::model
{

/// The organ-console role a division plays.
enum class DivisionKind : std::uint8_t
{
    Manual,   ///< Played by a hand keyboard (Great, Swell, Positive, ...).
    Pedal,    ///< Played by the pedalboard.
    Floating  ///< Not fixed to one keyboard; couplable onto others (Echo, Solo).
};

/**
 * @brief A division: the grouping of stops under one console keyboard section
 *        (Great, Swell, Pedal, ...), together with its expression + wind.
 *
 * A division owns the list of @c StopId it presents on its jamb, a note compass,
 * whether it is @c enclosed in a swell box (where an expression pedal would
 * apply) and whether it carries its own tremulant. It is pure spec; aliased as
 * @c DivisionSpec.
 *
 * @todo Outside this module only the compass is read (the plugin uses it to
 * bound each mapped MIDI channel). @ref isEnclosed, @ref hasTremulant, @ref stops
 * and @ref windchests have no callers: there is no swell box and no per-division
 * tremulant on the render path.
 */
class Division
{
public:
    Division() = default;

    // --- Identity & semantics ----------------------------------------------
    [[nodiscard]] core::DivisionId  id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] DivisionKind      kind() const noexcept { return kind_; }
    [[nodiscard]] bool              isEnclosed() const noexcept { return enclosed_; }
    [[nodiscard]] bool              hasTremulant() const noexcept { return hasTremulant_; }

    // --- Compass ------------------------------------------------------------
    [[nodiscard]] core::MidiNote lowNote() const noexcept { return lowNote_; }
    [[nodiscard]] core::MidiNote highNote() const noexcept { return highNote_; }

    /// @return true if @p note is within this division's keyboard compass. RT-safe.
    [[nodiscard]] bool contains(core::MidiNote note) const noexcept
    {
        return note >= lowNote_ && note <= highNote_;
    }

    // --- Stops & wind -------------------------------------------------------
    [[nodiscard]] const std::vector<core::StopId>&      stops() const noexcept { return stops_; }
    [[nodiscard]] std::size_t                           stopCount() const noexcept { return stops_.size(); }
    [[nodiscard]] const std::vector<core::WindchestId>& windchests() const noexcept { return windchests_; }

    // --- Off-thread construction (used by OrganLoader::compile) --------------
    /// @name Builders (NOT real-time safe; called only during load/compile).
    /// @{
    void setId(core::DivisionId id) noexcept { id_ = id; }
    void setName(std::string name) { name_ = std::move(name); }
    void setKind(DivisionKind k) noexcept { kind_ = k; }
    void setEnclosed(bool enclosed) noexcept { enclosed_ = enclosed; }
    void setHasTremulant(bool tremulant) noexcept { hasTremulant_ = tremulant; }
    void setCompass(core::MidiNote low, core::MidiNote high) noexcept;
    void addStop(core::StopId stop) { stops_.push_back(stop); }
    void addWindchest(core::WindchestId chest) { windchests_.push_back(chest); }
    /// @}

private:
    core::DivisionId               id_{};
    std::string                    name_;
    DivisionKind                   kind_        = DivisionKind::Manual;
    bool                           enclosed_    = false;
    bool                           hasTremulant_ = false;
    core::MidiNote                 lowNote_     = 36;
    core::MidiNote                 highNote_    = 96;
    std::vector<core::StopId>      stops_;
    std::vector<core::WindchestId> windchests_;
};

/// Spec-vocabulary alias: the immutable division specification.
using DivisionSpec = Division;

/**
 * @brief The physical keyboard binding for a manual division.
 *
 * A @c Division is the *tonal* grouping; a @c Manual is the *console* keyboard
 * that plays it, carrying the MIDI channel and stacking order a router would
 * need. Splitting them is what would let a floating division be played from more
 * than one manual via couplers without duplicating its stops.
 *
 * @todo Nothing reads @c Manual: @c Organ::manuals() has no callers, and the
 * plugin derives its channel -> division map from @c Organ::divisions() order
 * rather than from @c midiChannel. Couplers are not applied either.
 */
struct Manual
{
    core::DivisionId division{};       ///< The division this keyboard plays.
    std::int32_t     manualIndex = 0;  ///< Stacking order from the bottom (0 = lowest).
    std::int32_t     midiChannel = -1; ///< 0-based MIDI channel, or -1 if unassigned.
    core::MidiNote   lowNote     = 36; ///< Lowest physical key.
    core::MidiNote   highNote    = 96; ///< Highest physical key.

    /// @return true if @p note is within the physical keyboard. RT-safe.
    [[nodiscard]] bool contains(core::MidiNote note) const noexcept
    {
        return note >= lowNote && note <= highNote;
    }
};

} // namespace caecilia::model
