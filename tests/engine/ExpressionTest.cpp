// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The swell shoe. An enclosed division sits behind shutters a pedal opens and
// closes, and on an instrument with no other dynamics it is how a phrase is
// shaped. CC 11 had been arriving and being dropped.
//
// Three of these cases are about things nobody would hear as a bug so much as
// notice as wrongness later:
//
//   * per-division isolation — a shoe is one box, not a master fader;
//   * no step at a block boundary — the shoe is DRAGGED, and a per-block gain is
//     a staircase under a sustained chord, which is what an organ mostly plays;
//   * a panic must not fling a shut box open — "stop the notes" is not "undo the
//     player's dynamics", and the next phrase would arrive at full volume.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"

#include "support/RankTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Approx;
namespace core   = caecilia::core;
namespace engine = caecilia::core::engine;
namespace synth  = caecilia::synth;

namespace
{
constexpr core::SampleRate kSr     = 48000.0;
constexpr std::size_t      kBlock  = 256;
constexpr std::size_t      kVoices = 16;

synth::SpectralModel tone()
{
    synth::SpectralModel m;
    synth::PartialTrack t;
    t.ratioToF0 = 1.0f;
    t.ampDb     = 0.0f;
    t.seed      = 0x77u;
    m.partials.push_back(t);
    m.fundamentalHz = 220.0f;
    return m;
}

struct Rig
{
    engine::AudioEngine                               eng;
    caecilia::tests::RankTable                        ranks{ 4, 1 };
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    std::vector<float>                                 l, r;

    Rig()
    {
        eng.prepare(kSr, kBlock, 2, 1);

        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;

        for (std::size_t i = 0; i < kVoices; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(4);
            v->prepare(kSr, kBlock);
            v->setContext(ctx);
            v->seedFrom(tone());
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        eng.bindVoices(ptrs.data(), ptrs.size());
        ranks.publishTo(eng);
        l.assign(kBlock, 0.0f);
        r.assign(kBlock, 0.0f);
    }

    void send(const engine::EngineCommand& c) { (void) eng.commandQueue().push(c); }

    void run(int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* chans[2] = { l.data(), r.data() };
            core::AudioBlock block(chans, 2, kBlock);
            eng.processBlock(block);
        }
    }

    [[nodiscard]] double rms() const
    {
        double s = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i)
            s += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
        return std::sqrt(s / static_cast<double>(2 * kBlock));
    }
};

core::PipeId pipe(std::uint8_t note, std::uint8_t division)
{
    return core::PipeId{ 0, note, division };
}

/// Settle a note and a shoe position, then measure. The glide is 30 ms, so a
/// generous number of blocks puts both well past it.
double levelAt(float position, std::uint8_t division = 0)
{
    Rig rig;
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, division), 100,
                                               core::DivisionId{ division }, 0));
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ division }, position, 0));
    rig.run(40);
    return rig.rms();
}
} // namespace

TEST_CASE("A shut swell box attenuates without silencing", "[engine][expression]")
{
    // A real box at its tightest still passes a good deal of sound. One that went
    // to zero would read as a mute rather than as an enclosure, and the organist
    // would lose the bottom of their dynamic range rather than gaining it.
    const double open = levelAt(1.0f);
    const double shut = levelAt(0.0f);

    REQUIRE(open > 0.0);
    REQUIRE(shut > 0.0); // shut is not silent
    INFO("shut/open = " << shut / open);
    CHECK(shut / open == Approx(0.2239).margin(0.02)); // -13 dB
}

TEST_CASE("The swell shoe moves the level monotonically", "[engine][expression]")
{
    const double a = levelAt(0.0f);
    const double b = levelAt(0.35f);
    const double c = levelAt(0.7f);
    const double d = levelAt(1.0f);

    INFO(a << " < " << b << " < " << c << " < " << d);
    CHECK(a < b);
    CHECK(b < c);
    CHECK(c < d);
}

TEST_CASE("A swell shoe belongs to one division", "[engine][expression]")
{
    // A shoe is a box, not a master fader. Shutting the Récit must not touch the
    // Grand-Orgue -- and the whole point of an enclosed division is that it can be
    // played against an unenclosed one.
    Rig rig;
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 1), 100, core::DivisionId{ 1 }, 0));
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 2 }, 0.0f, 0));
    rig.run(40);
    const double otherDivisionShut = rig.rms();

    Rig ref;
    ref.send(engine::EngineCommand::makeNoteOn(pipe(60, 1), 100, core::DivisionId{ 1 }, 0));
    ref.run(40);
    const double untouched = ref.rms();

    REQUIRE(untouched > 0.0);
    INFO(otherDivisionShut << " vs " << untouched);
    CHECK(otherDivisionShut == Approx(untouched).epsilon(0.001));
}

TEST_CASE("Dragging the shoe does not step at block boundaries",
          "[engine][expression][regression]")
{
    // The shoe is dragged, not switched, and a gain applied per BLOCK is a
    // staircase: on a sustained chord — which is what an organ mostly plays — every
    // block boundary is a discontinuity. The ramp is per sample for this reason.
    Rig rig;
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{ 0 }, 0));
    rig.run(40); // settle the attack, box open

    // Slam the shoe shut and watch the glide, block by block, for a discontinuity
    // at the seams.
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 0 }, 0.0f, 0));

    float  lastSample = 0.0f;
    double worstSeam  = 0.0;
    double worstInner = 0.0;
    for (int b = 0; b < 12; ++b)
    {
        rig.run(1);
        if (b > 0)
            worstSeam = std::max(worstSeam, std::abs(static_cast<double>(rig.l[0] - lastSample)));
        for (std::size_t i = 1; i < kBlock; ++i)
            worstInner = std::max(worstInner,
                                  std::abs(static_cast<double>(rig.l[i] - rig.l[i - 1])));
        lastSample = rig.l[kBlock - 1];
    }

    // The seam between two blocks must be no worse than the signal's own motion
    // inside one. A per-block gain would make it several times worse.
    INFO("seam " << worstSeam << " vs inner " << worstInner);
    REQUIRE(worstInner > 0.0);
    CHECK(worstSeam <= worstInner * 1.2);
}

TEST_CASE("A panic does not fling the swell box open",
          "[engine][expression][regression]")
{
    // "Stop the notes" is not "undo the player's dynamics". Reopening the box on a
    // panic would make whatever they played next arrive at full volume, which is
    // the opposite of what they set the pedal for.
    Rig rig;
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 0 }, 0.0f, 0));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{ 0 }, 0));
    rig.run(40);
    const double shutLevel = rig.rms();

    rig.send(engine::EngineCommand::makePanic(0));
    rig.run(60); // let the release finish

    // Play again; the box must still be shut.
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{ 0 }, 0));
    rig.run(40);

    INFO("after panic " << rig.rms() << " vs before " << shutLevel);
    REQUIRE(shutLevel > 0.0);
    CHECK(rig.rms() == Approx(shutLevel).epsilon(0.05));
}

TEST_CASE("Preparing the engine re-opens every shoe", "[engine][expression]")
{
    // A host changing sample rate must not leave a box shut that nobody can see is
    // shut -- and a division nobody has ever sent CC 11 for must sound at all.
    Rig rig;
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 0 }, 0.0f, 0));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{ 0 }, 0));
    rig.run(40);
    const double shut = rig.rms();

    rig.eng.prepare(kSr, kBlock, 2, 1);
    for (auto& v : rig.voices)
    {
        v->prepare(kSr, kBlock);
        v->seedFrom(tone());
    }
    rig.eng.bindVoices(rig.ptrs.data(), rig.ptrs.size());

    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{ 0 }, 0));
    rig.run(40);

    INFO("after prepare " << rig.rms() << " vs shut " << shut);
    REQUIRE(shut > 0.0);
    CHECK(rig.rms() > shut * 2.0); // wide open again
}
