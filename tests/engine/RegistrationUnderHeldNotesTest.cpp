// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Drawing a stop with the chord still down.
//
// It is the most ordinary gesture an organist makes and the one the composite
// design could never do properly: with every rank folded into one spectrum, adding
// a stop meant re-voicing the sounding voice, which at best glided the whole chord
// to a new colour and at worst cut it.
//
// One voice per rank makes the right answer structural rather than clever. A rank
// that was already sounding is not touched at all — not re-seeded, not ramped,
// nothing — so it comes out BIT-IDENTICAL, which is the assertion below and is
// strictly stronger than "no audible click". A rank that is retired gets a real
// note-off and releases like a pipe; a rank that is drawn gets a real note-on and
// speaks like one.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/model/DemoOrgan.h"
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

/// An engine with a real instrument's ranks behind it.
struct Rig
{
    engine::AudioEngine                                eng;
    std::vector<std::unique_ptr<synth::AdditiveVoice>>  voices;
    std::vector<core::IVoice*>                          ptrs;
    std::vector<synth::RankVoicing>                     voicings;
    std::vector<float>                                  l, r;

    explicit Rig(const model::Organ& organ)
    {
        eng.prepare(kSr, kBlock, 2, 1);

        for (const model::Stop& s : organ.stops())
            if (s.division().value == 1) // one manual
                voicings.push_back(model::buildRankVoicing(organ, s.id()));

        for (std::size_t i = 0; i < 256; ++i)
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

    /// Publish the first @p n ranks as the registration.
    void draw(std::size_t n)
    {
        engine::EngagedRankTable t;
        for (std::size_t i = 0; i < n && i < voicings.size(); ++i)
            t.ranks[t.count++] =
                engine::EngagedRank{ &voicings[i], voicings[i].stop, voicings[i].division };
        t.epoch = static_cast<std::uint32_t>(n + 1);
        eng.setEngagedRanks(t);
    }

    void send(const engine::EngineCommand& c) { (void) eng.commandQueue().push(c); }

    /// Render one block and return the left channel.
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
};

double energyOf(const std::vector<float>& v)
{
    double e = 0.0;
    for (const float s : v)
        e += static_cast<double>(s) * s;
    return e;
}
} // namespace

TEST_CASE("Drawing a stop under a held chord leaves the sounding ranks untouched",
          "[engine][arch001][regression]")
{
    // Two rigs, identical up to the moment one of them draws another stop. The
    // ranks that were already sounding must be bit-identical afterwards -- the
    // difference between the two renders is EXACTLY the new rank, nothing else.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    Rig held{ organ };
    Rig ref{ organ };
    REQUIRE(held.voicings.size() >= 4);

    for (Rig* rig : { &held, &ref })
    {
        rig->draw(2);
        rig->run(1); // pick the table up
        rig->send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                                    core::DivisionId{ 1 }, 0));
        rig->run(30); // settle the attack
    }

    // One of them draws a third rank; the other does not.
    held.draw(3);

    // A few blocks, not one: a voice's attack bloom starts at exactly zero, so the
    // block a note is triggered in is silent by construction. Comparing there would
    // find no difference and say nothing about whether the rank was started.
    held.run(6);
    ref.run(6);

    const std::vector<float> withNew = held.run();
    const std::vector<float> without = ref.run();

    REQUIRE(energyOf(without) > 0.0);

    // The new rank has to be audible...
    double diff = 0.0;
    for (std::size_t i = 0; i < kBlock; ++i)
        diff += std::abs(static_cast<double>(withNew[i] - without[i]));
    INFO("difference " << diff);
    CHECK(diff > 0.0);

    // ...and the two rigs have to still be in step, which they only are if the
    // first two ranks were left completely alone. Continue both and compare the
    // ranks that did NOT change, by retiring the new one again.
    held.draw(2);
    held.run(1);
    ref.run(1);

    // After the third rank is released, both are the same two ranks in the same
    // state -- give the release time to finish and compare.
    held.run(60);
    ref.run(60);

    const std::vector<float> a = held.run();
    const std::vector<float> b = ref.run();

    double worst = 0.0;
    for (std::size_t i = 0; i < kBlock; ++i)
        worst = std::max(worst, std::abs(static_cast<double>(a[i] - b[i])));

    INFO("worst sample difference after the round trip: " << worst);
    CHECK(worst == 0.0); // bit-identical: the surviving ranks were never touched
}

TEST_CASE("Retiring a stop under a held chord releases only that rank",
          "[engine][arch001]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    Rig rig{ organ };
    REQUIRE(rig.voicings.size() >= 3);

    rig.draw(3);
    rig.run(1);
    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                               core::DivisionId{ 1 }, 0));
    rig.run(30);
    REQUIRE(rig.eng.activeVoiceCount() == 3);

    rig.draw(2); // retire the third
    rig.run(1);

    // It releases rather than vanishing, so it is still active for a moment.
    rig.run(80);
    INFO(rig.eng.activeVoiceCount() << " voices after retiring one of three");
    CHECK(rig.eng.activeVoiceCount() == 2);

    // And the key is still down, so the survivors are still sounding.
    const std::vector<float> still = rig.run();
    CHECK(energyOf(still) > 0.0);
}

TEST_CASE("A stop drawn with no key down starts nothing", "[engine][arch001]")
{
    // Reconciliation must be driven by keys that are physically DOWN, not by
    // voices that happen to be active: a voice in its release tail has had its key
    // released, and starting a newly drawn rank on it would sound a note nobody is
    // holding.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    Rig rig{ organ };

    rig.draw(2);
    rig.run(1);
    rig.send(engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                               core::DivisionId{ 1 }, 0));
    rig.run(20);
    rig.send(engine::EngineCommand::makeNoteOff(core::PipeId{ 0, 60, 1 },
                                                core::DivisionId{ 1 }, 0));
    rig.run(2); // releasing, still active

    const std::size_t releasing = rig.eng.activeVoiceCount();
    REQUIRE(releasing > 0);

    rig.draw(4); // draw two more while the tail is still sounding
    rig.run(1);

    INFO(rig.eng.activeVoiceCount() << " voices, was " << releasing);
    CHECK(rig.eng.activeVoiceCount() <= releasing); // nothing new started
}
