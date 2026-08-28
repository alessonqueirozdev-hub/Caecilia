// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The wind, connected.
//
// Everything below was already true of WindModel in isolation and was true of
// nothing the instrument actually rendered: AudioEngine::setWindSupply had no
// caller anywhere in the tree, so RenderContext::wind was null, every partial's
// wind coupling read a deviation of exactly zero, AudioEngine::stepWind was an
// empty function called once a block, and SetTremulant was an unhandled case.
//
// So these do not test the wind model. They test that the ORGAN has wind — which
// is a different claim, and the one that was false.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"
#include "caecilia/model/Windchest.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/wind/OrganWind.h"
#include "caecilia/wind/WindModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace core   = caecilia::core;
namespace engine = caecilia::core::engine;
namespace model  = caecilia::model;
namespace synth  = caecilia::synth;
namespace wind   = caecilia::wind;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 256;

/// What one pipe of a rank draws at middle C -- the SAME function the plugin calls
/// when it publishes the rank table, so the rig cannot drift away from it.
float flowOf(const synth::RankVoicing& v)
{
    return wind::rankWindFlow(v.footage);
}

/// An engine with the demo organ's ranks and, optionally, its wind.
struct Rig
{
    engine::AudioEngine                                eng;
    wind::WindModel                                    wind;
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    std::vector<synth::RankVoicing>                    voicings;
    std::vector<float>                                 l, r;

    Rig(const model::Organ& organ, bool bindWind)
    {
        // The organ's real chest count. Preparing for one put every rank's demand
        // on the single chest that existed, which is a different instrument from
        // the one being tested.
        eng.prepare(kSr, kBlock, 2,
                    std::max<std::size_t>(organ.windchests().size(), 1));

        if (bindWind)
        {
            wind.prepare(kSr, kBlock);
            wind.configure(wind::configFromOrgan(organ));
            wind.reset();
            eng.setWindSupply(&wind);
        }

        for (const model::Stop& s : organ.stops())
            voicings.push_back(model::buildRankVoicing(organ, s.id()));

        for (std::size_t i = 0; i < 512; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
            v->prepare(kSr, kBlock);
            if (bindWind)
            {
                synth::VoiceContext ctx;
                ctx.wind = &wind; // re-pointed per rank by adoptRank
                v->setContext(ctx);
            }
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        eng.bindVoices(ptrs.data(), ptrs.size());
        l.assign(kBlock, 0.0f);
        r.assign(kBlock, 0.0f);
    }

    void drawAll()
    {
        engine::EngagedRankTable t;
        for (const synth::RankVoicing& v : voicings)
        {
            if (t.count >= t.ranks.size())
                break;
            t.ranks[t.count++] = engine::EngagedRank{ &v, v.stop, v.division,
                                                      flowOf(v), v.chest };
        }
        t.epoch = 1;
        eng.setEngagedRanks(t);
    }

    /// Draw at most @p limit ranks on the chest that carries the tremulant.
    ///
    /// One, for the tremulant measurement. This organ's Récit carries a Gambe and a
    /// Voix céleste, which are DESIGNED to beat against each other at a few hertz --
    /// so drawing the whole division puts a large, genuine amplitude modulation
    /// right on top of the tremulant's own frequency and buries it.
    void drawTremulantChest(const model::Organ& organ, std::size_t limit = 64)
    {
        core::WindchestId shaken{};
        bool              found = false;
        for (const model::Windchest& c : organ.windchests())
            if (c.hasTremulant)
            {
                shaken = c.id;
                found  = true;
                break;
            }
        REQUIRE(found);

        engine::EngagedRankTable t;
        for (const synth::RankVoicing& v : voicings)
        {
            if (v.chest.value != shaken.value || t.count >= limit
                || t.count >= t.ranks.size())
                continue;
            t.ranks[t.count++] = engine::EngagedRank{ &v, v.stop, v.division };
        }
        t.epoch = 1;
        eng.setEngagedRanks(t);
        REQUIRE(t.count > 0);
    }

    void send(const engine::EngineCommand& c) { (void) eng.commandQueue().push(c); }

    std::vector<float> run()
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* chans[2] = { l.data(), r.data() };
        core::AudioBlock block(chans, 2, kBlock);
        eng.processBlock(block);
        return l;
    }

    void run(int blocks)
    {
        for (int b = 0; b < blocks; ++b)
            (void) run();
    }

    /// RMS of one block.
    double level()
    {
        const std::vector<float> v = run();
        double e = 0.0;
        for (const float s : v)
            e += static_cast<double>(s) * s;
        return std::sqrt(e / static_cast<double>(v.size()));
    }
};
} // namespace

TEST_CASE("An organ's chests compile into a wind supply", "[wind][organ]")
{
    const model::Organ    organ = model::buildCaeciliaDemoOrgan();
    const wind::WindModelConfig cfg = wind::configFromOrgan(organ);

    REQUIRE(cfg.chests.size() == organ.windchests().size());
    REQUIRE(cfg.bellows.size() == 1); // one blower, which is why chests interact

    for (std::size_t i = 0; i < cfg.chests.size(); ++i)
    {
        INFO(organ.windchests()[i].name);
        CHECK(cfg.chests[i].id.value == organ.windchests()[i].id.value);
        CHECK(cfg.chests[i].nominalPressurePa
              == organ.windchests()[i].nominalPressurePa);
    }

    // One tremulant, on the chest that declares one. The Récit, on this organ.
    std::size_t declared = 0;
    for (const model::Windchest& c : organ.windchests())
        declared += c.hasTremulant ? 1u : 0u;
    CHECK(cfg.tremulants.size() == declared);

    // And every rank is routed, or it would silently fall back to chest 0 and the
    // Récit's tremulant would shake the Grand-Orgue.
    CHECK(! cfg.pipeBindings.empty());
}

TEST_CASE("An organ with no chests still gets wind", "[wind][organ]")
{
    const model::Organ    empty;
    const wind::WindModelConfig cfg = wind::configFromOrgan(empty);
    CHECK(cfg.chests.size() == 1);
    CHECK(cfg.bellows.size() == 1);
}

TEST_CASE("A full chord loads the wind and releasing it lets the wind recover",
          "[wind][organ][regression]")
{
    // The audible half of the claim. A chord draws wind; the reservoir sags; every
    // pipe already sounding gives slightly in pitch and level. That is the
    // difference between an organ and a synthesiser playing organ samples, and
    // until the supply was bound it did not happen at all.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    Rig                rig{ organ, /*bindWind*/ true };
    rig.drawAll();
    rig.run(1);

    // One note, settled: the reference the chord will pull away from.
    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                               core::DivisionId{ 1 }, 0));
    rig.run(60);

    const float restingPa = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
    REQUIRE(restingPa > 0.0f);

    // A full chord across the compass.
    for (int note = 36; note <= 84; note += 4)
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 }, 100,
            core::DivisionId{ 1 }, 0));
    rig.run(120);

    const float loadedPa = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
    INFO("resting " << restingPa << " Pa, loaded " << loadedPa << " Pa, "
         << rig.eng.activeVoiceCount() << " voices");
    CHECK(loadedPa < restingPa); // the wind gives

    // Release it and the reservoir refills.
    for (int note = 36; note <= 84; note += 4)
        rig.send(engine::EngineCommand::makeNoteOff(
            core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 },
            core::DivisionId{ 1 }, 0));
    rig.run(400);

    const float recoveredPa = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
    INFO("recovered to " << recoveredPa << " Pa");
    CHECK(recoveredPa > loadedPa);

    // The sag is a few percent, not a collapse: this is a well-winded instrument,
    // not a harmonium with a hole in the bellows.
    const double sagPercent = 100.0 * (restingPa - loadedPa) / restingPa;
    INFO("sag " << sagPercent << "%");
    CHECK(sagPercent > 0.2);
    CHECK(sagPercent < 12.0);
}

TEST_CASE("A chord lands on the wind rather than merely lowering it",
          "[wind][organ][regression]")
{
    // The reservoir plate has mass, so the instrument does not slide to a new
    // pressure -- it drops past and comes back, a few times, at about four hertz.
    // Every pipe on that reservoir goes flat, quiet and dull in time with the
    // bounce, because pitch, level, brightness and speech all track the deviation.
    //
    // A first-order bellows can sag convincingly and can never do this, and that is
    // the difference between an organ that gives under the hands and one that fades.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    Rig                rig{ organ, /*bindWind*/ true };
    rig.drawAll();
    rig.run(40); // settle at nominal

    const float restingPa = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
    REQUIRE(restingPa > 0.0f);

    // A full chord, landing all at once, and then held while the plate settles.
    for (int note = 36; note <= 84; note += 4)
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 }, 100,
            core::DivisionId{ 1 }, 0));

    std::vector<float> trace;
    trace.reserve(600);
    for (int i = 0; i < 600; ++i)
    {
        rig.run(1);
        trace.push_back(rig.wind.pressureAt(core::WindchestId{ 0 }, 0));
    }

    const float lowest  = *std::min_element(trace.begin(), trace.end());
    const float settled = trace.back();

    // It went BELOW where it settles -- that is the whole claim, and a first-order
    // reservoir cannot produce it: monotone approach never passes its target.
    INFO("resting " << restingPa << " Pa, trough " << lowest << " Pa, settled "
                    << settled << " Pa");
    CHECK(lowest < settled);

    // And came back up past the trough, which is the bounce rather than a drift.
    const auto troughAt = static_cast<std::size_t>(
        std::min_element(trace.begin(), trace.end()) - trace.begin());
    REQUIRE(troughAt + 1 < trace.size());
    const float reboundPeak = *std::max_element(trace.begin() + static_cast<long>(troughAt),
                                                trace.end());
    CHECK(reboundPeak > lowest);

    // The overshoot is a real fraction of the sag, not a rounding wobble: at the
    // demo organ's damping of 0.45 the plate passes its target by about a fifth of
    // the step.
    const float step = settled - lowest;
    const float sag  = restingPa - settled;
    //
    // Measured on the demo organ: it rests at 980 Pa, a full chord settles it at
    // 966.8, and the plate passes that on the way down to 964.1 -- 2.70 Pa beyond
    // a 13.18 Pa sag, which is the 20.5% the theory gives for a damping of 0.45.
    INFO("sag " << sag << " Pa, overshoot past it " << step << " Pa");
    REQUIRE(sag > 0.0f);
    CHECK(step > sag * 0.05f);

    // Still a well-winded instrument: the bounce must not take it somewhere a
    // reservoir would never go.
    CHECK(lowest > restingPa * 0.85f);
}

TEST_CASE("The tremulant reaches the pipes", "[wind][organ][tremulant]")
{
    // The console has been calling caeciliaSetTremulant since the page was written.
    // The name was not registered in the editor, so the call was dropped; and even
    // registered, SetTremulant was an unhandled case in the engine standing beside
    // a null wind pointer. Three separate breaks in one wire, and the switch has
    // never made a sound.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    core::WindchestId shaken{};
    for (const model::Windchest& c : organ.windchests())
        if (c.hasTremulant)
            shaken = c.id;

    Rig rig{ organ, /*bindWind*/ true };
    rig.drawTremulantChest(organ, 1);
    rig.run(1);

    // The division the shaken chest actually feeds -- derived, not assumed: this
    // organ numbers its divisions Pedale, Grand-Orgue, Recit, and a hard-coded
    // guess plays a keyboard that draws nothing.
    core::DivisionId shakenDivision{};
    for (const synth::RankVoicing& v : rig.voicings)
        if (v.chest.value == shaken.value)
        {
            shakenDivision = v.division;
            break;
        }
    INFO("tremulant chest " << shaken.value << " feeds division "
                            << shakenDivision.value);

    rig.send(engine::EngineCommand::makeNoteOn(
        core::PipeId{ 0, 60, static_cast<std::uint8_t>(shakenDivision.value) }, 100,
        shakenDivision, 0));
    rig.run(80); // past the speech
    REQUIRE(rig.eng.activeVoiceCount() > 0);

    // Measured at the tremulant's own FREQUENCY, not as a peak-to-peak swing.
    //
    // A sounding rank already wanders by about three decibels block to block --
    // that is the per-partial drift and the beating between ranks, the "living
    // pipe" behaviour -- and a tremulant of a few tenths of a decibel disappears
    // into it completely. A single-bin DFT of the level envelope at the tremulant
    // rate separates the two: the drift is broadband, the tremulant is not.
    const auto envelopeAt = [&rig](double rateHz, int blocks)
    {
        std::vector<double> envelope;
        envelope.reserve(static_cast<std::size_t>(blocks));
        for (int b = 0; b < blocks; ++b)
            envelope.push_back(rig.level());

        double mean = 0.0;
        for (const double v : envelope)
            mean += v;
        mean /= static_cast<double>(envelope.size());
        if (mean <= 0.0)
            return 0.0;

        // Envelope sample rate is one per block.
        const double envSr = kSr / static_cast<double>(kBlock);
        const double w     = 2.0 * 3.14159265358979323846 * rateHz / envSr;
        const double coef  = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (const double v : envelope)
        {
            const double s0 = (v - mean) + coef * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coef * s1 * s2;
        // Modulation depth as a fraction of the mean level.
        return std::sqrt(std::max(power, 0.0)) * 2.0
             / (static_cast<double>(envelope.size()) * mean);
    };

    constexpr double kRate = 5.5;

    // Before anything subtle: is the CHEST moving at all? If the pressure is flat
    // the tremulant never arrived, and no measurement of the audio can tell that
    // apart from a tremulant that arrived and did nothing.
    {
        double lo = 1e9, hi = -1e9;
        for (int b = 0; b < 120; ++b)
        {
            rig.run(1);
            const float pa = rig.wind.pressureAt(shaken, 0);
            lo = std::min(lo, static_cast<double>(pa));
            hi = std::max(hi, static_cast<double>(pa));
        }
        INFO("chest pressure before the tremulant: " << lo << " .. " << hi << " Pa");
        CHECK(hi - lo < 1.0); // steady

        rig.send(engine::EngineCommand::makeTremulant(shaken, true, static_cast<float>(kRate),
                                                      wind::TremulantConfig{}.depthFraction));
        rig.run(60);

        double lo2 = 1e9, hi2 = -1e9;
        for (int b = 0; b < 120; ++b)
        {
            rig.run(1);
            const float pa = rig.wind.pressureAt(shaken, 0);
            lo2 = std::min(lo2, static_cast<double>(pa));
            hi2 = std::max(hi2, static_cast<double>(pa));
        }
        INFO("chest pressure with the tremulant: " << lo2 << " .. " << hi2 << " Pa");
        CHECK(hi2 - lo2 > 10.0); // shaking

        // Back off, so the level measurement below starts from a still instrument.
        // Leaving it running made the "steady" reading a second tremulant reading,
        // and the comparison came out at 0.78x -- the two measurements were of the
        // same thing, differing only by drift.
        rig.send(engine::EngineCommand::makeTremulant(shaken, false, static_cast<float>(kRate),
                                                      wind::TremulantConfig{}.depthFraction));
        rig.run(200); // the modulation dies away
    }

    // Steady: whatever the drift happens to put at 5.5 Hz, which is the floor.
    const double still = envelopeAt(kRate, 256);

    rig.send(engine::EngineCommand::makeTremulant(shaken, true, static_cast<float>(kRate),
                                                  wind::TremulantConfig{}.depthFraction));
    rig.run(60); // let it get going
    const double shaking = envelopeAt(kRate, 256);

    INFO("modulation at " << kRate << " Hz: steady " << still
         << ", tremulant " << shaking << " (x" << (shaking / std::max(still, 1e-12)) << ")");
    CHECK(shaking > still * 3.0);
}

TEST_CASE("Without a wind supply the engine still renders", "[wind][organ]")
{
    // The supply is optional and the null path has to stay silent about it: a
    // headless tool that never binds one must not crash, and must not go quiet.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    Rig                rig{ organ, /*bindWind*/ false };
    rig.drawAll();
    rig.run(1);
    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                               core::DivisionId{ 1 }, 0));
    rig.run(40);

    CHECK(rig.level() > 0.0);
}

TEST_CASE("Tremulant depth is a live control, and zero means still",
          "[wind][organ][tremulant]")
{
    // The host's Tremulant Depth parameter defaulted to 0.0, so drawing the
    // tremulant asked for a modulation of nothing: switch on, knob present, and the
    // instrument did not move. And rate and depth reached WindModel only through
    // configure(), which calls reset() -- so even a non-zero default could not have
    // been turned, because turning it would have zeroed the modulation phase and
    // stepped the pressure of every pipe on the chest.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    core::WindchestId shaken{};
    for (const model::Windchest& c : organ.windchests())
        if (c.hasTremulant)
            shaken = c.id;

    wind::WindModel model;
    model.prepare(kSr, kBlock);
    model.configure(wind::configFromOrgan(organ));
    model.reset();

    const auto swingPa = [&model, shaken](int blocks)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (int b = 0; b < blocks; ++b)
        {
            model.step(kBlock);
            const double pa = model.pressureAt(shaken, 0);
            lo = std::min(lo, pa);
            hi = std::max(hi, pa);
        }
        return hi - lo;
    };

    model.setChestTremulantShape(shaken, 5.5f, 0.0f);
    model.setChestTremulantEnabled(shaken, true);
    (void) swingPa(60); // let the ramps settle
    const double atZero = swingPa(120);
    INFO("swing at depth 0: " << atZero << " Pa");
    CHECK(atZero < 1.0);

    model.setChestTremulantShape(shaken, 5.5f, wind::kMaxTremulantDepthFraction);
    (void) swingPa(60);
    const double atFull = swingPa(120);
    INFO("swing at full depth: " << atFull << " Pa");

    // A quarter of chest nominal, peak to peak, less whatever the ramp has not
    // reached. Nominal here is 735 Pa, so this is on the order of 300 Pa.
    CHECK(atFull > 100.0);
    CHECK(atFull > atZero * 20.0);

    // And halving the depth roughly halves the swing, which is what makes it a
    // control rather than a switch with extra steps.
    model.setChestTremulantShape(shaken, 5.5f, wind::kMaxTremulantDepthFraction * 0.5f);
    (void) swingPa(60);
    const double atHalf = swingPa(120);
    INFO("swing at half depth: " << atHalf << " Pa (full was " << atFull << ")");
    CHECK(atHalf < atFull * 0.75);
    CHECK(atHalf > atFull * 0.25);
}


TEST_CASE("A big pipe draws more wind than a small one",
          "[wind][organ][regression]")
{
    // The demand tally used to count VOICES, so every sounding pipe booked the same
    // flow: ten notes on a 2' Doublette loaded the reservoir exactly as hard as ten
    // on a 16' Bombarde. On a real instrument those are not comparable, and the
    // difference is most of the reason an organist hears the wind at all -- it is
    // the pedal department, on the big flues and reeds, that makes a chord breathe.
    //
    // Flow scales with the pipe, and a pipe scales with the wavelength it sounds.
    // So a rank's footage and the note both matter, and this holds the two of them
    // to it.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // The same chord, on the deepest rank the organ has and on the shallowest.
    const auto sagFor = [&organ](bool deepRank, int lowestNote)
    {
        Rig rig{ organ, /*bindWind*/ true };

        const synth::RankVoicing* chosen = nullptr;
        for (const synth::RankVoicing& v : rig.voicings)
        {
            if (v.spectrum.partials.empty())
                continue;
            if (chosen == nullptr
                || (deepRank  && v.footage.feet() > chosen->footage.feet())
                || (!deepRank && v.footage.feet() < chosen->footage.feet()))
                chosen = &v;
        }
        REQUIRE(chosen != nullptr);

        engine::EngagedRankTable table;
        table.ranks[table.count++] = engine::EngagedRank{ chosen, chosen->stop,
                                                          chosen->division,
                                                          flowOf(*chosen) };
        table.epoch = 1;
        rig.eng.setEngagedRanks(table);
        rig.run(1);

        const float resting = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);

        for (int n = 0; n < 8; ++n)
            rig.send(engine::EngineCommand::makeNoteOn(
                core::PipeId{ 0, static_cast<std::uint8_t>(lowestNote + n * 2),
                              static_cast<std::uint8_t>(chosen->division.value) },
                100, chosen->division, 0));
        rig.run(200);

        const float loaded = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
        INFO(chosen->footage.feet() << "' rank, div " << int(chosen->division.value)
             << ", from MIDI " << lowestNote << ": " << rig.eng.activeVoiceCount()
             << " voices, " << resting << " -> " << loaded << " Pa, flow "
             << flowOf(*chosen));
        return static_cast<double>(resting - loaded);
    };

    // A deep rank in the bass against a shallow one in the treble: the two extremes
    // an organ actually spans.
    const double heavy = sagFor(/*deepRank*/ true,  36);
    const double light = sagFor(/*deepRank*/ false, 72);

    INFO("bass on the deepest rank sags " << heavy << " Pa; treble on the "
         "shallowest sags " << light << " Pa");
    // Measured: eight notes from MIDI 36 on the 16' sag the reservoir 3.53 Pa;
    // eight from MIDI 72 on the 1 3/5' sag it 0.043. A ratio of eighty.
    //
    // Ten is the assertion because the failure this guards is flow going back to a
    // constant, and a constant makes the ratio exactly ONE -- same eight voices
    // either side. Anything comfortably above one states the physics; ten states it
    // without being brittle about the exact ranks this organ happens to have.
    CHECK(heavy > 0.0);
    CHECK(light > 0.0);
    CHECK(heavy > light * 10.0);
}


TEST_CASE("The same rank draws more wind in the bass than in the treble",
          "[wind][organ][regression]")
{
    // The footage half of the demand figure and the NOTE half are separate claims,
    // and the previous case can only see them together. This isolates the note: one
    // rank, one count of keys, two ends of the compass.
    //
    // It is the physically obvious half and the easy one to drop, because a rank's
    // footage is a property somebody has to look up while the note is right there.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const auto sagAt = [&organ](int lowestNote)
    {
        Rig rig{ organ, /*bindWind*/ true };

        // The organ's 8' Montre: an ordinary unison rank with a full compass, so
        // both ends of the test are pipes it really has.
        const synth::RankVoicing* chosen = nullptr;
        for (const synth::RankVoicing& v : rig.voicings)
            if (! v.spectrum.partials.empty() && v.division.value == 1
                && v.family == core::TonalFamily::Principal
                && v.footage.feet() == 8.0)
            {
                chosen = &v;
                break;
            }
        REQUIRE(chosen != nullptr);

        engine::EngagedRankTable table;
        table.ranks[table.count++] = engine::EngagedRank{ chosen, chosen->stop,
                                                          chosen->division,
                                                          flowOf(*chosen) };
        table.epoch = 1;
        rig.eng.setEngagedRanks(table);
        rig.run(1);

        const float resting = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
        for (int n = 0; n < 8; ++n)
            rig.send(engine::EngineCommand::makeNoteOn(
                core::PipeId{ 0, static_cast<std::uint8_t>(lowestNote + n * 2), 1 },
                100, core::DivisionId{ 1 }, 0));
        rig.run(200);

        const float loaded = rig.wind.pressureAt(core::WindchestId{ 0 }, 0);
        INFO("MIDI " << lowestNote << "..: " << rig.eng.activeVoiceCount()
             << " voices, " << resting << " -> " << loaded << " Pa");
        return static_cast<double>(resting - loaded);
    };

    const double bass   = sagAt(36);
    const double treble = sagAt(72);

    INFO("same rank, same eight keys: bass sags " << bass << " Pa, treble "
         << treble << " Pa");
    CHECK(bass > 0.0);
    CHECK(treble > 0.0);

    // Three octaves apart, so eight times the wavelength and eight times the flow.
    // Four is the assertion: comfortably above the ONE that dropping the note term
    // would give, and not brittle about the reservoir's exact response curve.
    CHECK(bass > treble * 4.0);
}


TEST_CASE("A rank's wind draw is its length in eight-foot units",
          "[wind][organ]")
{
    // Small enough to state exactly, and worth stating: this is the number the
    // plugin puts on every rank of the table, and getting it inverted -- shorter
    // pipes drawing more -- would still produce an instrument that sags and
    // recovers, just one whose treble empties the bellows.
    CHECK(wind::rankWindFlow(core::footage::kSixteen) == 2.0f);
    CHECK(wind::rankWindFlow(core::footage::kEight)   == 1.0f);
    CHECK(wind::rankWindFlow(core::footage::kFour)    == 0.5f);
    CHECK(wind::rankWindFlow(core::footage::kTwo)     == 0.25f);

    // Mutations are their real length, not their nominal rank position.
    CHECK(wind::rankWindFlow(core::footage::kTwoAndTwoThird)
          == Catch::Approx(1.0 / 3.0).margin(1e-6));

    // A rank with no declared length still draws wind rather than none.
    CHECK(wind::rankWindFlow(core::Footage{ 0, 1 }) == 1.0f);
}


TEST_CASE("Each chest keeps the pressure it was voiced at", "[wind][organ][regression]")
{
    // Organ builders wind different divisions at different pressures on purpose,
    // and this organ says so: 980 Pa on the Pédale, 812 on the Grand-Orgue, 735 on
    // the Récit. Those numbers are a voicing decision, not a detail.
    //
    // A chest used to take the RESERVOIR's pascals directly, so the moment any wind
    // was drawn all three converged on the same figure -- measured at 967 / 967 /
    // 967 -- and every division was suddenly winded alike. A chest sits behind a
    // regulator that holds it at ITS pressure and passes the trunk's movement
    // through as a fraction, which is what it does now.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    REQUIRE(organ.windchests().size() >= 3);

    wind::WindModel model;
    model.prepare(kSr, kBlock);
    model.configure(wind::configFromOrgan(organ));
    model.reset();

    const auto pressures = [&model, &organ]()
    {
        std::vector<double> out;
        for (std::size_t c = 0; c < organ.windchests().size(); ++c)
            out.push_back(model.pressureAt(
                core::WindchestId{ static_cast<std::uint16_t>(c) }, 0));
        return out;
    };

    // At rest, each chest is at its own nominal.
    const std::vector<double> resting = pressures();
    for (std::size_t c = 0; c < resting.size(); ++c)
    {
        INFO(organ.windchests()[c].name);
        CHECK(resting[c]
              == Catch::Approx(organ.windchests()[c].nominalPressurePa).margin(0.5));
    }

    // A heavy load on ONE chest only.
    constexpr std::size_t kLoaded = 1;
    for (int b = 0; b < 400; ++b)
    {
        model.registerDemand(core::WindchestId{ kLoaded }, 50.0f);
        model.step(kBlock);
    }
    const std::vector<double> loaded = pressures();

    for (std::size_t c = 0; c < loaded.size(); ++c)
    {
        const double sagPercent = 100.0 * (resting[c] - loaded[c]) / resting[c];
        INFO(organ.windchests()[c].name << ": " << resting[c] << " -> " << loaded[c]
             << " Pa (" << sagPercent << "%)");

        // Every chest feels it -- one blower, one reservoir, and that shared
        // reservoir is the whole reason a heavy pedal chord makes the Récit flinch.
        CHECK(sagPercent > 0.2);

        // And none of them is dragged to another's pressure. The chest that drew
        // the wind takes its own trunk drop on top, so it sags furthest.
        CHECK(loaded[c] < resting[c]);
    }

    // The ordering survives: the Pédale is still the highest-pressure chest and the
    // Récit still the lowest, which is what "voiced at" means.
    for (std::size_t c = 1; c < loaded.size(); ++c)
        CHECK(loaded[c] < loaded[c - 1]);

    // The loaded chest sags further than its neighbours, in proportion.
    const double sagLoaded = 100.0 * (resting[kLoaded] - loaded[kLoaded]) / resting[kLoaded];
    const double sagOther  = 100.0 * (resting[0] - loaded[0]) / resting[0];
    INFO("loaded chest sags " << sagLoaded << "%, an unloaded one " << sagOther << "%");
    CHECK(sagLoaded > sagOther);
}
