// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <string>
#include <vector>

namespace caecilia::model
{

/**
 * @brief A drawstop: the first-class carrier of semantic registration metadata.
 *
 * A Stop is the single source of the "pillar-5" semantic identity ("Great
 * Principal 8'") intended to feed the tooltip, the screen-reader label, the
 * MIDI-learn target and the OSC address. None of those read it yet: the only
 * caller of @ref displayName is @c RegistrationIntent::captureFrom, and the
 * shipping console builds its own stop list. It couples a rich taxonomy —
 * @c TonalFamily + exact @c Footage + @c PitchClass + @c ChorusRole — to the
 * @c DivisionId it lives in and the @c RankId it controls.
 *
 * The registration brain treats this as immutable *specification*: the Selector
 * algebra (`family:reed & pitch:8 & div:swell`) resolves against exactly these
 * fields. Compound stops (mixtures) additionally list the footages of their
 * constituent ranks in @c mixtureComposition so build-plenum reasons about the
 * crown correctly.
 *
 * Immutable once compiled; aliased as @c StopSpec for spec-speaking modules.
 */
class Stop
{
public:
    Stop() = default;

    // --- Identity & semantic metadata --------------------------------------
    [[nodiscard]] core::StopId      id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] core::TonalFamily family() const noexcept { return family_; }
    [[nodiscard]] core::Footage     footage() const noexcept { return footage_; }
    [[nodiscard]] core::PitchClass  pitchClass() const noexcept { return pitchClass_; }
    [[nodiscard]] core::ChorusRole  role() const noexcept { return role_; }

    // --- Relations ----------------------------------------------------------
    [[nodiscard]] core::DivisionId  division() const noexcept { return division_; }
    [[nodiscard]] core::RankId      rank() const noexcept { return rank_; }

    // --- Compound (mixture) composition ------------------------------------
    /// @return Footages of the constituent ranks; empty for a single-rank stop.
    [[nodiscard]] const std::vector<core::Footage>& mixtureComposition() const noexcept
    {
        return mixtureComposition_;
    }

    /// @return true if this stop draws more than one rank (a mixture/cornet).
    [[nodiscard]] bool isCompound() const noexcept { return !mixtureComposition_.empty(); }

    /// @return true if this stop sounds a non-octave mutation (quint, tierce, ...).
    [[nodiscard]] bool isMutation() const noexcept { return footage_.isMutation(); }

    /**
     * @brief Human-readable semantic identity, e.g. "Principal 8'" or
     *        "Sesquialtera II".
     *
     * Allocates a string; intended for UI/tooltip/accessibility off the audio
     * thread, NOT for the render path.
     */
    [[nodiscard]] std::string displayName() const;

    // --- Off-thread construction (used by OrganLoader::compile) --------------
    /// @name Builders (NOT real-time safe; called only during load/compile).
    /// @{
    void setId(core::StopId id) noexcept { id_ = id; }
    void setName(std::string name) { name_ = std::move(name); }
    void setFamily(core::TonalFamily f) noexcept { family_ = f; }
    void setFootage(core::Footage f) noexcept { footage_ = f; }
    void setPitchClass(core::PitchClass p) noexcept { pitchClass_ = p; }
    void setRole(core::ChorusRole r) noexcept { role_ = r; }
    void setDivision(core::DivisionId d) noexcept { division_ = d; }
    void setRank(core::RankId r) noexcept { rank_ = r; }
    void setMixtureComposition(std::vector<core::Footage> comp) { mixtureComposition_ = std::move(comp); }
    /// @}

private:
    core::StopId               id_{};
    std::string                name_;
    core::TonalFamily          family_     = core::TonalFamily::Undefined;
    core::Footage              footage_    = core::footage::kEight;
    core::PitchClass           pitchClass_ = core::PitchClass::Unison;
    core::ChorusRole           role_       = core::ChorusRole::Undefined;
    core::DivisionId           division_{};
    core::RankId               rank_{};
    std::vector<core::Footage> mixtureComposition_;
};

/// Spec-vocabulary alias: the immutable stop specification.
using StopSpec = Stop;

/**
 * @brief Conventional organ label for a footage, e.g. 8'->"8'", 8/3->"2 2/3'",
 *        8/5->"1 3/5'".
 *
 * Off-thread helper (allocates); shared by @c Stop::displayName and the UI so
 * the whole product spells footages identically.
 */
[[nodiscard]] std::string footageLabel(core::Footage footage);

} // namespace caecilia::model
