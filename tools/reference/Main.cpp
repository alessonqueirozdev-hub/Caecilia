// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// caecilia-reference â€” the numbers the Rust 2.0 engine is tested against.
//
// The 2.0 port is a rewrite, not a translation, and "it sounds the same" has to
// mean something a test can check. This tool drives the C++ 1.x instrument down
// EXACTLY the path the plugin takes -- the demo organ, one AdditiveVoice per
// (rank, note), the organ's own wind model stepped by the engine, the live tuning
// -- and writes what comes out as numbers:
//
//   * the voicing data itself (every stop's 8'-referenced spectrum), which is
//     deterministic and must be reproduced EXACTLY by the port;
//   * the tuning tables of every built-in temperament, also exact;
//   * one held note per stop at the bottom, middle and top of its compass:
//     level, attack, release, stereo correlation and a harmonic profile;
//   * a full chord landing on the reservoir: per-block chest pressure and the
//     published sag, so the wind's response to a key is a curve to match;
//   * a tremulant: the pressure trajectory and the envelope it puts on a note.
//
// The render is DRY -- no reverb, no master EQ, no limiter -- because those are
// separate processors with separate tests, and a reference that folds them in
// cannot tell a voice regression from a reverb one.
//
// The partials drift on a deterministic random walk, so the port is not expected
// to match sample for sample. It is expected to match these figures within the
// tolerances its tests state.
//
// Usage:
//   caecilia-reference --out <dir> [--sr 48000] [--block 256] [--wav]
//
// Writes <dir>/reference.json, and with --wav one WAV per rendered case for
// listening (large; keep them out of the repository).
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/engine/EngineCommand.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/tuning/LiveTuning.h"
#include "caecilia/core/Version.h"
#include "caecilia/wind/OrganWind.h"
#include "caecilia/wind/WindModel.h"
#include "caecilia/wind/WindTypes.h"

#include "common/CliArgs.h"
#include "common/DenormalGuard.h"
#include "common/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace core  = caecilia::core;
namespace eng   = caecilia::core::engine;
namespace model = caecilia::model;
namespace synth = caecilia::synth;
namespace tun   = caecilia::tuning;
namespace wind  = caecilia::wind;

namespace
{

constexpr std::size_t kPolyphony = 512;

// ---------------------------------------------------------------------------
// A tiny JSON writer. The output is read by the Rust tests, so it only has to be
// valid JSON with stable key order; pretty enough to diff by eye.
// ---------------------------------------------------------------------------
class Json
{
public:
    explicit Json(std::FILE* f) : f_(f) {}

    void open(const char* key, char bracket)  { sep(); keyOf(key); std::fputc(bracket, f_); first_ = true; }
    void close(char bracket)                  { std::fputc(bracket, f_); first_ = false; }
    void num(const char* key, double v)       { sep(); keyOf(key); number(v); }
    void str(const char* key, const std::string& v)
    {
        sep(); keyOf(key);
        std::fputc('"', f_);
        for (const char c : v)
        {
            if (c == '"' || c == '\\') std::fputc('\\', f_);
            std::fputc(c, f_);
        }
        std::fputc('"', f_);
    }
    void boolean(const char* key, bool v)     { sep(); keyOf(key); std::fputs(v ? "true" : "false", f_); }
    void array(const char* key, const std::vector<double>& v)
    {
        open(key, '[');
        for (const double x : v) { sep(); number(x); }
        close(']');
    }

private:
    void sep()   { if (! first_) std::fputc(',', f_); first_ = false; }
    void keyOf(const char* key) { if (key != nullptr) std::fprintf(f_, "\"%s\":", key); }
    void number(double v)
    {
        if (! std::isfinite(v)) { std::fputs("null", f_); return; }
        std::fprintf(f_, "%.17g", v);
    }

    std::FILE* f_;
    bool       first_ = true;
};

// ---------------------------------------------------------------------------
// The instrument, built the way CaeciliaAudioProcessor builds it.
// ---------------------------------------------------------------------------
struct Instrument
{
    model::Organ                                       organ = model::buildCaeciliaDemoOrgan();
    wind::WindModel                                    windModel;
    tun::LiveTuning                                    tuning;
    eng::AudioEngine                                   engine;
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         voicePtrs;
    std::vector<synth::RankVoicing>                    voicings;
    std::uint64_t                                      epoch = 0;

    Instrument(double sr, std::size_t block)
    {
        windModel.prepare(sr, block);
        windModel.configure(wind::configFromOrgan(organ));
        windModel.reset();

        const auto chests = std::max<std::size_t>(organ.windchests().size(), 1);
        engine.prepare(sr, block, 2, chests);
        engine.setWindSupply(&windModel);
        // An offline render: the governor, were it on, would only meter.
        engine.setRealtime(false);

        std::vector<eng::ChestEnclosure> enclosed;
        for (const model::Division& d : organ.divisions())
            if (d.isEnclosed())
                for (const core::WindchestId c : d.windchests())
                    enclosed.push_back(eng::ChestEnclosure{ c, d.id() });
        engine.setEnclosedChests(enclosed);

        tuning.publish(tun::makeSnapshot(core::TemperamentId::Equal, 440.0));
        tuning.adoptPending();
        engine.setTuning(&tuning);

        synth::VoiceContext ctx;
        ctx.tuning  = &tuning;
        ctx.wind    = &windModel;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;
        for (std::size_t i = 0; i < kPolyphony; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
            v->prepare(sr, block);
            v->setContext(ctx);
            voicePtrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        engine.bindVoices(voicePtrs.data(), voicePtrs.size());

        voicings.resize(organ.stops().size());
        for (const model::Stop& s : organ.stops())
            voicings[s.id().value] = model::buildRankVoicing(organ, s.id());

        // No master reverb: the reference is dry on purpose (see the file docs).
        engine.setMasterReverb(nullptr);
    }

    void draw(const std::vector<core::StopId>& stops)
    {
        eng::EngagedRankTable table;
        for (const core::StopId id : stops)
        {
            const synth::RankVoicing& v = voicings[id.value];
            if (v.spectrum.partials.empty() || table.count >= table.ranks.size())
                continue;
            table.ranks[table.count++] = eng::EngagedRank{ &v, v.stop, v.division,
                                                           wind::rankWindFlow(v.footage),
                                                           v.chest };
        }
        table.epoch = ++epoch;
        engine.setEngagedRanks(table);
    }

    void key(core::DivisionId division, int note, bool down, std::uint8_t velocity = 100)
    {
        const core::PipeId pipe{ 0, static_cast<std::uint8_t>(note),
                                 static_cast<std::uint8_t>(division.value) };
        (void) engine.commandQueue().push(down
            ? eng::EngineCommand::makeNoteOn(pipe, velocity, division)
            : eng::EngineCommand::makeNoteOff(pipe, division));
    }

    const model::Division& divisionOf(core::DivisionId id) const
    {
        for (const model::Division& d : organ.divisions())
            if (d.id() == id)
                return d;
        return organ.divisions().front();
    }
};

// ---------------------------------------------------------------------------
// Analysis helpers.
// ---------------------------------------------------------------------------
struct Render
{
    std::vector<float> l, r;
};

double db(double v) { return 20.0 * std::log10(v > 1e-12 ? v : 1e-12); }

double rms(const Render& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double>(x.l[i]) * x.l[i] + static_cast<double>(x.r[i]) * x.r[i];
    return to > from ? std::sqrt(s / (2.0 * static_cast<double>(to - from))) : 0.0;
}

/// RMS in consecutive windows of @p win frames, over the whole render.
std::vector<double> envelope(const Render& x, std::size_t win)
{
    std::vector<double> e;
    for (std::size_t i = 0; i + win <= x.l.size(); i += win)
        e.push_back(rms(x, i, i + win));
    return e;
}

/// Goertzel magnitude of (l+r)/2 at @p hz over [from, to), normalised so a unit
/// sine reads 1.
double goertzel(const Render& x, double sr, double hz, std::size_t from, std::size_t to)
{
    const double w = 2.0 * std::numbers::pi * hz / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    const std::size_t n = to - from;
    for (std::size_t i = from; i < to; ++i)
    {
        // Hann window: the grid is half-integer and leakage would read as content.
        const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i - from)
                                                 / static_cast<double>(n - 1));
        const double s0 = hann * 0.5 * (x.l[i] + x.r[i]) + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w);
    const double im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / (0.5 * static_cast<double>(n));
}

double correlation(const Render& x, std::size_t from, std::size_t to)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        lr += static_cast<double>(x.l[i]) * x.r[i];
        ll += static_cast<double>(x.l[i]) * x.l[i];
        rr += static_cast<double>(x.r[i]) * x.r[i];
    }
    return (ll > 0.0 && rr > 0.0) ? lr / std::sqrt(ll * rr) : 1.0;
}

void writeWav(const std::filesystem::path& path, const Render& x, double sr)
{
    caecilia::tools::WavData w;
    w.sampleRate  = sr;
    w.numChannels = 2;
    w.numFrames   = x.l.size();
    w.interleaved.resize(w.numFrames * 2);
    for (std::size_t i = 0; i < w.numFrames; ++i)
    {
        w.interleaved[2 * i]     = x.l[i];
        w.interleaved[2 * i + 1] = x.r[i];
    }
    std::string error;
    if (! caecilia::tools::WavFile::write(path.string(), w, &error))
        std::fprintf(stderr, "could not write %s: %s\n", path.string().c_str(), error.c_str());
}

std::string slug(const std::string& s)
{
    std::string out;
    for (const char c : s)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (! out.empty() && out.back() != '-')
            out.push_back('-');
    }
    while (! out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

const char* familyName(core::TonalFamily f)
{
    switch (f)
    {
        case core::TonalFamily::Principal: return "Principal";
        case core::TonalFamily::Flute:     return "Flute";
        case core::TonalFamily::String:    return "String";
        case core::TonalFamily::Reed:      return "Reed";
        case core::TonalFamily::Mixture:   return "Mixture";
        default:                           return "Other";
    }
}

// ---------------------------------------------------------------------------
// The cases.
// ---------------------------------------------------------------------------
struct Settings
{
    double                sr    = 48000.0;
    std::size_t           block = 256;
    bool                  wav   = false;
    std::filesystem::path out;
};

/// Run @p blocks blocks, calling @p perBlock(blockIndex) BEFORE each is rendered
/// (so a key pushed there lands at the top of that block) and @p after(index)
/// after it.
template <typename Before, typename After>
Render run(Instrument& inst, const Settings& s, std::size_t blocks, Before&& perBlock, After&& after)
{
    Render x;
    x.l.assign(blocks * s.block, 0.0f);
    x.r.assign(blocks * s.block, 0.0f);
    for (std::size_t b = 0; b < blocks; ++b)
    {
        perBlock(b);
        inst.tuning.adoptPending();
        float* ch[2] = { x.l.data() + b * s.block, x.r.data() + b * s.block };
        core::AudioBlock block(ch, 2, s.block);
        inst.engine.processBlock(block);
        after(b);
    }
    return x;
}

std::size_t blocksFor(const Settings& s, double seconds)
{
    return static_cast<std::size_t>(std::ceil(seconds * s.sr / static_cast<double>(s.block)));
}

void writeOrganData(Json& j, const Instrument& inst)
{
    j.open("organ", '{');
    j.str("name", inst.organ.name());

    j.open("windchests", '[');
    for (const model::Windchest& c : inst.organ.windchests())
    {
        j.open(nullptr, '{');
        j.num("id", c.id.value);
        j.str("name", c.name);
        j.num("nominalPressurePa", c.nominalPressurePa);
        j.boolean("hasTremulant", c.hasTremulant);
        j.close('}');
    }
    j.close(']');

    j.open("divisions", '[');
    for (const model::Division& d : inst.organ.divisions())
    {
        j.open(nullptr, '{');
        j.num("id", d.id().value);
        j.str("name", d.name());
        j.num("lowNote", d.lowNote());
        j.num("highNote", d.highNote());
        j.boolean("enclosed", d.isEnclosed());
        std::vector<double> chests;
        for (const core::WindchestId c : d.windchests())
            chests.push_back(c.value);
        j.array("windchests", chests);
        j.close('}');
    }
    j.close(']');

    // The voicing itself: deterministic, and the port must reproduce it exactly.
    j.open("stops", '[');
    for (const model::Stop& s : inst.organ.stops())
    {
        const synth::RankVoicing& v = inst.voicings[s.id().value];
        j.open(nullptr, '{');
        j.num("id", s.id().value);
        j.str("name", s.name());
        j.str("family", familyName(s.family()));
        j.num("footageNum", s.footage().num);
        j.num("footageDen", s.footage().den);
        j.num("division", s.division().value);
        j.num("chest", v.chest.value);
        j.num("windFlow", wind::rankWindFlow(v.footage));
        j.num("windSensitivity", v.windSensitivity);
        j.boolean("compound", s.isCompound());
        j.open("speech", '{');
        j.num("attackAtC2Sec", v.speech.attackAtC2Sec);
        j.num("attackAtC7Sec", v.speech.attackAtC7Sec);
        j.num("releaseAtC2Sec", v.speech.releaseAtC2Sec);
        j.num("releaseAtC7Sec", v.speech.releaseAtC7Sec);
        j.close('}');
        j.open("partials", '[');
        for (const synth::PartialTrack& p : v.spectrum.partials)
        {
            j.open(nullptr, '{');
            j.num("ratio", p.ratioToF0);
            j.num("rankBaseRatio", p.rankBaseRatio);
            j.num("ampDb", p.ampDb);
            j.num("windSensitivity", p.windSensitivity);
            j.num("onsetSeconds", p.onsetSeconds);
            j.num("brightnessTrack", p.brightnessTrack);
            j.num("phase", p.phase);
            j.num("seed", static_cast<double>(p.seed));
            j.num("rankRatioToF0", p.rankRatioToF0);
            j.close('}');
        }
        j.close(']');
        j.open("formants", '[');
        for (std::size_t k = 0; k < v.spectrum.steadyFormants.peakCount; ++k)
        {
            const synth::FormantPeak& f = v.spectrum.steadyFormants.peaks[k];
            j.open(nullptr, '{');
            j.num("centerHz", f.centerHz);
            j.num("gainDb", f.gainDb);
            j.num("bandwidthHz", f.bandwidthHz);
            j.close('}');
        }
        j.close(']');
        j.close('}');
    }
    j.close(']');
    j.close('}');
}

void writeTuning(Json& j)
{
    j.open("tuning", '[');
    const std::pair<core::TemperamentId, const char*> temperaments[] = {
        { core::TemperamentId::Equal, "Equal" },
        { core::TemperamentId::QuarterMeantone, "QuarterMeantone" },
        { core::TemperamentId::Werckmeister3, "Werckmeister3" },
        { core::TemperamentId::Kirnberger3, "Kirnberger3" },
        { core::TemperamentId::Pythagorean, "Pythagorean" },
        { core::TemperamentId::Young, "Young" },
    };
    for (const auto& [id, name] : temperaments)
    {
        for (const double a4 : { 440.0, 415.0 })
        {
            tun::LiveTuning t;
            t.publish(tun::makeSnapshot(id, a4));
            t.adoptPending();
            std::vector<double> hz;
            for (int n = 0; n < 128; ++n)
                hz.push_back(t.frequencyForNote(static_cast<core::MidiNote>(n)));
            j.open(nullptr, '{');
            j.str("temperament", name);
            j.num("a4", a4);
            j.array("hz", hz);
            j.close('}');
        }
    }
    j.close(']');
}

/// One stop, one key: 0.25 s of nothing, 2.0 s held, 1.25 s of release.
void writeRankNotes(Json& j, const Settings& s)
{
    constexpr double kLead = 0.25, kHold = 2.0, kTail = 1.25;
    // Fresh state per case would cost a full instrument build per note; one
    // instrument with the wind reset between cases is the same thing, since every
    // voice has fallen silent by the end of a tail.
    Instrument inst(s.sr, s.block);
    const std::size_t envWin = static_cast<std::size_t>(s.sr * 0.005); // 5 ms

    j.open("rankNotes", '[');
    for (const model::Stop& stop : inst.organ.stops())
    {
        const model::Division& div = inst.divisionOf(stop.division());
        const int lo = div.lowNote(), hi = div.highNote();
        const int notes[] = { lo, std::clamp(60, lo, hi), hi };

        for (const int note : notes)
        {
            inst.engine.reset();
            inst.windModel.reset();
            inst.draw({ stop.id() });

            const std::size_t onBlock  = blocksFor(s, kLead);
            const std::size_t offBlock = blocksFor(s, kLead + kHold);
            const std::size_t total    = blocksFor(s, kLead + kHold + kTail);
            Render x = run(inst, s, total,
                [&](std::size_t b) {
                    if (b == onBlock)  inst.key(stop.division(), note, true);
                    if (b == offBlock) inst.key(stop.division(), note, false);
                },
                [](std::size_t) {});

            const std::size_t on  = onBlock * s.block;
            const std::size_t off = offBlock * s.block;
            const std::size_t steadyFrom = off - static_cast<std::size_t>(s.sr * 1.0);
            const double steady = rms(x, steadyFrom, off);

            // Attack: first 5 ms window at or above -3 dB of the steady level.
            const std::vector<double> env = envelope(x, envWin);
            double attackMs = -1.0;
            for (std::size_t w = on / envWin; w < env.size(); ++w)
                if (env[w] >= steady * 0.70794578)
                {
                    attackMs = static_cast<double>(w * envWin + envWin - on) * 1000.0 / s.sr;
                    break;
                }
            // Release: first window after note-off at or below -40 dB of steady.
            double releaseMs = -1.0;
            for (std::size_t w = off / envWin; w < env.size(); ++w)
                if (env[w] <= steady * 0.01)
                {
                    releaseMs = static_cast<double>(w * envWin - off) * 1000.0 / s.sr;
                    break;
                }

            // Harmonic profile on a half-key grid: covers a 16' rank's fundamental
            // and every partial a mixture puts up to 24 times the key.
            const double keyHz = inst.tuning.frequencyForNote(static_cast<core::MidiNote>(note));
            std::vector<double> profile;
            for (int k = 1; k <= 48; ++k)
            {
                const double hz = keyHz * 0.5 * k;
                profile.push_back(hz < s.sr * 0.45 ? db(goertzel(x, s.sr, hz, steadyFrom, off)) : -240.0);
            }

            j.open(nullptr, '{');
            j.num("stop", stop.id().value);
            j.num("note", note);
            j.num("keyHz", keyHz);
            j.num("onFrame", static_cast<double>(on));
            j.num("offFrame", static_cast<double>(off));
            j.num("steadyRmsDb", db(steady));
            j.num("attackMs", attackMs);
            j.num("releaseMs", releaseMs);
            j.num("correlation", correlation(x, steadyFrom, off));
            j.array("halfKeyProfileDb", profile);
            j.close('}');

            if (s.wav && note == notes[1])
                writeWav(s.out / ("rank-" + std::to_string(stop.id().value) + "-"
                                  + slug(stop.name()) + "-n" + std::to_string(note) + ".wav"),
                         x, s.sr);
        }
    }
    j.close(']');
}

/// A full chord landing on the reservoir: everything drawn, middle C held, eight
/// keys added at 1.0 s, everything released at 3.0 s.
void writeWindChord(Json& j, const Settings& s)
{
    Instrument inst(s.sr, s.block);
    std::vector<core::StopId> all;
    for (const model::Stop& st : inst.organ.stops())
        all.push_back(st.id());
    inst.draw(all);

    const core::DivisionId manual = model::primaryManual(inst.organ);
    const int held = 60;
    const int more[] = { 72, 76, 79, 83, 86, 89, 91, 93 };

    const std::size_t addBlock = blocksFor(s, 1.0);
    const std::size_t offBlock = blocksFor(s, 3.0);
    const std::size_t total    = blocksFor(s, 4.5);

    std::vector<std::vector<double>> pressure(inst.organ.windchests().size());
    std::vector<double> sag, voices;
    Render x = run(inst, s, total,
        [&](std::size_t b) {
            if (b == 0) inst.key(manual, held, true);
            if (b == addBlock) for (const int n : more) inst.key(manual, n, true);
            if (b == offBlock)
            {
                inst.key(manual, held, false);
                for (const int n : more) inst.key(manual, n, false);
            }
        },
        [&](std::size_t) {
            for (std::size_t c = 0; c < pressure.size(); ++c)
                pressure[c].push_back(inst.windModel.pressureAt(
                    core::WindchestId{ static_cast<std::uint16_t>(c) }, 0));
            const auto m = inst.engine.latestMeters();
            sag.push_back(m.windSagNorm);
            voices.push_back(static_cast<double>(m.activeVoices));
        });

    j.open("windChord", '{');
    j.num("division", manual.value);
    j.num("heldNote", held);
    std::vector<double> moreD(std::begin(more), std::end(more));
    j.array("addedNotes", moreD);
    j.num("addFrame", static_cast<double>(addBlock * s.block));
    j.num("offFrame", static_cast<double>(offBlock * s.block));
    j.num("blockFrames", static_cast<double>(s.block));
    j.open("chestPressurePa", '[');
    for (const auto& p : pressure)
    {
        j.open(nullptr, '[');
        for (const double v : p) { j.num(nullptr, v); }
        j.close(']');
    }
    j.close(']');
    j.array("sagNorm", sag);
    j.array("activeVoices", voices);
    j.array("rms10msDb", [&] {
        std::vector<double> e = envelope(x, static_cast<std::size_t>(s.sr * 0.01));
        for (double& v : e) v = db(v);
        return e;
    }());
    j.close('}');

    if (s.wav)
        writeWav(s.out / "wind-chord.wav", x, s.sr);
}

/// A tremulant on a held note: 1 s plain, 3 s shaken, 1 s plain again.
void writeTremulant(Json& j, const Settings& s)
{
    Instrument inst(s.sr, s.block);

    // The first chest that carries a tremulant, and a flute (else any stop) on it.
    const model::Windchest* chest = nullptr;
    for (const model::Windchest& c : inst.organ.windchests())
        if (c.hasTremulant) { chest = &c; break; }
    if (chest == nullptr)
    {
        j.open("tremulant", '{');
        j.close('}');
        return;
    }
    const model::Stop* pick = nullptr;
    for (const model::Stop& st : inst.organ.stops())
    {
        if (inst.voicings[st.id().value].chest != chest->id)
            continue;
        if (pick == nullptr
            || (st.family() == core::TonalFamily::Flute && pick->family() != core::TonalFamily::Flute))
            pick = &st;
    }
    if (pick == nullptr)
    {
        j.open("tremulant", '{');
        j.close('}');
        return;
    }
    inst.draw({ pick->id() });

    constexpr float kRateHz = 5.5f;
    constexpr float kDepth  = 0.6f; // of the full scale, as the host parameter has it
    const float depthFraction = kDepth * wind::kMaxTremulantDepthFraction;
    const int   note = 60;

    const std::size_t onBlock  = blocksFor(s, 1.0);
    const std::size_t offBlock = blocksFor(s, 4.0);
    const std::size_t total    = blocksFor(s, 5.0);
    std::vector<double> pressure;   // every 32 frames
    const std::size_t stride = 32;
    Render x = run(inst, s, total,
        [&](std::size_t b) {
            if (b == 0) inst.key(pick->division(), note, true);
            if (b == onBlock)
                (void) inst.engine.commandQueue().push(
                    eng::EngineCommand::makeTremulant(chest->id, true, kRateHz, depthFraction));
            if (b == offBlock)
                (void) inst.engine.commandQueue().push(
                    eng::EngineCommand::makeTremulant(chest->id, false, kRateHz, depthFraction));
        },
        [&](std::size_t) {
            for (std::size_t f = 0; f < s.block; f += stride)
                pressure.push_back(inst.windModel.pressureAt(chest->id, f));
        });

    // Rate and depth over the middle two seconds of the shaken stretch.
    const std::size_t from = (onBlock * s.block + static_cast<std::size_t>(s.sr * 0.5)) / stride;
    const std::size_t to   = (onBlock * s.block + static_cast<std::size_t>(s.sr * 2.5)) / stride;
    double mean = 0.0;
    for (std::size_t i = from; i < to; ++i) mean += pressure[i];
    mean /= static_cast<double>(to - from);
    int crossings = 0;
    double pmin = 1e9, pmax = -1e9;
    for (std::size_t i = from + 1; i < to; ++i)
    {
        if ((pressure[i - 1] - mean) < 0.0 && (pressure[i] - mean) >= 0.0)
            ++crossings;
        pmin = std::min(pmin, pressure[i]);
        pmax = std::max(pmax, pressure[i]);
    }
    const double measuredRate = crossings / 2.0;

    const std::size_t envWin = static_cast<std::size_t>(s.sr * 0.005);
    const std::vector<double> env = envelope(x, envWin);
    double emin = 1e9, emax = 0.0;
    for (std::size_t w = (onBlock * s.block + static_cast<std::size_t>(s.sr * 0.5)) / envWin;
         w < (onBlock * s.block + static_cast<std::size_t>(s.sr * 2.5)) / envWin; ++w)
    {
        emin = std::min(emin, env[w]);
        emax = std::max(emax, env[w]);
    }
    const std::size_t plainFrom = static_cast<std::size_t>(s.sr * 0.5) / envWin;
    const std::size_t plainTo   = (onBlock * s.block) / envWin;
    double pmn = 1e9, pmx = 0.0;
    for (std::size_t w = plainFrom; w < plainTo; ++w)
    {
        pmn = std::min(pmn, env[w]);
        pmx = std::max(pmx, env[w]);
    }

    j.open("tremulant", '{');
    j.num("chest", chest->id.value);
    j.num("stop", pick->id().value);
    j.num("note", note);
    j.num("rateHz", kRateHz);
    j.num("depthFraction", depthFraction);
    j.num("onFrame", static_cast<double>(onBlock * s.block));
    j.num("offFrame", static_cast<double>(offBlock * s.block));
    j.num("pressureStrideFrames", static_cast<double>(stride));
    j.num("measuredRateHz", measuredRate);
    j.num("pressurePeakToPeakPa", pmax - pmin);
    j.num("envelopeSwingDb", db(emax) - db(emin));
    j.num("plainEnvelopeSwingDb", db(pmx) - db(pmn));
    j.array("pressurePa", pressure);
    std::vector<double> envDb = env;
    for (double& v : envDb) v = db(v);
    j.array("rms5msDb", envDb);
    j.close('}');

    if (s.wav)
        writeWav(s.out / ("tremulant-" + slug(pick->name()) + ".wav"), x, s.sr);
}

} // namespace

int main(int argc, char** argv)
{
    const caecilia::tools::DenormalGuard denormalGuard;
    const caecilia::tools::CliArgs args(argc, argv);

    Settings s;
    s.sr    = args.number("sr").value_or(48000.0);
    s.block = static_cast<std::size_t>(args.integer("block").value_or(256));
    s.wav   = args.has("wav");
    s.out   = args.value("out", "reference-out");
    std::filesystem::create_directories(s.out);

    const auto jsonPath = s.out / "reference.json";
    std::FILE* f = std::fopen(jsonPath.string().c_str(), "wb");
    if (f == nullptr)
    {
        std::fprintf(stderr, "cannot write %s\n", jsonPath.string().c_str());
        return 1;
    }

    Json j(f);
    j.open(nullptr, '{');
    j.str("generator", "caecilia-reference");
    j.str("caeciliaVersion", caecilia::core::versionString());
    j.num("sampleRate", s.sr);
    j.num("blockFrames", static_cast<double>(s.block));
    {
        Instrument inst(s.sr, s.block);
        writeOrganData(j, inst);
    }
    writeTuning(j);
    std::fprintf(stderr, "rank notes...\n");
    writeRankNotes(j, s);
    std::fprintf(stderr, "wind chord...\n");
    writeWindChord(j, s);
    std::fprintf(stderr, "tremulant...\n");
    writeTremulant(j, s);
    j.close('}');
    std::fputc('\n', f);
    std::fclose(f);

    std::fprintf(stderr, "wrote %s\n", jsonPath.string().c_str());
    return 0;
}
