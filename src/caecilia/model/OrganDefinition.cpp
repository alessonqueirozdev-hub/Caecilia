// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/OrganDefinition.h"

#include "caecilia/core/ParameterIds.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace caecilia::model
{
namespace
{

/// Lower-case a token for case-insensitive enum matching.
std::string toLower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Token -> enum
// ---------------------------------------------------------------------------

std::optional<core::TonalFamily> tonalFamilyFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "principal" || t == "diapason") return core::TonalFamily::Principal;
    if (t == "flute")                        return core::TonalFamily::Flute;
    if (t == "string")                       return core::TonalFamily::String;
    if (t == "reed")                         return core::TonalFamily::Reed;
    if (t == "mixture")                      return core::TonalFamily::Mixture;
    if (t == "hybrid")                       return core::TonalFamily::Hybrid;
    if (t == "percussion")                   return core::TonalFamily::Percussion;
    if (t == "undefined")                    return core::TonalFamily::Undefined;
    return std::nullopt;
}

std::optional<core::EngineKind> engineKindFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "sample")    return core::EngineKind::Sample;
    if (t == "additive")  return core::EngineKind::Additive;
    if (t == "waveguide") return core::EngineKind::Waveguide;
    if (t == "modal")     return core::EngineKind::Modal;
    return std::nullopt;
}

std::optional<core::ChorusRole> chorusRoleFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "foundation")   return core::ChorusRole::Foundation;
    if (t == "chorus")       return core::ChorusRole::Chorus;
    if (t == "mixturecrown") return core::ChorusRole::MixtureCrown;
    if (t == "solo")         return core::ChorusRole::Solo;
    if (t == "color")        return core::ChorusRole::Color;
    if (t == "effect")       return core::ChorusRole::Effect;
    if (t == "undefined")    return core::ChorusRole::Undefined;
    return std::nullopt;
}

std::optional<core::PitchClass> pitchClassFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "sub")      return core::PitchClass::Sub;
    if (t == "unison")   return core::PitchClass::Unison;
    if (t == "octave")   return core::PitchClass::Octave;
    if (t == "super")    return core::PitchClass::Super;
    if (t == "mutation") return core::PitchClass::Mutation;
    if (t == "compound") return core::PitchClass::Compound;
    return std::nullopt;
}

std::optional<DivisionKind> divisionKindFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "manual")   return DivisionKind::Manual;
    if (t == "pedal")    return DivisionKind::Pedal;
    if (t == "floating") return DivisionKind::Floating;
    return std::nullopt;
}

std::optional<CouplerKind> couplerKindFromString(std::string_view s)
{
    const std::string t = toLower(s);
    if (t == "intermanual") return CouplerKind::InterManual;
    if (t == "intramanual") return CouplerKind::IntraManual;
    if (t == "melody")      return CouplerKind::Melody;
    if (t == "bass")        return CouplerKind::Bass;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// enum -> token (canonical spelling)
// ---------------------------------------------------------------------------

std::string toString(core::TonalFamily f)
{
    switch (f)
    {
        case core::TonalFamily::Principal:  return "Principal";
        case core::TonalFamily::Flute:      return "Flute";
        case core::TonalFamily::String:     return "String";
        case core::TonalFamily::Reed:       return "Reed";
        case core::TonalFamily::Mixture:    return "Mixture";
        case core::TonalFamily::Hybrid:     return "Hybrid";
        case core::TonalFamily::Percussion: return "Percussion";
        case core::TonalFamily::Undefined:  return "Undefined";
    }
    return "Undefined";
}

std::string toString(core::EngineKind e)
{
    switch (e)
    {
        case core::EngineKind::Sample:    return "Sample";
        case core::EngineKind::Additive:  return "Additive";
        case core::EngineKind::Waveguide: return "Waveguide";
        case core::EngineKind::Modal:     return "Modal";
    }
    return "Additive";
}

std::string toString(core::ChorusRole r)
{
    switch (r)
    {
        case core::ChorusRole::Foundation:   return "Foundation";
        case core::ChorusRole::Chorus:       return "Chorus";
        case core::ChorusRole::MixtureCrown: return "MixtureCrown";
        case core::ChorusRole::Solo:         return "Solo";
        case core::ChorusRole::Color:        return "Color";
        case core::ChorusRole::Effect:       return "Effect";
        case core::ChorusRole::Undefined:    return "Undefined";
    }
    return "Undefined";
}

std::string toString(core::PitchClass p)
{
    switch (p)
    {
        case core::PitchClass::Sub:      return "Sub";
        case core::PitchClass::Unison:   return "Unison";
        case core::PitchClass::Octave:   return "Octave";
        case core::PitchClass::Super:    return "Super";
        case core::PitchClass::Mutation: return "Mutation";
        case core::PitchClass::Compound: return "Compound";
    }
    return "Unison";
}

std::string toString(DivisionKind k)
{
    switch (k)
    {
        case DivisionKind::Manual:   return "Manual";
        case DivisionKind::Pedal:    return "Pedal";
        case DivisionKind::Floating: return "Floating";
    }
    return "Manual";
}

std::string toString(CouplerKind k)
{
    switch (k)
    {
        case CouplerKind::InterManual: return "InterManual";
        case CouplerKind::IntraManual: return "IntraManual";
        case CouplerKind::Melody:      return "Melody";
        case CouplerKind::Bass:        return "Bass";
    }
    return "InterManual";
}

// ---------------------------------------------------------------------------
// Structural validation
// ---------------------------------------------------------------------------

LoadDiagnostics OrganDefinition::validate() const
{
    LoadDiagnostics diag;

    if (name.empty())
        diag.warning("Organ has no name.", "organ");

    // Collect the set of declared names so references can be checked.
    std::unordered_set<std::string> windchestNames;
    for (const auto& w : windchests)
    {
        if (w.name.empty())
            diag.error("Windchest with empty name.", "windchest");
        else if (!windchestNames.insert(w.name).second)
            diag.error("Duplicate windchest name '" + w.name + "'.", "windchest");
    }

    for (std::size_t i = 0; i < ranks.size(); ++i)
    {
        const RankDef& r = ranks[i];
        const std::string ctx = "rank[" + std::to_string(i) + "] '" + r.name + "'";

        // Rank names are NOT required to be unique: on a real organ the same
        // Trompette 8 appears on two manuals as two different ranks of pipes, and
        // this instrument's own demo organ has five such pairs. What must be
        // unique is a REFERENCE, and that is checked where references are made.
        if (r.name.empty())
            diag.error("Rank with empty name.", ctx);

        if (!tonalFamilyFromString(r.family))
            diag.warning("Unknown tonal family '" + r.family + "' (treated as Undefined).", ctx);
        if (!engineKindFromString(r.engine))
            diag.warning("Unknown engine '" + r.engine + "' (treated as Additive).", ctx);
        if (r.footageDen == 0)
            diag.error("Rank footage has zero denominator.", ctx);
        if (r.highNote < r.lowNote)
            diag.warning("Rank compass is inverted; it will be normalised.", ctx);
        if (!r.windchest.empty() && windchestNames.find(r.windchest) == windchestNames.end())
            diag.error("Rank references unknown windchest '" + r.windchest + "'.", ctx);
    }

    std::unordered_set<std::string> divisionNames;
    for (std::size_t i = 0; i < divisions.size(); ++i)
    {
        const DivisionDef& d = divisions[i];
        const std::string ctx = "division[" + std::to_string(i) + "] '" + d.name + "'";

        if (d.name.empty())
            diag.error("Division with empty name.", ctx);
        else if (!divisionNames.insert(d.name).second)
            diag.error("Duplicate division name '" + d.name + "'.", ctx);

        if (!divisionKindFromString(d.kind))
            diag.warning("Unknown division kind '" + d.kind + "' (treated as Manual).", ctx);
    }

    for (std::size_t i = 0; i < stops.size(); ++i)
    {
        const StopDef& s = stops[i];
        const std::string ctx = "stop[" + std::to_string(i) + "] '" + s.name + "'";

        if (s.name.empty())
            diag.error("Stop with empty name.", ctx);
        if (s.footageDen == 0)
            diag.error("Stop footage has zero denominator.", ctx);
        if (divisionNames.find(s.division) == divisionNames.end())
            diag.error("Stop references unknown division '" + s.division + "'.", ctx);
        const std::size_t matches = countRankMatches(ranks, s.rank);
        if (matches == 0)
            diag.error("Stop references unknown rank '" + s.rank + "'.", ctx);
        else if (matches > 1)
            diag.error("Stop references rank '" + s.rank + "', which names "
                       + std::to_string(matches)
                       + " ranks. Qualify it as \"windchest/rank\".", ctx);
    }

    for (std::size_t i = 0; i < couplers.size(); ++i)
    {
        const CouplerDef& c = couplers[i];
        const std::string ctx = "coupler[" + std::to_string(i) + "] '" + c.name + "'";

        if (divisionNames.find(c.from) == divisionNames.end())
            diag.error("Coupler references unknown 'from' division '" + c.from + "'.", ctx);
        if (divisionNames.find(c.to) == divisionNames.end())
            diag.error("Coupler references unknown 'to' division '" + c.to + "'.", ctx);
        if (!couplerKindFromString(c.kind))
            diag.warning("Unknown coupler kind '" + c.kind + "' (treated as InterManual).", ctx);
    }

    // --- Does this organ fit the instrument? --------------------------------
    //
    // An organ larger than the tables that carry it does not fail to load. It
    // loads, reports nothing, and quietly does less -- which is the worst of the
    // three possible outcomes, because the organist is then hunting for a
    // Bombarde that the file declares, the console never draws, and no amount of
    // registration will ever sound. Every one of these was reachable the moment
    // an organ could come from a file instead of from the binary.

    if (stops.size() > core::params::kMaxStopParameters)
    {
        // A stop's id is its index, and the registration the audio thread reads
        // is a single 64-bit word. A stop past the width of that word cannot be
        // held in a registration, cannot be given a host parameter and cannot be
        // drawn -- so this is an error and not a warning: the file describes an
        // instrument that this one cannot be.
        const std::size_t lost = stops.size() - core::params::kMaxStopParameters;
        diag.error("Organ declares " + std::to_string(stops.size())
                   + " stops; this instrument can address "
                   + std::to_string(core::params::kMaxStopParameters)
                   + ". The last " + std::to_string(lost) + " ('"
                   + stops[core::params::kMaxStopParameters].name
                   + "' onwards) could never be drawn or sound.",
                   "organ");
    }

    if (couplers.size() > core::params::kMaxCouplerParameters)
    {
        // Not a warning either, and for a sharper reason than the stops: a
        // coupler past this one is refused by the console as well as absent from
        // the host, so there is no surface anywhere that can engage it.
        const std::size_t lost = couplers.size() - core::params::kMaxCouplerParameters;
        diag.error("Organ declares " + std::to_string(couplers.size())
                   + " couplers; this instrument can address "
                   + std::to_string(core::params::kMaxCouplerParameters)
                   + ". The last " + std::to_string(lost) + " ('"
                   + couplers[core::params::kMaxCouplerParameters].name
                   + "' onwards) could never be engaged.",
                   "organ");
    }

    if (windchests.size() > core::params::kMaxWindchests)
    {
        // A warning, because unlike the two above the organ still plays: the
        // ranks on those chests speak, and draw their wind from chest 0. What is
        // lost is whose wind they take, which is audible as a sag in the wrong
        // place rather than as a missing stop.
        diag.warning("Organ declares " + std::to_string(windchests.size())
                     + " windchests; this instrument holds wind for "
                     + std::to_string(core::params::kMaxWindchests)
                     + ". Ranks on the rest will draw from the first chest.",
                     "organ");
    }

    return diag;
}

std::size_t countRankMatches(const std::vector<RankDef>& ranks,
                             std::string_view reference)
{
    // A qualified reference names exactly one rank or none: the chest and the name
    // together are unique, because chest names are.
    const std::size_t slash = reference.find('/');
    if (slash != std::string_view::npos)
    {
        const std::string_view chest = reference.substr(0, slash);
        const std::string_view name  = reference.substr(slash + 1);
        std::size_t n = 0;
        for (const RankDef& r : ranks)
            if (r.windchest == chest && r.name == name)
                ++n;
        return n;
    }

    std::size_t n = 0;
    for (const RankDef& r : ranks)
        if (r.name == reference)
            ++n;
    return n;
}

std::optional<std::size_t> findRank(const std::vector<RankDef>& ranks,
                                    std::string_view reference)
{
    if (countRankMatches(ranks, reference) != 1)
        return std::nullopt;

    const std::size_t slash = reference.find('/');
    for (std::size_t i = 0; i < ranks.size(); ++i)
    {
        if (slash != std::string_view::npos)
        {
            if (ranks[i].windchest == reference.substr(0, slash)
                && ranks[i].name == reference.substr(slash + 1))
                return i;
        }
        else if (ranks[i].name == reference)
        {
            return i;
        }
    }
    return std::nullopt;
}

std::string rankReference(const std::vector<RankDef>& ranks, std::size_t index)
{
    if (index >= ranks.size())
        return {};

    const RankDef& r = ranks[index];
    if (countRankMatches(ranks, r.name) == 1)
        return r.name; // unambiguous, so say it the short way

    return r.windchest + "/" + r.name;
}

} // namespace caecilia::model
