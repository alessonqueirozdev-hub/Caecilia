// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The swell box, as a box.
//
// The expression pedal has always applied a gain, and RenderContext carried the
// admission that a flat gain reads as a volume pedal rather than as a lid: a real
// shutter takes far more treble than bass, and it is the DULLING the ear reads as
// a box closing, not the loss of level.
//
// Two things had to exist first. The buses are per windchest, so the plugin had to
// stop asking the engine for one chest -- with a single bus every voice of the
// instrument accumulated into it whatever fed it, and the routing the scheduler
// already did had nowhere to route to. And the filter had to be per chest rather
// than per voice, or it would be a biquad on the hot path, per note, forever.
//
// So the assertion here is not "closing the shoe makes it quieter". That was
// already true and is the half that was never the problem. It is that closing the
// shoe makes it quieter AT THE TOP FASTER THAN AT THE BOTTOM.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/wind/OrganWind.h"
#include "caecilia/wind/WindModel.h"

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
constexpr double           kTwoPi = 6.283185307179586476925286766559;

/// Energy at one frequency, by Goertzel over a HANN-WINDOWED buffer.
///
/// The window is not a nicety. A rectangular Goertzel's sidelobes fall off very
/// slowly, and the upper harmonics being measured here sit forty-odd decibels
/// below the fundamental -- so an unwindowed probe at the twelfth harmonic returns
/// leakage from the first, not energy at the twelfth. Measured that way, closing
/// the shutter appeared to cost the same at both ends of the spectrum whatever
/// cutoff it used, because both readings were the same low-frequency energy.
///
/// Hann drops the first sidelobe to -31 dB and rolls off fast, which is the
/// separation this needs.
double toneAt(const std::vector<float>& x, double hz)
{
    const std::size_t n = x.size();
    if (n < 4)
        return 0.0;

    const double w    = kTwoPi * hz / kSr;
    const double coef = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i)
                                                 / static_cast<double>(n - 1)));
        const double s0 = static_cast<double>(x[i]) * win + coef * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coef * s1 * s2;
}

/// An engine with the demo organ's ranks, its wind, and its swell boxes.
struct Rig
{
    engine::AudioEngine                                eng;
    wind::WindModel                                    wind;
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    std::vector<synth::RankVoicing>                    voicings;
    std::vector<float>                                 l, r;

    explicit Rig(const model::Organ& organ, bool declareEnclosures = true)
    {
        // The organ's real chest count, which is the whole point: with one chest
        // every voice lands on bus 0 and the shutter has nothing to be per.
        eng.prepare(kSr, kBlock, 2,
                    std::max<std::size_t>(organ.windchests().size(), 1));

        wind.prepare(kSr, kBlock);
        wind.configure(wind::configFromOrgan(organ));
        wind.reset();
        eng.setWindSupply(&wind);

        std::vector<engine::ChestEnclosure> enclosed;
        if (declareEnclosures)
            for (const model::Division& d : organ.divisions())
                if (d.isEnclosed())
                    for (const core::WindchestId c : d.windchests())
                        enclosed.push_back(engine::ChestEnclosure{ c, d.id() });
        eng.setEnclosedChests(enclosed);

        for (const model::Stop& s : organ.stops())
            voicings.push_back(model::buildRankVoicing(organ, s.id()));

        for (std::size_t i = 0; i < 256; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
            v->prepare(kSr, kBlock);
            synth::VoiceContext ctx;
            ctx.wind = &wind;
            v->setContext(ctx);
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        eng.bindVoices(ptrs.data(), ptrs.size());
        l.assign(kBlock, 0.0f);
        r.assign(kBlock, 0.0f);
    }

    /// Draw every rank of one division.
    core::DivisionId draw(const model::Organ& organ, bool wantEnclosed)
    {
        core::DivisionId chosen{};
        bool             found = false;
        for (const model::Division& d : organ.divisions())
            if (d.isEnclosed() == wantEnclosed && ! d.windchests().empty())
            {
                chosen = d.id();
                found  = true;
                break;
            }
        REQUIRE(found);

        engine::EngagedRankTable t;
        for (const synth::RankVoicing& v : voicings)
        {
            if (v.division.value != chosen.value || t.count >= t.ranks.size())
                continue;
            t.ranks[t.count++] = engine::EngagedRank{ &v, v.stop, v.division,
                                                      wind::rankWindFlow(v.footage) };
        }
        t.epoch = 1;
        eng.setEngagedRanks(t);
        REQUIRE(t.count > 0);
        return chosen;
    }

    void send(const engine::EngineCommand& c) { (void) eng.commandQueue().push(c); }

    void run(int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* ch[2] = { l.data(), r.data() };
            core::AudioBlock blk(ch, 2, kBlock);
            eng.processBlock(blk);
        }
    }

    /// Render and collect, past whatever the shoe was doing.
    std::vector<float> collect(int blocks)
    {
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(blocks) * kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* ch[2] = { l.data(), r.data() };
            core::AudioBlock blk(ch, 2, kBlock);
            eng.processBlock(blk);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    }
};

/// Energy low in the spectrum and high in it, for a note at @p f0.
struct Bands
{
    double low  = 0.0;
    double high = 0.0;

    [[nodiscard]] double tiltDb() const
    {
        return 10.0 * std::log10((high + 1e-30) / (low + 1e-30));
    }
};

Bands bandsOf(const std::vector<float>& x, double f0)
{
    Bands b;
    for (const int h : { 1, 2, 3 })
        b.low += toneAt(x, f0 * h);
    for (const int h : { 10, 12, 14, 16, 18 })
        b.high += toneAt(x, f0 * h);
    return b;
}
} // namespace

TEST_CASE("Closing the swell takes the treble faster than the bass",
          "[engine][swell][regression]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Middle C on the enclosed division: low enough that its tenth to eighteenth
    // harmonics are still well inside the band, which is where a shutter bites.
    constexpr core::MidiNote kNote = 60;
    const double f0 = 261.6255653;

    const auto render = [&](float shoe)
    {
        Rig rig{ organ };
        const core::DivisionId div = rig.draw(organ, /*wantEnclosed*/ true);

        rig.send(engine::EngineCommand::makeExpression(div, shoe));
        rig.run(1);
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, kNote, static_cast<std::uint8_t>(div.value) }, 100, div, 0));

        rig.run(200); // past the speech AND past the shoe's glide
        return rig.collect(64);
    };

    const std::vector<float> open   = render(1.0f);
    const std::vector<float> closed = render(0.0f);

    const Bands bOpen   = bandsOf(open, f0);
    const Bands bClosed = bandsOf(closed, f0);

    REQUIRE(bOpen.low > 0.0);
    REQUIRE(bOpen.high > 0.0);

    const double lowLossDb  = 10.0 * std::log10((bClosed.low + 1e-30) / bOpen.low);
    const double highLossDb = 10.0 * std::log10((bClosed.high + 1e-30) / bOpen.high);

    INFO("closing the box costs " << lowLossDb << " dB at the bottom and "
         << highLossDb << " dB at the top");

    // Both fall -- a shutter is still a shutter.
    CHECK(lowLossDb  < -3.0);
    CHECK(highLossDb < -3.0);

    // And the top falls MUCH further. That difference is the whole claim: without
    // it the pedal is a fader, and the note this test exists to keep true is the
    // one in RenderContext saying so.
    INFO("spectral tilt: " << (lowLossDb - highLossDb) << " dB more loss up top");
    CHECK(highLossDb < lowLossDb - 6.0);
}

TEST_CASE("An unenclosed division is not filtered", "[engine][swell]")
{
    // A shutter on a division that has no box would dull the Grand-Orgue for no
    // reason, and nothing about the sound would say why. The engine only filters
    // chests it was told are enclosed, and this is what says so.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    constexpr core::MidiNote kNote = 60;
    const double f0 = 261.6255653;

    const auto render = [&](float shoe)
    {
        Rig rig{ organ };
        const core::DivisionId div = rig.draw(organ, /*wantEnclosed*/ false);

        rig.send(engine::EngineCommand::makeExpression(div, shoe));
        rig.run(1);
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, kNote, static_cast<std::uint8_t>(div.value) }, 100, div, 0));
        rig.run(200);
        return rig.collect(64);
    };

    const Bands bOpen   = bandsOf(render(1.0f), f0);
    const Bands bClosed = bandsOf(render(0.0f), f0);
    REQUIRE(bOpen.low > 0.0);

    const double lowLossDb  = 10.0 * std::log10((bClosed.low + 1e-30) / bOpen.low);
    const double highLossDb = 10.0 * std::log10((bClosed.high + 1e-30) / bOpen.high);

    // The flat gain still applies -- an unenclosed division with a shoe assigned to
    // it is unusual but not forbidden, and the engine does not second-guess it.
    // What must NOT happen is the spectral tilt.
    INFO("unenclosed: " << lowLossDb << " dB at the bottom, " << highLossDb
         << " at the top");
    CHECK(std::abs(highLossDb - lowLossDb) < 2.0);
}

TEST_CASE("A shoe left open colours nothing", "[engine][swell]")
{
    // The shutter is always running on an enclosed chest rather than switched in
    // and out, because switching a filter under a held chord is a click. Open, it
    // therefore has to be inaudible -- otherwise every enclosed division is quietly
    // dulled for the whole life of the instrument and nobody would connect the two.
    //
    // Compared against the SAME division with no enclosure declared, which is the
    // only comparison that isolates the shutter. An earlier version of this asserted
    // that the open division's high harmonics sit within some number of decibels of
    // its low ones -- which measures the ranks' own spectrum, not the filter, and
    // would have passed or failed on how bright the Recit happens to be voiced.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    constexpr core::MidiNote kNote = 60;
    const double f0 = 261.6255653;

    const auto render = [&](bool declareEnclosures)
    {
        Rig rig{ organ, declareEnclosures };
        const core::DivisionId div = rig.draw(organ, /*wantEnclosed*/ true);

        rig.send(engine::EngineCommand::makeExpression(div, 1.0f)); // wide open
        rig.run(1);
        rig.send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, kNote, static_cast<std::uint8_t>(div.value) },
            100, div, 0));
        rig.run(200);
        return bandsOf(rig.collect(64), f0);
    };

    const Bands filtered = render(/*declareEnclosures*/ true);
    const Bands bypassed = render(/*declareEnclosures*/ false);

    REQUIRE(bypassed.low > 0.0);
    REQUIRE(bypassed.high > 0.0);

    const double lowDb  = 10.0 * std::log10((filtered.low + 1e-30) / bypassed.low);
    const double highDb = 10.0 * std::log10((filtered.high + 1e-30) / bypassed.high);

    INFO("open shutter versus none: " << lowDb << " dB at the bottom, "
         << highDb << " dB at the top");

    // An 18 kHz one-pole is not literally transparent, and pretending otherwise
    // would be a tolerance chosen to pass. Half a decibel at the bottom and a
    // decibel and a half up at the eighteenth harmonic is what it costs, against
    // the fourteen and twenty-four the closed box takes.
    CHECK(std::abs(lowDb)  < 0.5);
    CHECK(std::abs(highDb) < 1.5);
}
