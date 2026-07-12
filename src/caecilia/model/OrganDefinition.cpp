/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#include "caecilia/model/OrganDefinition.h"

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

    std::unordered_set<std::string> rankNames;
    for (std::size_t i = 0; i < ranks.size(); ++i)
    {
        const RankDef& r = ranks[i];
        const std::string ctx = "rank[" + std::to_string(i) + "] '" + r.name + "'";

        if (r.name.empty())
            diag.error("Rank with empty name.", ctx);
        else if (!rankNames.insert(r.name).second)
            diag.error("Duplicate rank name '" + r.name + "'.", ctx);

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
        if (rankNames.find(s.rank) == rankNames.end())
            diag.error("Stop references unknown rank '" + s.rank + "'.", ctx);
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

    return diag;
}

} // namespace caecilia::model
