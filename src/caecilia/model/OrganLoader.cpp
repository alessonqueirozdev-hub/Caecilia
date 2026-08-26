// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/OrganLoader.h"

#include <cstdint>
#include <unordered_map>

namespace caecilia::model
{
namespace
{

using NameToIndex = std::unordered_map<std::string, std::uint16_t>;

/// Look a name up in an index map; returns std::nullopt if absent.
std::optional<std::uint16_t> indexOf(const NameToIndex& map, const std::string& name)
{
    const auto it = map.find(name);
    return it == map.end() ? std::nullopt : std::optional<std::uint16_t>{it->second};
}

RankVoicingSpec toVoicingSpec(const VoicingDef& d)
{
    RankVoicingSpec v;
    v.chiffAmount         = d.chiff;
    v.harmonicDevelopment = d.harmonicDevelopment;
    v.brightness          = d.brightness;
    v.windSensitivity     = d.windSensitivity;
    v.detuneScatterCents  = d.detuneScatterCents;
    v.levelScatterDb      = d.levelScatterDb;
    v.brightnessScatter   = d.brightnessScatter;
    v.attackScatterMs     = d.attackScatterMs;
    return v;
}

SampleSetDescriptor toSampleDescriptor(const SampleSetDef& d)
{
    SampleSetDescriptor s;
    s.resourceToken     = d.resourceToken;
    s.sourceSampleRateHz = d.sourceSampleRate;
    s.channelCount      = d.channels;
    s.baseNote          = d.baseNote;
    s.streaming         = d.streaming;
    s.loopStartFrame    = d.loopStartFrame;
    s.loopEndFrame      = d.loopEndFrame;
    return s;
}

} // namespace

ParseResult OrganLoader::parse(std::string_view content,
                               OrganFileFormat format,
                               std::string_view sourceName)
{
    ParseResult result;
    (void) content;
    (void) format;

    // TODO(phase model): implement the JSON/YAML reader that fills an
    // OrganDefinition from `content`. Until then the loader compiles hand-built
    // definitions and reports parsing as unimplemented so callers fail loudly.
    result.diagnostics.error("Organ-file parsing is not yet implemented.",
                             sourceName.empty() ? std::string{"<memory>"} : std::string{sourceName});
    return result;
}

CompileResult OrganLoader::compile(const OrganDefinition& definition)
{
    CompileResult result;

    // Re-run structural validation so compile() is safe on a hand-built definition.
    result.diagnostics.merge(definition.validate());
    if (result.diagnostics.hasErrors())
        return result; // organ stays absent

    // --- Windchests: id == index -------------------------------------------
    NameToIndex windchestIndex;
    std::vector<Windchest> windchests;
    windchests.reserve(definition.windchests.size());
    for (const WindchestDef& wd : definition.windchests)
    {
        const auto id = static_cast<std::uint16_t>(windchests.size());
        Windchest w;
        w.id                = core::WindchestId{id};
        w.name              = wd.name;
        w.nominalPressurePa = wd.pressurePa;
        w.hasTremulant      = wd.tremulant;
        windchestIndex.emplace(wd.name, id);
        windchests.push_back(std::move(w));
    }

    // --- Ranks: id == index; resolve windchest; materialise pipes ----------
    NameToIndex rankIndex;
    std::vector<Rank> ranks;
    ranks.reserve(definition.ranks.size());
    for (const RankDef& rd : definition.ranks)
    {
        const auto id = static_cast<std::uint16_t>(ranks.size());
        Rank r;
        r.setId(core::RankId{id});
        r.setName(rd.name);
        r.setFamily(tonalFamilyFromString(rd.family).value_or(core::TonalFamily::Undefined));
        r.setEngine(engineKindFromString(rd.engine).value_or(core::EngineKind::Additive));
        r.setFootage(core::Footage{rd.footageNum, rd.footageDen});
        r.setCompass(static_cast<core::MidiNote>(rd.lowNote), static_cast<core::MidiNote>(rd.highNote));
        r.setVoicing(toVoicingSpec(rd.voicing));
        if (rd.sampleSet)
            r.setSampleSet(toSampleDescriptor(*rd.sampleSet));

        PipeSpatial spatial;
        spatial.panNorm        = rd.pan;
        spatial.distanceMeters = rd.distanceM;
        r.setBaseSpatial(spatial);

        if (const auto wc = indexOf(windchestIndex, rd.windchest))
        {
            r.setWindchest(core::WindchestId{*wc});
            windchests[*wc].ranks.push_back(core::RankId{id});
        }

        r.generatePipes();
        rankIndex.emplace(rd.name, id);
        ranks.push_back(std::move(r));
    }

    // --- Divisions: id == index; build manual keyboards --------------------
    NameToIndex divisionIndex;
    std::vector<Division> divisions;
    std::vector<Manual>   manuals;
    divisions.reserve(definition.divisions.size());
    for (const DivisionDef& dd : definition.divisions)
    {
        const auto id = static_cast<std::uint16_t>(divisions.size());
        Division d;
        d.setId(core::DivisionId{id});
        d.setName(dd.name);
        const DivisionKind kind = divisionKindFromString(dd.kind).value_or(DivisionKind::Manual);
        d.setKind(kind);
        d.setEnclosed(dd.enclosed);
        d.setHasTremulant(dd.tremulant);
        d.setCompass(static_cast<core::MidiNote>(dd.lowNote), static_cast<core::MidiNote>(dd.highNote));

        if (kind == DivisionKind::Manual)
        {
            Manual m;
            m.division    = core::DivisionId{id};
            m.manualIndex = dd.manualIndex;
            m.midiChannel = dd.midiChannel;
            m.lowNote     = static_cast<core::MidiNote>(dd.lowNote);
            m.highNote    = static_cast<core::MidiNote>(dd.highNote);
            manuals.push_back(m);
        }

        divisionIndex.emplace(dd.name, id);
        divisions.push_back(std::move(d));
    }

    // --- Stops: id == index; resolve division + rank; register on division --
    std::vector<Stop> stops;
    stops.reserve(definition.stops.size());
    for (const StopDef& sd : definition.stops)
    {
        const auto id = static_cast<std::uint16_t>(stops.size());
        Stop s;
        s.setId(core::StopId{id});
        s.setName(sd.name);
        s.setFamily(tonalFamilyFromString(sd.family).value_or(core::TonalFamily::Undefined));
        s.setFootage(core::Footage{sd.footageNum, sd.footageDen});
        s.setPitchClass(pitchClassFromString(sd.pitchClass).value_or(core::PitchClass::Unison));
        s.setRole(chorusRoleFromString(sd.role).value_or(core::ChorusRole::Foundation));

        if (const auto div = indexOf(divisionIndex, sd.division))
        {
            s.setDivision(core::DivisionId{*div});
            divisions[*div].addStop(core::StopId{id});
        }
        if (const auto rk = indexOf(rankIndex, sd.rank))
            s.setRank(core::RankId{*rk});

        if (!sd.mixture.empty())
        {
            std::vector<core::Footage> comp;
            comp.reserve(sd.mixture.size());
            for (const auto& [num, den] : sd.mixture)
                comp.push_back(core::Footage{num, den});
            s.setMixtureComposition(std::move(comp));
        }

        stops.push_back(std::move(s));
    }

    // --- Stamp each rank with the division that draws it -------------------
    // Ranks are compiled before stops, so generatePipes() could not know the
    // division; the stop list is the only place that link exists. Run it here,
    // once every stop is resolved and before the couplers, so that everything
    // reading a pipe out of the model (Organ::collectPipesForKey, the
    // PerPipeVoicer seed, coupler activation) sees the owning division.
    // A unit rank shared by stops in two divisions takes the last stop's.
    for (const Stop& s : stops)
    {
        const std::size_t rankIdx = s.rank().value;
        if (rankIdx < ranks.size())
            ranks[rankIdx].stampDivision(s.division());
    }

    // --- Couplers: id == index; resolve from/to ----------------------------
    std::vector<Coupler> couplers;
    couplers.reserve(definition.couplers.size());
    for (const CouplerDef& cd : definition.couplers)
    {
        const auto id = static_cast<std::uint16_t>(couplers.size());
        Coupler c;
        c.setId(CouplerId{id});
        c.setName(cd.name);
        if (const auto from = indexOf(divisionIndex, cd.from))
            c.setFrom(core::DivisionId{*from});
        if (const auto to = indexOf(divisionIndex, cd.to))
            c.setTo(core::DivisionId{*to});
        c.setOctaveShift(cd.octaveShift);
        c.setKind(couplerKindFromString(cd.kind).value_or(CouplerKind::InterManual));
        couplers.push_back(std::move(c));
    }

    // --- Assemble the immutable organ --------------------------------------
    Organ organ;
    organ.setName(definition.name);
    organ.setBuilder(definition.builder);
    organ.setYear(definition.year);
    organ.setWindchests(std::move(windchests));
    organ.setRanks(std::move(ranks));
    organ.setStops(std::move(stops));
    organ.setDivisions(std::move(divisions));
    organ.setManuals(std::move(manuals));
    organ.setCouplers(std::move(couplers));

    result.organ = std::move(organ);
    return result;
}

CompileResult OrganLoader::load(std::string_view content,
                                OrganFileFormat format,
                                std::string_view sourceName)
{
    CompileResult result;

    ParseResult parsed = parse(content, format, sourceName);
    result.diagnostics.merge(parsed.diagnostics);
    if (!parsed.ok())
        return result;

    CompileResult compiled = compile(*parsed.definition);
    result.diagnostics.merge(compiled.diagnostics);
    result.organ = std::move(compiled.organ);
    return result;
}

std::string OrganLoader::serialize(const OrganDefinition& definition, OrganFileFormat format)
{
    (void) definition;
    (void) format;
    // TODO(phase model): implement the JSON/YAML writer (round-trips
    // an OrganDefinition back to editable document text).
    return {};
}

} // namespace caecilia::model
