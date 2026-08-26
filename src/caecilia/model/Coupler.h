// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstdint>
#include <string>

namespace caecilia::model
{

/// Identifies a coupler within a loaded organ (dense index).
struct CouplerId
{
    std::uint16_t value = 0;
    friend constexpr bool operator==(CouplerId, CouplerId) noexcept = default;
};

/// The kind of coupling relationship a coupler expresses.
enum class CouplerKind : std::uint8_t
{
    InterManual, ///< Couples one division's keys to another (e.g. Swell -> Great).
    IntraManual, ///< Octave/sub-octave within the SAME division (from == to).
    Melody,      ///< Couples only the topmost sounding key (melody coupler).
    Bass         ///< Couples only the lowest sounding key (bass coupler).
};

/**
 * @brief A coupler: routes keypresses from a source division to a destination
 *        division, optionally transposed by whole octaves.
 *
 * A coupler DESCRIBES the intent that pressing a key on the destination manual
 * also sounds the source division's engaged stops (shifted by @c octaveShift).
 * A unison coupler has @c octaveShift == 0; a super-octave is +12, a sub-octave
 * is -12. Intra-manual octave couplers set @c from == @c to.
 *
 * Applied by the ENGINE, in the same fan-out that turns one key into one voice
 * per drawn rank: @c AudioEngine::expandKey adds the ranks of every division a
 * drawn coupler lends this one. That keeps the producer dumb -- a key press is one
 * command whatever is drawn -- and is why a coupler costs no extra MIDI traffic.
 *
 * Couplers do NOT chain. This organ declares Récit/Pédale separately from
 * Récit/Grand-Orgue plus Grand-Orgue/Pédale, which is what an instrument does when
 * the two are not the same thing.
 *
 * @todo @c RegistrationState::engagedCouplers() still has no readers: the plugin
 *       holds the drawn set as its own mask over the host's coupler parameters,
 *       the same way it holds the registration, and the semantic registration
 *       state is not yet the thing the audio path is built from.
 * Aliased as @c CouplerSpec.
 */
class Coupler
{
public:
    Coupler() = default;

    [[nodiscard]] CouplerId        id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] core::DivisionId from() const noexcept { return from_; }
    [[nodiscard]] core::DivisionId to() const noexcept { return to_; }
    [[nodiscard]] std::int32_t     octaveShiftSemitones() const noexcept { return octaveShift_; }
    [[nodiscard]] CouplerKind      kind() const noexcept { return kind_; }

    /// @return true if this coupler applies no transposition. RT-safe.
    [[nodiscard]] bool isUnison() const noexcept { return octaveShift_ == 0; }

    /**
     * @brief Transpose a source key by this coupler's octave shift, clamped to
     *        the MIDI range so a shifted key never wraps.
     * @return The destination note, clamped to [0, 127]. RT-safe.
     */
    [[nodiscard]] core::MidiNote mapNote(core::MidiNote note) const noexcept
    {
        const std::int32_t shifted = static_cast<std::int32_t>(note) + octaveShift_;
        if (shifted < 0)   return 0;
        if (shifted > 127) return 127;
        return static_cast<core::MidiNote>(shifted);
    }

    // --- Off-thread construction (used by OrganLoader::compile) --------------
    /// @name Builders (NOT real-time safe; called only during load/compile).
    /// @{
    void setId(CouplerId id) noexcept { id_ = id; }
    void setName(std::string name) { name_ = std::move(name); }
    void setFrom(core::DivisionId d) noexcept { from_ = d; }
    void setTo(core::DivisionId d) noexcept { to_ = d; }
    void setOctaveShift(std::int32_t semitones) noexcept { octaveShift_ = semitones; }
    void setKind(CouplerKind k) noexcept { kind_ = k; }
    /// @}

private:
    CouplerId       id_{};
    std::string     name_;
    core::DivisionId from_{};
    core::DivisionId to_{};
    std::int32_t    octaveShift_ = 0;
    CouplerKind     kind_ = CouplerKind::InterManual;
};

/// Spec-vocabulary alias: the immutable coupler specification.
using CouplerSpec = Coupler;

} // namespace caecilia::model
