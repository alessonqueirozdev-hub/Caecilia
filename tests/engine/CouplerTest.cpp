// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Couplers.
//
// Coupler.h has said it plainly since it was written: nothing applies a coupler,
// RegistrationState::engagedCouplers() has no readers, and a drawn coupler is
// silent. The demo organ declares three and none of them has ever done anything.
//
// For a romantic organ that is not a missing feature so much as a missing
// instrument: coupling is how an organist builds sound across the manuals, and
// without it the Récit can only be played by the hand that is on the Récit.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/model/Coupler.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/Organ.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace core   = caecilia::core;
namespace engine = caecilia::core::engine;
namespace model  = caecilia::model;
namespace synth  = caecilia::synth;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 256;

struct Rig
{
    engine::AudioEngine                                eng;
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    std::vector<synth::RankVoicing>                    voicings;
    std::vector<float>                                 l, r;
    engine::EngagedRankTable                           table;
    std::uint32_t                                      epoch = 0;

    explicit Rig(const model::Organ& organ)
    {
        eng.prepare(kSr, kBlock, 2, std::max<std::size_t>(organ.windchests().size(), 1));

        for (const model::Stop& s : organ.stops())
            voicings.push_back(model::buildRankVoicing(organ, s.id()));

        for (std::size_t i = 0; i < 512; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
            v->prepare(kSr, kBlock);
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        eng.bindVoices(ptrs.data(), ptrs.size());
        l.assign(kBlock, 0.0f);
        r.assign(kBlock, 0.0f);
    }

    /// Draw every rank of @p divisions.
    void drawDivisions(std::initializer_list<std::uint16_t> divisions)
    {
        table.count = 0;
        for (const synth::RankVoicing& v : voicings)
        {
            bool wanted = false;
            for (const std::uint16_t d : divisions)
                wanted = wanted || v.division.value == d;
            if (! wanted || table.count >= table.ranks.size())
                continue;
            table.ranks[table.count++] =
                engine::EngagedRank{ &v, v.stop, v.division, 1.0f, v.chest };
        }
        publish();
    }

    void drawCoupler(std::uint16_t from, std::uint16_t to, std::int16_t semitones = 0)
    {
        if (table.couplerCount < table.couplers.size())
            table.couplers[table.couplerCount++] =
                engine::EngagedCoupler{ core::DivisionId{ from },
                                        core::DivisionId{ to }, semitones };
        publish();
    }

    void clearCouplers()
    {
        table.couplerCount = 0;
        publish();
    }

    void publish()
    {
        table.epoch = ++epoch;
        eng.setEngagedRanks(table);
    }

    /// How many ranks are drawn on one division.
    [[nodiscard]] std::size_t ranksOn(std::uint16_t division) const
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < table.count; ++i)
            n += table.ranks[i].division.value == division ? 1u : 0u;
        return n;
    }

    void send(const engine::EngineCommand& c) { (void) eng.commandQueue().push(c); }

    void noteOn(std::uint8_t note, std::uint16_t division)
    {
        send(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, note, static_cast<std::uint8_t>(division) }, 100,
            core::DivisionId{ division }, 0));
    }

    void noteOff(std::uint8_t note, std::uint16_t division)
    {
        send(engine::EngineCommand::makeNoteOff(
            core::PipeId{ 0, note, static_cast<std::uint8_t>(division) },
            core::DivisionId{ division }, 0));
    }

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

    [[nodiscard]] std::size_t active() const { return eng.activeVoiceCount(); }
};

/// The organ's own divisions, by role rather than by number.
struct Divisions
{
    std::uint16_t pedal = 0, go = 0, recit = 0;
};

Divisions divisionsOf(const model::Organ& organ)
{
    Divisions d;
    for (const model::Division& div : organ.divisions())
    {
        if (div.name().rfind("Péd", 0) == 0)  d.pedal = div.id().value;
        else if (div.name().rfind("Gran", 0) == 0) d.go = div.id().value;
        else if (div.name().rfind("Réc", 0) == 0)  d.recit = div.id().value;
    }
    return d;
}
} // namespace

TEST_CASE("A drawn coupler sounds the borrowed division", "[engine][coupler][regression]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.go, d.recit });
    rig.run(1);

    const std::size_t goRanks    = rig.ranksOn(d.go);
    const std::size_t recitRanks = rig.ranksOn(d.recit);
    REQUIRE(goRanks > 0);
    REQUIRE(recitRanks > 0);

    // Uncoupled: a Grand-Orgue key sounds the Grand-Orgue and nothing else, even
    // with the Récit's stops drawn. That was already true and is half the claim.
    rig.noteOn(60, d.go);
    rig.run(20);
    INFO("uncoupled: " << rig.active() << " voices for " << goRanks << " GO ranks");
    CHECK(rig.active() == goRanks);

    rig.noteOff(60, d.go);
    rig.run(400);
    REQUIRE(rig.active() == 0);

    // Coupled: the same key now also calls the Récit's ranks.
    rig.drawCoupler(d.recit, d.go);
    rig.run(1);
    rig.noteOn(60, d.go);
    rig.run(20);

    INFO("coupled: " << rig.active() << " voices for " << goRanks << " + "
                     << recitRanks << " ranks");
    CHECK(rig.active() == goRanks + recitRanks);
}

TEST_CASE("An octave coupler sounds the borrowed rank transposed",
          "[engine][coupler]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.go, d.recit });
    rig.drawCoupler(d.recit, d.go, /*semitones*/ +12);
    rig.run(1);

    rig.noteOn(60, d.go);
    rig.run(20);
    REQUIRE(rig.active() == rig.ranksOn(d.go) + rig.ranksOn(d.recit));

    // The coupled voices live at note 72, so a note-off on 60 has to find them
    // there. If the transposition were applied on the way in but not on the way
    // out, the Récit would keep sounding with the key up -- which is exactly the
    // kind of asymmetry a note-on-only test would miss.
    rig.noteOff(60, d.go);
    rig.run(400);
    INFO(rig.active() << " voices still sounding after the key came up");
    CHECK(rig.active() == 0);
}

TEST_CASE("A key transposed off the compass is silent, not clamped",
          "[engine][coupler]")
{
    // Clamping would pile the whole top octave of a super-octave coupler onto note
    // 127 -- a cluster nobody played, and one that gets louder the higher you go.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.go, d.recit });
    rig.drawCoupler(d.recit, d.go, /*semitones*/ +12);
    rig.run(1);

    // Two keys a semitone apart, both of which transpose past 127.
    rig.noteOn(120, d.go);
    rig.noteOn(121, d.go);
    rig.run(20);

    // Each sounds its own division only: the coupled half fell off the top.
    INFO(rig.active() << " voices; " << rig.ranksOn(d.go) << " GO ranks per key");
    CHECK(rig.active() == rig.ranksOn(d.go) * 2);
}

TEST_CASE("A pipe two keys are holding survives one of them", "[engine][coupler][regression]")
{
    // Hold C4 on the Récit and C4 on the Grand-Orgue with Récit/Grand-Orgue drawn:
    // both keys call for the same Récit pipes. Releasing one hand must not silence
    // what the other is still playing, which is what a real pallet does -- it is
    // open while ANY key holds it.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.go, d.recit });
    rig.drawCoupler(d.recit, d.go);
    rig.run(1);

    const std::size_t goRanks    = rig.ranksOn(d.go);
    const std::size_t recitRanks = rig.ranksOn(d.recit);

    // First: the Grand-Orgue key ALONE really is coupling. Without this the rest of
    // the case cannot tell a drawn coupler from a retired one -- the counts come out
    // the same either way, because the Récit key is sounding those ranks regardless.
    rig.noteOn(60, d.go);
    rig.run(20);
    INFO("GO key alone with the coupler drawn: " << rig.active() << " voices");
    REQUIRE(rig.active() == goRanks + recitRanks);

    rig.noteOff(60, d.go);
    rig.run(400);
    REQUIRE(rig.active() == 0);

    rig.noteOn(60, d.recit);
    rig.run(20);
    REQUIRE(rig.active() == recitRanks);

    // The Grand-Orgue key adds its OWN ranks; the Récit's are already sounding and
    // are not started a second time.
    rig.noteOn(60, d.go);
    rig.run(20);
    INFO(rig.active() << " voices with both keys down");
    CHECK(rig.active() == recitRanks + goRanks);

    // Let go of the Grand-Orgue. Its own ranks release; the Récit's stay, because
    // the Récit key is still down.
    rig.noteOff(60, d.go);
    rig.run(400);
    INFO(rig.active() << " voices after releasing the coupling key");
    CHECK(rig.active() == recitRanks);

    rig.noteOff(60, d.recit);
    rig.run(400);
    CHECK(rig.active() == 0);
}

TEST_CASE("Drawing a coupler under a held chord sounds it at once",
          "[engine][coupler][regression]")
{
    // The reconciliation used to compare RANK SETS, which cannot see a coupler
    // being drawn: the ranks are unchanged and every key's expansion is not. A
    // coupler drawn mid-chord would then have appeared only on the next key press.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.go, d.recit });
    rig.run(1);

    rig.noteOn(60, d.go);
    rig.noteOn(64, d.go);
    rig.run(30);
    const std::size_t before = rig.active();
    REQUIRE(before == rig.ranksOn(d.go) * 2);

    rig.drawCoupler(d.recit, d.go);
    rig.run(10);

    INFO(before << " voices before the coupler, " << rig.active() << " after");
    CHECK(rig.active() == before + rig.ranksOn(d.recit) * 2);

    // And retiring it again releases exactly what it added.
    rig.clearCouplers();
    rig.run(400);
    INFO(rig.active() << " voices after retiring the coupler");
    CHECK(rig.active() == before);
}

TEST_CASE("Couplers do not chain", "[engine][coupler]")
{
    // Récit/Grand-Orgue plus Grand-Orgue/Pédale does NOT make a pedal key sound the
    // Récit. That takes Récit/Pédale, which this organ declares separately -- and
    // would not, if chaining were the intent. It is also what mechanical action
    // does, the coupler sitting at the key rather than after it.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const Divisions    d     = divisionsOf(organ);

    Rig rig{ organ };
    rig.drawDivisions({ d.pedal, d.go, d.recit });
    rig.drawCoupler(d.recit, d.go);     // Récit keys borrowed by the Grand-Orgue
    rig.drawCoupler(d.go, d.pedal);     // Grand-Orgue borrowed by the Pédale
    rig.run(1);

    rig.noteOn(40, d.pedal);
    rig.run(20);

    const std::size_t expected = rig.ranksOn(d.pedal) + rig.ranksOn(d.go);
    INFO(rig.active() << " voices; pedal+GO is " << expected << ", and chaining "
         << "would add the Récit's " << rig.ranksOn(d.recit));
    CHECK(rig.active() == expected);
}
