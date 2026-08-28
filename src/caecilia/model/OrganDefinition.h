// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/model/Diagnostics.h"
#include "caecilia/model/Division.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace caecilia::model
{

/**
 * @file OrganDefinition.h
 * @brief The mutable, human-readable mirror of an organ file (YAML/JSON).
 *
 * These @c *Def structs map 1:1 onto the on-disk document. Cross-references are
 * by NAME (a rank names its windchest; a stop names its division and rank), so
 * the file stays human-editable; @c OrganLoader::compile resolves those names to
 * dense ids while producing the immutable @c Organ. Enum-valued fields are kept
 * as strings here and parsed by the helpers at the bottom of this header, so an
 * unknown token becomes a diagnostic rather than a silent default.
 *
 * This layer is off-thread only and freely allocates; nothing here is RT-safe.
 */

/// File mirror of a windchest.
struct WindchestDef
{
    std::string name;
    float       pressurePa = 812.0f;
    bool        tremulant  = false;
};

/// File mirror of a rank's voicing block (see @c RankVoicingSpec).
struct VoicingDef
{
    float chiff               = 0.0f;
    float harmonicDevelopment = 0.5f;
    float brightness          = 0.5f;
    float windSensitivity     = 0.5f;
    float detuneScatterCents  = 1.0f;
    float levelScatterDb      = 0.5f;
    float brightnessScatter   = 0.05f;
    float attackScatterMs     = 2.0f;
};

/// File mirror of a sample-set reference.
struct SampleSetDef
{
    std::string   resourceToken;
    double        sourceSampleRate = 48000.0;
    std::int32_t  channels         = 1;
    core::MidiNote baseNote        = 60;
    bool          streaming        = true;
    std::uint64_t loopStartFrame   = 0;
    std::uint64_t loopEndFrame     = 0;
};

/// File mirror of a rank.
struct RankDef
{
    std::string  name;
    std::string  family  = "Principal"; ///< Parsed via @ref tonalFamilyFromString.
    std::string  engine  = "Additive";  ///< Parsed via @ref engineKindFromString.
    std::int32_t footageNum = 8;
    std::int32_t footageDen = 1;
    std::string  windchest;             ///< Name reference into @c OrganDefinition::windchests.
    std::int32_t lowNote  = 36;
    std::int32_t highNote = 96;
    VoicingDef   voicing;
    std::optional<SampleSetDef> sampleSet;
    float        pan         = 0.0f;
    float        distanceM   = 8.0f;
};

/// File mirror of a drawstop.
struct StopDef
{
    std::string  name;
    std::string  family     = "Principal"; ///< Parsed via @ref tonalFamilyFromString.
    std::int32_t footageNum = 8;
    std::int32_t footageDen = 1;
    std::string  pitchClass = "Unison";    ///< Parsed via @ref pitchClassFromString.
    std::string  role       = "Foundation";///< Parsed via @ref chorusRoleFromString.
    std::string  division;                 ///< Name reference into @c divisions.

    /// Reference into @c ranks: a bare rank name, or "windchest/rank" when the
    /// bare name is ambiguous.
    ///
    /// Rank names are NOT unique, because on a real organ they are not: the same
    /// Trompette 8 appears on two manuals as two different ranks of pipes, and
    /// this instrument's own demo organ has five such pairs. Qualifying by the
    /// windchest is what tells them apart, and it is only needed where the bare
    /// name would be ambiguous -- most of a document uses bare names.
    std::string  rank;
    /// Constituent footages for a compound stop; empty for a single-rank stop.
    std::vector<std::pair<std::int32_t, std::int32_t>> mixture;
};

/// File mirror of a coupler.
struct CouplerDef
{
    std::string  name;
    std::string  from;                 ///< Source division name.
    std::string  to;                   ///< Destination division name.
    std::int32_t octaveShift = 0;      ///< Semitone shift (+12 super, -12 sub).
    std::string  kind = "InterManual"; ///< Parsed via @ref couplerKindFromString.
};

/// File mirror of a division (and its optional manual keyboard binding).
struct DivisionDef
{
    std::string  name;
    std::string  kind        = "Manual"; ///< Parsed via @ref divisionKindFromString.
    std::int32_t lowNote     = 36;
    std::int32_t highNote    = 96;
    bool         enclosed    = false;
    bool         tremulant   = false;
    std::int32_t manualIndex = 0;        ///< Console stacking order (manual divisions).
    std::int32_t midiChannel = -1;       ///< 0-based MIDI channel, or -1 if unassigned.
};

/**
 * @brief The complete parsed organ document, before compilation.
 *
 * Populate it by parsing an organ file (see @c OrganLoader) or by hand for a
 * flat test organ, then call @c OrganLoader::compile to obtain an immutable
 * @c Organ. @ref validate performs cheap structural checks (naming, references,
 * compass sanity) without building the full spec.
 */
struct OrganDefinition
{
    std::string               name;
    std::string               builder;
    std::int32_t              year = 0;
    std::vector<WindchestDef> windchests;
    std::vector<RankDef>      ranks;
    std::vector<StopDef>      stops;
    std::vector<DivisionDef>  divisions;
    std::vector<CouplerDef>   couplers;

    /**
     * @brief Structural self-check: names present, references resolvable,
     *        compasses ordered.
     * @return Accumulated diagnostics (empty if clean). Does NOT build a spec.
     */
    [[nodiscard]] LoadDiagnostics validate() const;
};

// ---------------------------------------------------------------------------
// Semantic token <-> enum helpers. Case-insensitive on input; std::nullopt for
// an unrecognised token so the caller can raise a precise diagnostic.
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<core::TonalFamily> tonalFamilyFromString(std::string_view s);
[[nodiscard]] std::optional<core::EngineKind>  engineKindFromString(std::string_view s);
[[nodiscard]] std::optional<core::ChorusRole>  chorusRoleFromString(std::string_view s);
[[nodiscard]] std::optional<core::PitchClass>  pitchClassFromString(std::string_view s);
[[nodiscard]] std::optional<DivisionKind>      divisionKindFromString(std::string_view s);
[[nodiscard]] std::optional<CouplerKind>       couplerKindFromString(std::string_view s);

/**
 * @brief Resolve a rank reference against a definition's ranks.
 * @param ranks     The definition's ranks.
 * @param reference A bare rank name, or "windchest/rank".
 * @return The index into @p ranks, or nullopt when nothing matches or more than
 *         one does. The two failures are different and the caller should say so.
 */
[[nodiscard]] std::optional<std::size_t> findRank(const std::vector<RankDef>& ranks,
                                                  std::string_view reference);

/// @return How many ranks a bare-or-qualified reference matches.
[[nodiscard]] std::size_t countRankMatches(const std::vector<RankDef>& ranks,
                                           std::string_view reference);

/**
 * @brief The shortest reference that names @p index unambiguously.
 *
 * The bare name when it is unique, "windchest/rank" when it is not. What an
 * exporter should write, and what keeps a document readable.
 */
[[nodiscard]] std::string rankReference(const std::vector<RankDef>& ranks,
                                        std::size_t index);

[[nodiscard]] std::string toString(core::TonalFamily f);
[[nodiscard]] std::string toString(core::EngineKind e);
[[nodiscard]] std::string toString(core::ChorusRole r);
[[nodiscard]] std::string toString(core::PitchClass p);
[[nodiscard]] std::string toString(DivisionKind k);
[[nodiscard]] std::string toString(CouplerKind k);

} // namespace caecilia::model
