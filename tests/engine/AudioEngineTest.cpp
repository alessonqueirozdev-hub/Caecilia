// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// AudioEngine / VoicePool behaviour. None of this had a test, and all of it was
// broken:
//
//   * A pipe carried no division, so the same key on two manuals produced the
//     same identity: a note-off on one released the note on every other manual,
//     and a physical keyboard could never reach the pedal at all.
//   * The sustain pedal was not handled anywhere, so it did nothing.
//   * A block larger than the one prepare() was promised left the tail of the
//     host's buffer untouched, and the master chain then processed whatever the
//     host had left in it.
//   * Voices had to converge back to zero after a storm of note events; nothing
//     verified that they did.
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
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 256;
constexpr std::size_t      kVoices = 16;

synth::SpectralModel simpleTone()
{
    synth::SpectralModel m;
    for (int n = 1; n <= 4; ++n)
    {
        synth::PartialTrack t;
        t.ratioToF0 = static_cast<float>(n);
        t.ampDb     = -6.0f * static_cast<float>(n - 1);
        t.seed      = 0x1000u + static_cast<std::uint32_t>(n);
        m.partials.push_back(t);
    }
    m.fundamentalHz = 220.0f;
    return m;
}

/// An engine with a small pool of prepared voices, ready to receive commands.
struct Rig
{
    engine::AudioEngine                                engineInstance;
    /// Four divisions' worth of drawn rank, so a test can play on any of them.
    /// One voice per rank means an engine with nothing drawn is silent.
    caecilia::tests::RankTable                         ranks{ 8, 4 };
    std::vector<std::unique_ptr<synth::AdditiveVoice>>  voices;
    std::vector<core::IVoice*>                          ptrs;
    std::vector<float>                                  l, r;

    Rig()
    {
        engineInstance.prepare(kSr, kBlock, 2, 1);

        const synth::SpectralModel model = simpleTone();
        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Principal;
        ctx.footage = core::footage::kEight;

        voices.reserve(kVoices);
        ptrs.reserve(kVoices);
        for (std::size_t i = 0; i < kVoices; ++i)
        {
            auto v = std::make_unique<synth::AdditiveVoice>();
            v->bank().setMaxPartials(8);
            v->prepare(kSr, kBlock);
            v->setContext(ctx);
            v->seedFrom(model);
            ptrs.push_back(v.get());
            voices.push_back(std::move(v));
        }
        engineInstance.bindVoices(ptrs.data(), ptrs.size());
        ranks.publishTo(engineInstance);

        l.assign(kBlock * 8, 0.0f);
        r.assign(kBlock * 8, 0.0f);
    }

    void send(const engine::EngineCommand& c) { (void) engineInstance.commandQueue().push(c); }

    /// Render @p blocks blocks of kBlock frames each.
    void run(int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b)
        {
            float* chans[2] = { l.data(), r.data() };
            std::fill(l.begin(), l.begin() + kBlock, 0.0f);
            std::fill(r.begin(), r.begin() + kBlock, 0.0f);
            core::AudioBlock block(chans, 2, kBlock);
            engineInstance.processBlock(block);
        }
    }

    [[nodiscard]] std::size_t active() const { return engineInstance.activeVoiceCount(); }
};

core::PipeId pipe(std::uint8_t note, std::uint8_t division)
{
    return core::PipeId{ 0, note, division };
}
} // namespace

TEST_CASE("A note-off on one division leaves the other divisions sounding",
          "[engine][midi][regression]")
{
    Rig rig;

    // The same key, on two different manuals.
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 1), 100, core::DivisionId{1}));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 2), 100, core::DivisionId{2}));
    rig.run(2);
    REQUIRE(rig.active() == 2);

    // Release it on ONE of them. Before pipes carried a division, both identities
    // were equal and this released both.
    rig.send(engine::EngineCommand::makeNoteOff(pipe(60, 1), core::DivisionId{1}));
    rig.run(1);
    CHECK(rig.active() == 2); // the released one is still in its tail

    // Let the tail finish; exactly one voice must survive.
    rig.run(60);
    CHECK(rig.active() == 1);
}

TEST_CASE("The sustain pedal holds notes and releases them on lift",
          "[engine][midi][sustain]")
{
    Rig rig;
    const core::DivisionId div{1};

    rig.send(engine::EngineCommand::makeSustain(div, true));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(64, 1), 100, div));
    rig.run(2);
    REQUIRE(rig.active() == 1);

    // Key up while the pedal is down: the pipe must keep speaking, indefinitely.
    rig.send(engine::EngineCommand::makeNoteOff(pipe(64, 1), div));
    rig.run(120);
    CHECK(rig.active() == 1);

    // Pedal up: now it releases and the tail finishes.
    rig.send(engine::EngineCommand::makeSustain(div, false));
    rig.run(120);
    CHECK(rig.active() == 0);
}

TEST_CASE("The sustain pedal is per-division", "[engine][midi][sustain]")
{
    Rig rig;
    rig.send(engine::EngineCommand::makeSustain(core::DivisionId{1}, true));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 1), 100, core::DivisionId{1}));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 2), 100, core::DivisionId{2}));
    rig.run(2);
    REQUIRE(rig.active() == 2);

    // Division 2 has no pedal down, so its note releases normally.
    rig.send(engine::EngineCommand::makeNoteOff(pipe(60, 1), core::DivisionId{1}));
    rig.send(engine::EngineCommand::makeNoteOff(pipe(60, 2), core::DivisionId{2}));
    rig.run(120);
    CHECK(rig.active() == 1); // only the pedal-held one survives
}

TEST_CASE("A panic does not leave the sustain pedal latched", "[engine][midi][sustain]")
{
    Rig rig;
    const core::DivisionId div{0};

    rig.send(engine::EngineCommand::makeSustain(div, true));
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, div));
    rig.run(2);
    rig.send(engine::EngineCommand::makePanic());
    rig.run(120);
    REQUIRE(rig.active() == 0);

    // A note played after the panic must not be silently held by a stale pedal.
    rig.send(engine::EngineCommand::makeNoteOn(pipe(62, 0), 100, div));
    rig.run(2);
    rig.send(engine::EngineCommand::makeNoteOff(pipe(62, 0), div));
    rig.run(120);
    CHECK(rig.active() == 0);
}

TEST_CASE("Voices always converge back to silence after a storm of events",
          "[engine][midi][robustness]")
{
    Rig rig;

    // A deterministic but adversarial stream: overlapping notes across three
    // divisions, more simultaneous notes than the pool holds, and note-offs that
    // sometimes arrive for notes that were already stolen.
    std::uint32_t rng = 0x2545F491u;
    auto next = [&rng] {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    };

    for (int i = 0; i < 2000; ++i)
    {
        const auto note = static_cast<std::uint8_t>(36 + (next() % 48));
        const auto div  = static_cast<std::uint8_t>(next() % 3);
        const core::DivisionId d{ div };
        if ((next() & 1u) != 0u)
            rig.send(engine::EngineCommand::makeNoteOn(pipe(note, div), 100, d));
        else
            rig.send(engine::EngineCommand::makeNoteOff(pipe(note, div), d));

        if ((i % 7) == 0)
            rig.run(1);
    }

    // Release everything and let the tails finish.
    rig.send(engine::EngineCommand::makePanic());
    rig.run(200);

    CHECK(rig.active() == 0);
    for (std::size_t i = 0; i < kBlock; ++i)
    {
        REQUIRE(std::isfinite(rig.l[i]));
        REQUIRE(std::isfinite(rig.r[i]));
    }
}

TEST_CASE("A block larger than prepare() promised is rendered in full",
          "[engine][robustness][regression]")
{
    Rig rig;

    // Host contract violation, but offline bounces and validators both do it. The
    // engine used to clamp its render to maxBlockFrames and leave the REST of the
    // buffer holding whatever the host had put there.
    constexpr std::size_t kOversized = kBlock * 3;
    std::vector<float> l(kOversized, 1234.5f), r(kOversized, 1234.5f); // poison
    float* chans[2] = { l.data(), r.data() };
    core::AudioBlock block(chans, 2, kOversized);

    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{0}));
    rig.engineInstance.processBlock(block);

    // Not one sample of the poison may survive, anywhere in the buffer.
    for (std::size_t i = 0; i < kOversized; ++i)
    {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::fabs(l[i]) < 10.0f);
        REQUIRE(std::fabs(r[i]) < 10.0f);
    }
}


TEST_CASE("A note that starts and stops inside one block is not swallowed",
          "[engine][midi][regression]")
{
    // Commands used to be applied in one batch at the top of the block. A note
    // whose on and off share a block therefore went on and off before a single
    // sample was rendered, and the result was not "quiet" -- it was bit-exactly
    // zero, because trigger() sets the envelope to zero and the first rendered
    // Release sample clamps to zero and goes Idle. At the 4096-frame blocks an
    // offline bounce uses, every note shorter than 85 ms simply vanished.
    const auto energyOf = [](std::uint32_t onAt, std::uint32_t offAt)
    {
        Rig rig;
        rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100,
                                                   core::DivisionId{0}, onAt));
        rig.send(engine::EngineCommand::makeNoteOff(pipe(60, 0),
                                                    core::DivisionId{0}, offAt));
        rig.run(1);

        double e = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i)
            e += static_cast<double>(rig.l[i]) * rig.l[i];
        return e;
    };

    // The control arm: both events at the top of the block, which is what every
    // producer that does not know better stamps. This is what the engine did with
    // EVERY note before, and it must still come out silent -- so anything the
    // timed arm produces is attributable to the offset, not to the test rig.
    CHECK(energyOf(0, 0) == 0.0);

    // The real case: note on at frame 16, off at frame 200 of a 256-frame block.
    const double energy = energyOf(16, 200);
    INFO("energy " << energy);
    CHECK(energy > 0.0);
}

TEST_CASE("An event lands where the host stamped it, not at the top of the block",
          "[engine][midi]")
{
    // A voice's gain is recomputed per slice from its own elapsed note time, and
    // its attack bloom starts at exactly zero -- so the slice a note is triggered
    // in is silent whatever the offset, and "audio appears at frame 128" would be
    // a statement about the envelope rather than about the timing.
    //
    // What IS the timing: by the end of the FOLLOWING block, a note stamped late
    // has accumulated less note time than one stamped early, and is quieter for
    // it. Applied at the top of the block, as everything was before, the two arms
    // are identical.
    const auto energyInSecondBlock = [](std::uint32_t onAt)
    {
        Rig rig;
        rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100,
                                                   core::DivisionId{0}, onAt));
        rig.run(2); // run() renders into the same buffer, so this leaves block two

        double e = 0.0;
        for (std::size_t i = 0; i < kBlock; ++i)
            e += static_cast<double>(rig.l[i]) * rig.l[i];
        return e;
    };

    const double early = energyInSecondBlock(0);
    const double late  = energyInSecondBlock(static_cast<std::uint32_t>(kBlock - 8));

    INFO("early " << early << "  late " << late);
    CHECK(early > 0.0);
    CHECK(early > late * 4.0);
}

TEST_CASE("An offset past the end of the block still lands in it", "[engine][midi]")
{
    // A command whose offset exceeds the block is clamped to the last frame, not
    // deferred. Deferring would strand it until the next callback -- and a
    // note-off that arrives a block late is a stuck note.
    Rig rig;
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{0}, 0));
    rig.run(1);
    REQUIRE(rig.active() == 1);

    rig.send(engine::EngineCommand::makeNoteOff(pipe(60, 0), core::DivisionId{0},
                                                /*sampleOffset*/ 999999));
    rig.run(1);

    // It was consumed within that block, so the voice is releasing rather than
    // still being held down.
    for (int b = 0; b < 60; ++b)
        rig.run(1);
    CHECK(rig.active() == 0);
}

TEST_CASE("The published meter describes the whole block, not its last slice",
          "[engine][meters][regression]")
{
    // The meters were captured AND published per slice, off a snapshot the
    // capture had just reset. With the block now cut at every event, the console's
    // VU would read whatever the final slice happened to hold.
    //
    // Measured during the note's attack, where the level is still ramping: the
    // last four frames of the block are its loudest, so a per-slice meter reads
    // materially HIGH. The test requires that contrast to exist before it asserts
    // anything, because without it there is nothing to distinguish.
    Rig rig;
    rig.send(engine::EngineCommand::makeNoteOn(pipe(60, 0), 100, core::DivisionId{0}, 0));
    rig.run(1); // block one: the voice is triggered but its bloom starts at zero

    // Block two, cut four frames from the end.
    rig.send(engine::EngineCommand::makeSustain(core::DivisionId{7}, true,
                                                static_cast<std::uint32_t>(kBlock - 4)));
    rig.run(1);

    // Over BOTH channels, because that is what the engine's meter measures --
    // and the voice is panned, so the two are not the same signal.
    const auto rmsOver = [&rig](std::size_t from, std::size_t to)
    {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += static_cast<double>(rig.l[i]) * rig.l[i]
               + static_cast<double>(rig.r[i]) * rig.r[i];
        return std::sqrt(s / static_cast<double>(2 * (to - from)));
    };

    const double blockRms     = rmsOver(0, kBlock);
    const double lastSliceRms = rmsOver(kBlock - 4, kBlock);

    REQUIRE(blockRms > 0.0);
    // If this fails the rig stopped producing a contrast and the assertion below
    // would be vacuous -- which is exactly how this test passed against the bug
    // the first time it was written.
    REQUIRE(lastSliceRms > blockRms * 1.2);

    const auto meters = rig.engineInstance.latestMeters();
    INFO("meter " << meters.master.rms << ", block " << blockRms
                  << ", last slice " << lastSliceRms);
    CHECK(static_cast<double>(meters.master.rms) == Approx(blockRms).epsilon(0.05));
}

TEST_CASE("The CPU budget is spent once per block, however many events cut it",
          "[engine][regression]")
{
    // The budget was reset inside the slice loop. Once events cut a block, that
    // hands a nine-slice block nine full budgets and makes the governor
    // decorative. Scaling per slice is the other half: charge every slice the
    // full per-block cost of a voice and a block with events in it sheds voices
    // on a load that fits comfortably without them.
    //
    // Four AdditiveVoices of four partials cost 0.58 units each, so 2.32 together.
    // A budget of 2.4 fits them exactly once and not twice.
    const auto survivorsAfterSettling = [](bool manyEvents)
    {
        Rig rig;
        rig.engineInstance.setBlockBudget(2.4f);
        for (std::uint8_t k = 0; k < 4; ++k)
            rig.send(engine::EngineCommand::makeNoteOn(
                pipe(static_cast<std::uint8_t>(60 + k), 0), 100, core::DivisionId{0}, 0));
        rig.run(2);

        if (manyEvents)
        {
            // Eight harmless events spread across the block, purely to cut it.
            for (std::uint32_t k = 1; k <= 8; ++k)
                rig.send(engine::EngineCommand::makeSustain(core::DivisionId{7},
                                                            (k & 1u) != 0, k * 28u));
        }
        rig.run(1);

        // A shed voice is RELEASED, not killed, so it stays active for its whole
        // release tail. Counting it one block later counts nothing.
        rig.run(80);
        return rig.active();
    };

    REQUIRE(survivorsAfterSettling(false) == 4); // nothing sheds without the events
    CHECK(survivorsAfterSettling(true)   == 4);  // and nothing sheds with them
}
