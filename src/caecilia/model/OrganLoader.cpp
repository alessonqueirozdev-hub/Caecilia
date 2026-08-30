// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/model/OrganLoader.h"

#include "caecilia/model/SpectralModelFile.h"

#include "caecilia/model/Json.h"

#include <algorithm>
#include <cmath>

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

namespace
{
// ---------------------------------------------------------------------------
// Reading a document into an OrganDefinition.
//
// Every field is optional and every one has a default in the *Def structs, so a
// minimal organ file is short and a full one is explicit. What is NOT tolerated
// is a field of the wrong TYPE or an enum token that is not one: those are
// mistakes a builder wants told about, and defaulting past them produces an organ
// that is quietly not the one they described.
// ---------------------------------------------------------------------------

/// Accumulates diagnostics against a path like "ranks[2].windchest".
class Reader
{
public:
    Reader(LoadDiagnostics& diagnostics, std::string source)
        : diag_(diagnostics), source_(std::move(source))
    {
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

    void error(const std::string& path, const std::string& message,
               const json::Value* at = nullptr)
    {
        std::string where = source_ + ": " + path;
        if (at != nullptr)
            where += " (line " + std::to_string(at->position().line) + ")";
        diag_.error(message, where);
        failed_ = true;
    }

    /// A string field. Absent leaves @p out alone; present-but-not-a-string is an
    /// error rather than a silent default.
    void str(const json::Value& obj, const char* key, const std::string& path,
             std::string& out)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isString())
        {
            error(path + "." + key, "expected a string", v);
            return;
        }
        out = v->asString();
    }

    void number(const json::Value& obj, const char* key, const std::string& path,
                float& out)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isNumber())
        {
            error(path + "." + key, "expected a number", v);
            return;
        }
        out = static_cast<float>(v->asNumber());
    }

    void number(const json::Value& obj, const char* key, const std::string& path,
                double& out)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isNumber())
        {
            error(path + "." + key, "expected a number", v);
            return;
        }
        out = v->asNumber();
    }

    /// An integer field. A number with a fraction is a mistake here -- a note
    /// number or a rank count is a whole thing -- and saying so beats truncating.
    template <typename Int>
    void integer(const json::Value& obj, const char* key, const std::string& path,
                 Int& out)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isNumber())
        {
            error(path + "." + key, "expected a whole number", v);
            return;
        }
        const double d = v->asNumber();
        if (d != std::floor(d))
        {
            error(path + "." + key, "expected a whole number", v);
            return;
        }
        out = static_cast<Int>(d);
    }

    void boolean(const json::Value& obj, const char* key, const std::string& path,
                 bool& out)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isBool())
        {
            error(path + "." + key, "expected true or false", v);
            return;
        }
        out = v->asBool();
    }

    /// An enum-valued string, checked against its parser so an unknown token is a
    /// diagnostic rather than a silent fall back to the default.
    template <typename Parse>
    void token(const json::Value& obj, const char* key, const std::string& path,
               std::string& out, Parse&& parse, const char* what)
    {
        const json::Value* v = obj.find(key);
        if (v == nullptr)
            return;
        if (!v->isString())
        {
            error(path + "." + key, "expected a string", v);
            return;
        }
        if (!parse(v->asString()).has_value())
        {
            error(path + "." + key,
                  "'" + v->asString() + "' is not a known " + what, v);
            return;
        }
        out = v->asString();
    }

    /// An array of objects, each handed to @p each with its own path.
    template <typename Each>
    void objects(const json::Value& root, const char* key, Each&& each)
    {
        const json::Value* arr = root.find(key);
        if (arr == nullptr)
            return;
        if (!arr->isArray())
        {
            error(key, "expected an array", arr);
            return;
        }
        for (std::size_t i = 0; i < arr->items().size(); ++i)
        {
            const json::Value& item = arr->items()[i];
            const std::string  path = std::string(key) + "[" + std::to_string(i) + "]";
            if (!item.isObject())
            {
                error(path, "expected an object", &item);
                continue;
            }
            each(item, path);
        }
    }

private:
    LoadDiagnostics& diag_;
    std::string      source_;
    bool             failed_ = false;
};

void readVoicing(Reader& r, const json::Value& obj, const std::string& path,
                 VoicingDef& v)
{
    const json::Value* block = obj.find("voicing");
    if (block == nullptr)
        return;
    if (!block->isObject())
    {
        r.error(path + ".voicing", "expected an object", block);
        return;
    }
    const std::string p = path + ".voicing";
    r.number(*block, "chiff",               p, v.chiff);
    r.number(*block, "harmonicDevelopment", p, v.harmonicDevelopment);
    r.number(*block, "brightness",          p, v.brightness);
    r.number(*block, "windSensitivity",     p, v.windSensitivity);
    r.number(*block, "detuneScatterCents",  p, v.detuneScatterCents);
    r.number(*block, "levelScatterDb",      p, v.levelScatterDb);
    r.number(*block, "brightnessScatter",   p, v.brightnessScatter);
    r.number(*block, "attackScatterMs",     p, v.attackScatterMs);
}

void readSampleSet(Reader& r, const json::Value& obj, const std::string& path,
                   std::optional<SampleSetDef>& out)
{
    const json::Value* block = obj.find("sampleSet");
    if (block == nullptr)
        return;
    if (!block->isObject())
    {
        r.error(path + ".sampleSet", "expected an object", block);
        return;
    }

    SampleSetDef       s;
    const std::string  p = path + ".sampleSet";
    r.str(*block, "resource",   p, s.resourceToken);
    r.number(*block, "sampleRate", p, s.sourceSampleRate);
    r.integer(*block, "channels",   p, s.channels);
    r.integer(*block, "baseNote",   p, s.baseNote);
    r.boolean(*block, "streaming",  p, s.streaming);
    r.integer(*block, "loopStart",  p, s.loopStartFrame);
    r.integer(*block, "loopEnd",    p, s.loopEndFrame);
    out = s;
}

/// A footage, written either as a number ("8") or as a pair ("[8, 3]" for 2 2/3').
///
/// Both spellings because both are natural: an 8' rank is a number and a 2 2/3'
/// one is a ratio, and forcing the first to be written [8, 1] would make every
/// ordinary rank look like an exception.
void readFootage(Reader& r, const json::Value& obj, const char* key,
                 const std::string& path, std::int32_t& num, std::int32_t& den)
{
    const json::Value* v = obj.find(key);
    if (v == nullptr)
        return;

    if (v->isNumber())
    {
        const double d = v->asNumber();
        if (d != std::floor(d) || d <= 0.0)
        {
            r.error(path + "." + key, "expected a whole number of feet", v);
            return;
        }
        num = static_cast<std::int32_t>(d);
        den = 1;
        return;
    }

    if (v->isArray() && v->items().size() == 2
        && v->items()[0].isNumber() && v->items()[1].isNumber())
    {
        const double n = v->items()[0].asNumber();
        const double d = v->items()[1].asNumber();
        if (n != std::floor(n) || d != std::floor(d) || n <= 0.0 || d <= 0.0)
        {
            r.error(path + "." + key, "a footage ratio must be whole and positive", v);
            return;
        }
        num = static_cast<std::int32_t>(n);
        den = static_cast<std::int32_t>(d);
        return;
    }

    r.error(path + "." + key,
            "expected a number of feet, or a [numerator, denominator] pair", v);
}
} // namespace

ParseResult OrganLoader::parse(std::string_view content,
                               OrganFileFormat format,
                               std::string_view sourceName)
{
    ParseResult result;
    const std::string source =
        sourceName.empty() ? std::string{"<memory>"} : std::string{sourceName};

    // Sniffing is deliberately crude and deliberately documented: a document that
    // does not begin with '{' is not JSON, and there is no YAML reader to fall
    // back to, so saying so is more useful than guessing.
    OrganFileFormat resolved = format;
    if (resolved == OrganFileFormat::Auto)
    {
        std::size_t i = 0;
        while (i < content.size()
               && (content[i] == ' ' || content[i] == '\t'
                   || content[i] == '\n' || content[i] == '\r'))
            ++i;
        resolved = (i < content.size() && content[i] == '{') ? OrganFileFormat::Json
                                                             : OrganFileFormat::Yaml;
    }

    if (resolved != OrganFileFormat::Json)
    {
        result.diagnostics.error("Only JSON organ documents are supported.", source);
        return result;
    }

    json::Error jsonError;
    const std::optional<json::Value> root = json::parse(content, jsonError);
    if (!root.has_value())
    {
        result.diagnostics.error(jsonError.message,
                                 source + ": line " + std::to_string(jsonError.where.line)
                                        + ", column " + std::to_string(jsonError.where.column));
        return result;
    }
    if (!root->isObject())
    {
        result.diagnostics.error("An organ document must be a JSON object.", source);
        return result;
    }

    Reader          r(result.diagnostics, source);
    OrganDefinition def;

    r.str(*root, "name",    "", def.name);
    r.str(*root, "builder", "", def.builder);
    r.integer(*root, "year", "", def.year);

    r.objects(*root, "windchests", [&](const json::Value& o, const std::string& path)
    {
        WindchestDef w;
        r.str(o, "name", path, w.name);
        r.number(o, "pressurePa", path, w.pressurePa);
        r.boolean(o, "tremulant", path, w.tremulant);
        def.windchests.push_back(std::move(w));
    });

    r.objects(*root, "ranks", [&](const json::Value& o, const std::string& path)
    {
        RankDef k;
        r.str(o, "name", path, k.name);
        r.token(o, "family", path, k.family, tonalFamilyFromString, "tonal family");
        r.token(o, "engine", path, k.engine, engineKindFromString, "engine kind");
        readFootage(r, o, "footage", path, k.footageNum, k.footageDen);
        r.str(o, "windchest", path, k.windchest);
        r.str(o, "spectrum",  path, k.spectrum);
        r.boolean(o, "stopped", path, k.stopped);
        r.integer(o, "lowNote",  path, k.lowNote);
        r.integer(o, "highNote", path, k.highNote);
        r.number(o, "pan",       path, k.pan);
        r.number(o, "distanceM", path, k.distanceM);
        readVoicing(r, o, path, k.voicing);
        readSampleSet(r, o, path, k.sampleSet);
        def.ranks.push_back(std::move(k));
    });

    r.objects(*root, "divisions", [&](const json::Value& o, const std::string& path)
    {
        DivisionDef d;
        r.str(o, "name", path, d.name);
        r.token(o, "kind", path, d.kind, divisionKindFromString, "division kind");
        r.integer(o, "lowNote",  path, d.lowNote);
        r.integer(o, "highNote", path, d.highNote);
        r.boolean(o, "enclosed",  path, d.enclosed);
        r.boolean(o, "tremulant", path, d.tremulant);
        r.integer(o, "manual",      path, d.manualIndex);
        r.integer(o, "midiChannel", path, d.midiChannel);
        def.divisions.push_back(std::move(d));
    });

    r.objects(*root, "stops", [&](const json::Value& o, const std::string& path)
    {
        StopDef s;
        r.str(o, "name", path, s.name);
        r.token(o, "family", path, s.family, tonalFamilyFromString, "tonal family");
        readFootage(r, o, "footage", path, s.footageNum, s.footageDen);
        r.token(o, "pitchClass", path, s.pitchClass, pitchClassFromString, "pitch class");
        r.token(o, "role", path, s.role, chorusRoleFromString, "chorus role");
        r.str(o, "division", path, s.division);
        r.str(o, "rank",     path, s.rank);

        // A compound stop names its constituent footages, in the same two
        // spellings a rank's footage uses.
        if (const json::Value* mix = o.find("mixture"))
        {
            if (!mix->isArray())
            {
                r.error(path + ".mixture", "expected an array of footages", mix);
            }
            else
            {
                for (std::size_t m = 0; m < mix->items().size(); ++m)
                {
                    json::Value wrapper = json::Value::object();
                    wrapper.members().emplace_back("f", mix->items()[m]);

                    std::int32_t num = 0, den = 1;
                    readFootage(r, wrapper, "f",
                                path + ".mixture[" + std::to_string(m) + "]", num, den);
                    if (num > 0)
                        s.mixture.emplace_back(num, den);
                }
            }
        }
        def.stops.push_back(std::move(s));
    });

    r.objects(*root, "couplers", [&](const json::Value& o, const std::string& path)
    {
        CouplerDef c;
        r.str(o, "name", path, c.name);
        r.str(o, "from", path, c.from);
        r.str(o, "to",   path, c.to);
        r.integer(o, "octaveShift", path, c.octaveShift);
        r.token(o, "kind", path, c.kind, couplerKindFromString, "coupler kind");
        def.couplers.push_back(std::move(c));
    });

    if (r.failed())
        return result; // definition stays absent; every problem is already reported

    // Structural validation runs here rather than only in compile(), so a caller
    // that parses without compiling still learns about a dangling reference.
    result.diagnostics.merge(def.validate());
    if (result.diagnostics.hasErrors())
        return result;

    result.definition = std::move(def);
    return result;
}

CompileResult OrganLoader::compile(const OrganDefinition& definition,
                                   const ResourceResolver& resolve)
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

        r.setStopped(rd.stopped);

        // A measured spectrum, if this rank names one. The reference is kept
        // whether or not it resolves, so a document that travelled without its
        // spectra round-trips unchanged rather than quietly losing the reference
        // on the first save.
        if (! rd.spectrum.empty())
        {
            r.setSpectrumFile(rd.spectrum);
            const std::string ctx = "rank '" + rd.name + "' spectrum '" + rd.spectrum + "'";

            if (! resolve)
            {
                result.diagnostics.warning(
                    "This rank names a measured spectrum, but nothing here can open "
                    "one. It will sound from the procedural recipe.", ctx);
            }
            else if (const std::optional<std::string> text = resolve(rd.spectrum))
            {
                SpectralModelLoad loaded = loadSpectralModel(*text, rd.spectrum);
                result.diagnostics.merge(loaded.diagnostics);
                if (loaded.model.has_value())
                    r.setMeasuredSpectrum(std::move(*loaded.model));
            }
            else
            {
                // A warning and not an error: the rank still speaks, from the
                // recipe. Refusing the whole organ because one spectrum file was
                // left behind would be the worse trade -- but saying nothing would
                // leave an organist wondering why their measured Montre sounds
                // like everyone else's.
                result.diagnostics.warning("Could not open it; this rank will sound "
                                           "from the procedural recipe.", ctx);
            }
        }
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

        // A keyboard for anything an organist plays with, which includes the
        // PEDALBOARD: it has a compass, it has a MIDI channel, and without one the
        // channel-to-division map has no entry for it and the pedals cannot be
        // played at all. Only a Floating division has no keyboard of its own --
        // that is what floating means.
        //
        // compile() built one for DivisionKind::Manual alone, so an organ loaded
        // from a document came back with one fewer keyboard than the same organ
        // built in C++. Caught by round-tripping the demo organ through the format.
        if (kind == DivisionKind::Manual || kind == DivisionKind::Pedal)
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

    // Keyboards in STACKING order, lowest first, which is the order an organist
    // sees them and the order the console draws them. Compiling them in division
    // order instead made the vector's meaning depend on the order the document
    // happened to declare its divisions in -- so the same organ built in C++ and
    // loaded from a file came back with its keyboards in different positions,
    // while carrying the same manualIndex on each. The index is the truth; this
    // makes the vector agree with it.
    std::sort(manuals.begin(), manuals.end(),
              [](const Manual& a, const Manual& b)
              {
                  if (a.manualIndex != b.manualIndex)
                      return a.manualIndex < b.manualIndex;
                  return a.division.value < b.division.value; // stable for a tie
              });

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
        if (const auto rk = findRank(definition.ranks, sd.rank))
            s.setRank(core::RankId{ static_cast<std::uint16_t>(*rk) });

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
                                std::string_view sourceName,
                                const ResourceResolver& resolve)
{
    CompileResult result;

    ParseResult parsed = parse(content, format, sourceName);
    result.diagnostics.merge(parsed.diagnostics);
    if (!parsed.ok())
        return result;

    CompileResult compiled = compile(*parsed.definition, resolve);
    result.diagnostics.merge(compiled.diagnostics);
    result.organ = std::move(compiled.organ);
    return result;
}

namespace
{
// ---------------------------------------------------------------------------
// Writing a definition back out.
//
// Only what differs from the default is written. An organ file is a document a
// person reads, and a rank that spells out eight voicing parameters it did not
// choose buries the two it did.
// ---------------------------------------------------------------------------

void put(json::Value& obj, const char* key, const std::string& v)
{
    obj.members().emplace_back(key, json::Value(v));
}

void put(json::Value& obj, const char* key, double v)
{
    obj.members().emplace_back(key, json::Value(v));
}

/// Most numbers in an organ document are floats -- pressures, voicing, pan --
/// and they must be written the way a float reads, not the way its widened double
/// does. See json::Value(float).
void put(json::Value& obj, const char* key, float v)
{
    obj.members().emplace_back(key, json::Value(v));
}

void put(json::Value& obj, const char* key, bool v)
{
    obj.members().emplace_back(key, json::Value(v));
}

/// A footage: a bare number when it is whole feet, a pair when it is a ratio --
/// the two spellings the reader accepts, chosen the way a builder would write it.
void putFootage(json::Value& obj, const char* key, std::int32_t num, std::int32_t den)
{
    if (den == 1)
    {
        put(obj, key, static_cast<double>(num));
        return;
    }
    json::Value pair = json::Value::array();
    pair.items().push_back(json::Value(static_cast<double>(num)));
    pair.items().push_back(json::Value(static_cast<double>(den)));
    obj.members().emplace_back(key, std::move(pair));
}

json::Value voicingToJson(const VoicingDef& v)
{
    const VoicingDef d{}; // the defaults, to compare against
    json::Value      out = json::Value::object();

    if (v.chiff               != d.chiff)               put(out, "chiff", v.chiff);
    if (v.harmonicDevelopment != d.harmonicDevelopment) put(out, "harmonicDevelopment", v.harmonicDevelopment);
    if (v.brightness          != d.brightness)          put(out, "brightness", v.brightness);
    if (v.windSensitivity     != d.windSensitivity)     put(out, "windSensitivity", v.windSensitivity);
    if (v.detuneScatterCents  != d.detuneScatterCents)  put(out, "detuneScatterCents", v.detuneScatterCents);
    if (v.levelScatterDb      != d.levelScatterDb)      put(out, "levelScatterDb", v.levelScatterDb);
    if (v.brightnessScatter   != d.brightnessScatter)   put(out, "brightnessScatter", v.brightnessScatter);
    if (v.attackScatterMs     != d.attackScatterMs)     put(out, "attackScatterMs", v.attackScatterMs);
    return out;
}
} // namespace

std::string OrganLoader::serialize(const OrganDefinition& definition,
                                   OrganFileFormat format)
{
    if (format == OrganFileFormat::Yaml)
        return {}; // no YAML writer; parse() says the same about reading one

    const RankDef     rankDefaults{};
    const DivisionDef divDefaults{};
    const StopDef     stopDefaults{};
    const CouplerDef  couplerDefaults{};
    const WindchestDef chestDefaults{};

    json::Value root = json::Value::object();
    put(root, "name", definition.name);
    if (!definition.builder.empty())
        put(root, "builder", definition.builder);
    if (definition.year != 0)
        put(root, "year", static_cast<double>(definition.year));

    json::Value chests = json::Value::array();
    for (const WindchestDef& w : definition.windchests)
    {
        json::Value o = json::Value::object();
        put(o, "name", w.name);
        if (w.pressurePa != chestDefaults.pressurePa)
            put(o, "pressurePa", w.pressurePa);
        if (w.tremulant != chestDefaults.tremulant)
            put(o, "tremulant", w.tremulant);
        chests.items().push_back(std::move(o));
    }
    if (!chests.items().empty())
        root.members().emplace_back("windchests", std::move(chests));

    json::Value ranks = json::Value::array();
    for (const RankDef& k : definition.ranks)
    {
        json::Value o = json::Value::object();
        put(o, "name", k.name);
        put(o, "family", k.family);
        if (k.engine != rankDefaults.engine)
            put(o, "engine", k.engine);
        putFootage(o, "footage", k.footageNum, k.footageDen);
        if (!k.windchest.empty())
            put(o, "windchest", k.windchest);
        if (! k.spectrum.empty()) put(o, "spectrum", k.spectrum);
        if (k.stopped != rankDefaults.stopped) put(o, "stopped", k.stopped);
        if (k.lowNote  != rankDefaults.lowNote)  put(o, "lowNote",  static_cast<double>(k.lowNote));
        if (k.highNote != rankDefaults.highNote) put(o, "highNote", static_cast<double>(k.highNote));
        if (k.pan       != rankDefaults.pan)       put(o, "pan", k.pan);
        if (k.distanceM != rankDefaults.distanceM) put(o, "distanceM", k.distanceM);

        json::Value voicing = voicingToJson(k.voicing);
        if (!voicing.members().empty())
            o.members().emplace_back("voicing", std::move(voicing));

        if (k.sampleSet)
        {
            const SampleSetDef  s{};
            json::Value         set = json::Value::object();
            put(set, "resource", k.sampleSet->resourceToken);
            if (k.sampleSet->sourceSampleRate != s.sourceSampleRate)
                put(set, "sampleRate", k.sampleSet->sourceSampleRate);
            if (k.sampleSet->channels != s.channels)
                put(set, "channels", static_cast<double>(k.sampleSet->channels));
            if (k.sampleSet->baseNote != s.baseNote)
                put(set, "baseNote", static_cast<double>(k.sampleSet->baseNote));
            if (k.sampleSet->streaming != s.streaming)
                put(set, "streaming", k.sampleSet->streaming);
            if (k.sampleSet->loopStartFrame != s.loopStartFrame)
                put(set, "loopStart", static_cast<double>(k.sampleSet->loopStartFrame));
            if (k.sampleSet->loopEndFrame != s.loopEndFrame)
                put(set, "loopEnd", static_cast<double>(k.sampleSet->loopEndFrame));
            o.members().emplace_back("sampleSet", std::move(set));
        }
        ranks.items().push_back(std::move(o));
    }
    if (!ranks.items().empty())
        root.members().emplace_back("ranks", std::move(ranks));

    json::Value divisions = json::Value::array();
    for (const DivisionDef& d : definition.divisions)
    {
        json::Value o = json::Value::object();
        put(o, "name", d.name);
        put(o, "kind", d.kind);
        if (d.lowNote  != divDefaults.lowNote)  put(o, "lowNote",  static_cast<double>(d.lowNote));
        if (d.highNote != divDefaults.highNote) put(o, "highNote", static_cast<double>(d.highNote));
        if (d.enclosed  != divDefaults.enclosed)  put(o, "enclosed", d.enclosed);
        if (d.tremulant != divDefaults.tremulant) put(o, "tremulant", d.tremulant);
        if (d.manualIndex != divDefaults.manualIndex)
            put(o, "manual", static_cast<double>(d.manualIndex));
        if (d.midiChannel != divDefaults.midiChannel)
            put(o, "midiChannel", static_cast<double>(d.midiChannel));
        divisions.items().push_back(std::move(o));
    }
    if (!divisions.items().empty())
        root.members().emplace_back("divisions", std::move(divisions));

    json::Value stops = json::Value::array();
    for (const StopDef& s : definition.stops)
    {
        json::Value o = json::Value::object();
        put(o, "name", s.name);
        put(o, "family", s.family);
        putFootage(o, "footage", s.footageNum, s.footageDen);
        if (s.pitchClass != stopDefaults.pitchClass) put(o, "pitchClass", s.pitchClass);
        if (s.role       != stopDefaults.role)       put(o, "role", s.role);
        put(o, "division", s.division);
        put(o, "rank", s.rank);

        if (!s.mixture.empty())
        {
            json::Value mix = json::Value::array();
            for (const auto& f : s.mixture)
            {
                if (f.second == 1)
                {
                    mix.items().push_back(json::Value(static_cast<double>(f.first)));
                }
                else
                {
                    json::Value pair = json::Value::array();
                    pair.items().push_back(json::Value(static_cast<double>(f.first)));
                    pair.items().push_back(json::Value(static_cast<double>(f.second)));
                    mix.items().push_back(std::move(pair));
                }
            }
            o.members().emplace_back("mixture", std::move(mix));
        }
        stops.items().push_back(std::move(o));
    }
    if (!stops.items().empty())
        root.members().emplace_back("stops", std::move(stops));

    json::Value couplers = json::Value::array();
    for (const CouplerDef& c : definition.couplers)
    {
        json::Value o = json::Value::object();
        put(o, "name", c.name);
        put(o, "from", c.from);
        put(o, "to",   c.to);
        if (c.octaveShift != couplerDefaults.octaveShift)
            put(o, "octaveShift", static_cast<double>(c.octaveShift));
        if (c.kind != couplerDefaults.kind)
            put(o, "kind", c.kind);
        couplers.items().push_back(std::move(o));
    }
    if (!couplers.items().empty())
        root.members().emplace_back("couplers", std::move(couplers));

    return json::write(root);
}

OrganDefinition OrganLoader::definitionFrom(const Organ& organ)
{
    OrganDefinition def;
    def.name    = organ.name();
    def.builder = organ.builder();
    def.year    = organ.year();

    // Ids are dense indices into these vectors, which is exactly the invariant
    // compile() established, so resolving one back to a name is a lookup and not
    // a search.
    const auto chestName = [&organ](core::WindchestId id) -> std::string
    {
        return id.value < organ.windchests().size() ? organ.windchests()[id.value].name
                                                    : std::string{};
    };
    // A stop's rank reference is the SHORTEST unambiguous one -- the bare name
    // where it is unique, "windchest/rank" where it is not. Most of a document
    // therefore reads as bare names, and the qualified ones are exactly the places
    // a reader would otherwise have to guess.
    const auto rankRef = [&def](core::RankId id) -> std::string
    {
        return id.value < def.ranks.size()
                 ? rankReference(def.ranks, static_cast<std::size_t>(id.value))
                 : std::string{};
    };
    const auto divisionName = [&organ](core::DivisionId id) -> std::string
    {
        return id.value < organ.divisions().size() ? organ.divisions()[id.value].name()
                                                   : std::string{};
    };

    for (const Windchest& w : organ.windchests())
    {
        WindchestDef d;
        d.name       = w.name;
        d.pressurePa = w.nominalPressurePa;
        d.tremulant  = w.hasTremulant;
        def.windchests.push_back(std::move(d));
    }

    for (const Rank& r : organ.ranks())
    {
        RankDef d;
        d.name       = r.name();
        d.family     = toString(r.family());
        d.engine     = toString(r.engine());
        d.footageNum = r.footage().num;
        d.footageDen = r.footage().den;
        d.windchest  = chestName(r.windchest());
        d.spectrum   = r.spectrumFile();
        d.stopped    = r.isStopped();
        d.lowNote    = r.lowNote();
        d.highNote   = r.highNote();
        d.pan        = r.baseSpatial().panNorm;
        d.distanceM  = r.baseSpatial().distanceMeters;

        const RankVoicingSpec& v = r.voicing();
        d.voicing.chiff               = v.chiffAmount;
        d.voicing.harmonicDevelopment = v.harmonicDevelopment;
        d.voicing.brightness          = v.brightness;
        d.voicing.windSensitivity     = v.windSensitivity;
        d.voicing.detuneScatterCents  = v.detuneScatterCents;
        d.voicing.levelScatterDb      = v.levelScatterDb;
        d.voicing.brightnessScatter   = v.brightnessScatter;
        d.voicing.attackScatterMs     = v.attackScatterMs;

        if (r.hasSamples())
        {
            const SampleSetDescriptor& s = r.sampleSet();
            SampleSetDef sd;
            sd.resourceToken    = s.resourceToken;
            sd.sourceSampleRate = s.sourceSampleRateHz;
            sd.channels         = s.channelCount;
            sd.baseNote         = s.baseNote;
            sd.streaming        = s.streaming;
            sd.loopStartFrame   = s.loopStartFrame;
            sd.loopEndFrame     = s.loopEndFrame;
            d.sampleSet         = sd;
        }

        def.ranks.push_back(std::move(d));
    }

    for (const Division& v : organ.divisions())
    {
        DivisionDef d;
        d.name      = v.name();
        d.kind      = toString(v.kind());
        d.lowNote   = v.lowNote();
        d.highNote  = v.highNote();
        d.enclosed  = v.isEnclosed();
        d.tremulant = v.hasTremulant();

        // The manual keyboard is a separate object in the compiled organ and a
        // pair of fields on the division in the document, which is where a person
        // would expect to write them.
        for (const Manual& m : organ.manuals())
            if (m.division.value == v.id().value)
            {
                d.manualIndex = m.manualIndex;
                d.midiChannel = m.midiChannel;
                break;
            }

        def.divisions.push_back(std::move(d));
    }

    for (const Stop& s : organ.stops())
    {
        StopDef d;
        d.name       = s.name();
        d.family     = toString(s.family());
        d.footageNum = s.footage().num;
        d.footageDen = s.footage().den;
        d.pitchClass = toString(s.pitchClass());
        d.role       = toString(s.role());
        d.division   = divisionName(s.division());
        d.rank       = rankRef(s.rank());
        for (const core::Footage f : s.mixtureComposition())
            d.mixture.emplace_back(f.num, f.den);
        def.stops.push_back(std::move(d));
    }

    for (const Coupler& c : organ.couplers())
    {
        CouplerDef d;
        d.name        = c.name();
        d.from        = divisionName(c.from());
        d.to          = divisionName(c.to());
        d.octaveShift = c.octaveShiftSemitones();
        d.kind        = toString(c.kind());
        def.couplers.push_back(std::move(d));
    }

    return def;
}

} // namespace caecilia::model
