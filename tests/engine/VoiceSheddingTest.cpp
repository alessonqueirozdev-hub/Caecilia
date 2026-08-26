// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// What the engine gives up when it cannot afford everything, and how.
//
// Three defects, all in the same handful of lines:
//
//   * The victim was whichever voice the render loop happened to reach when the
//     budget ran out. That is pool slot order, so the pipe that vanished under a
//     tutti was as likely to be the 16' pedal holding the passage up as the top
//     of a mixture -- and StealPolicy::Quietest, which the engine is configured
//     with, was consulted only on the pool-full path and never here.
//   * The comment claimed shedding started a release rather than skipping the
//     voice, and the code did both: noteOff() and then `continue`. The pipe went
//     silent for exactly one block and came back the next still at full envelope,
//     so a shed note was bracketed by two discontinuities instead of fading.
//   * levelEstimate -- the number both victim choices are made from -- left out
//     the swell box and the velocity gain, so the pipes behind a shut lid
//     reported themselves exactly as audible as the ones in the open.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/IVoice.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/VoiceScheduler.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"

#include "support/RankTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Approx;
namespace core   = caecilia::core;
namespace engine = caecilia::core::engine;
namespace synth  = caecilia::synth;

namespace
{
// ---------------------------------------------------------------------------
// A voice that is nothing but a cost and a level, so planBlock's policy can be
// tested without synthesising anything.
// ---------------------------------------------------------------------------
class StubVoice final : public core::IVoice
{
public:
    StubVoice(float level, float cost) noexcept : level_(level), cost_(cost) {}

    void prepare(core::SampleRate, std::size_t) override {}
    void noteOn(core::PipeId, core::Velocity) noexcept override { active_ = true; }
    void noteOff() noexcept override { released_ = true; }
    void silence() noexcept override { active_ = false; }
    void setExpression(float, float) noexcept override {}
    void adoptRank(const void*) noexcept override {}
    void renderAdd(core::AudioBlock&) noexcept override { ++renders_; }

    [[nodiscard]] bool             isActive() const noexcept override { return active_; }
    [[nodiscard]] core::EngineKind kind() const noexcept override
    {
        return core::EngineKind::Additive;
    }
    [[nodiscard]] core::VoiceTier tier() const noexcept override
    {
        return core::VoiceTier::Additive;
    }
    [[nodiscard]] float cpuCostEstimate() const noexcept override { return cost_; }
    [[nodiscard]] float levelEstimate() const noexcept override { return level_; }

    bool active_   = true;
    bool released_ = false;
    int  renders_  = 0;

private:
    float level_ = 1.0f;
    float cost_  = 1.0f;
};

/// A pool view over a hand-built set of stub voices, all of one engine kind.
struct StubPool
{
    std::vector<std::unique_ptr<StubVoice>> owned;
    std::vector<core::IVoice*>              ptrs;
    std::vector<core::PipeId>               pipes;
    std::vector<std::uint16_t>              indices;
    std::array<std::size_t, 2>              offsets{};

    void add(float level, float cost)
    {
        owned.push_back(std::make_unique<StubVoice>(level, cost));
        ptrs.push_back(owned.back().get());
        pipes.push_back(core::PipeId{});
        indices.push_back(static_cast<std::uint16_t>(indices.size()));
    }

    [[nodiscard]] engine::VoicePoolView view() noexcept
    {
        offsets = { 0, indices.size() };
        engine::VoicePoolView v;
        v.voices        = ptrs.data();
        v.pipes         = pipes.data();
        v.voiceCount    = ptrs.size();
        v.activeIndices = indices.data();
        v.activeCount   = indices.size();
        v.kindOffsets   = offsets.data();
        v.numKinds      = 1;
        return v;
    }

    [[nodiscard]] StubVoice& at(std::size_t i) noexcept { return *owned[i]; }
};

// ---------------------------------------------------------------------------
// A real engine with two enclosed divisions, so a shut swell shoe can make one
// division's pipes genuinely quieter than the other's.
// ---------------------------------------------------------------------------
constexpr core::SampleRate kSr     = 48000.0;
constexpr std::size_t      kBlock  = 256;
constexpr std::size_t      kVoices = 64;

synth::SpectralModel tone()
{
    synth::SpectralModel m;
    for (int n = 1; n <= 4; ++n)
    {
        synth::PartialTrack t;
        t.ratioToF0 = static_cast<float>(n);
        t.ampDb     = -6.0f * static_cast<float>(n - 1);
        t.seed      = 0x7100u + static_cast<std::uint32_t>(n);
        m.partials.push_back(t);
    }
    m.fundamentalHz = 220.0f;
    return m;
}

struct Rig
{
    engine::AudioEngine                                engineInstance;
    caecilia::tests::RankTable                         ranks{ 2, 4 };
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*>                         ptrs;
    std::vector<float>                                 l, r;

    Rig()
    {
        // Two windchests, one per division, and both boxed -- so a shoe on either
        // division reaches its own pipes and nothing else.
        engineInstance.prepare(kSr, kBlock, 2, 2);

        const synth::SpectralModel model = tone();
        synth::VoiceContext        ctx;
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

        // Each division's rank on its own chest, both enclosed.
        auto table = ranks.table();
        for (std::size_t i = 0; i < table.count; ++i)
            table.ranks[i].chest = core::WindchestId{ static_cast<std::uint16_t>(i) };
        engineInstance.setEngagedRanks(table);

        const std::array<engine::ChestEnclosure, 2> boxed{
            engine::ChestEnclosure{ core::WindchestId{ 0 }, core::DivisionId{ 0 } },
            engine::ChestEnclosure{ core::WindchestId{ 1 }, core::DivisionId{ 1 } },
        };
        engineInstance.setEnclosedChests(boxed);

        l.assign(kBlock, 0.0f);
        r.assign(kBlock, 0.0f);
    }

    void send(const engine::EngineCommand& c) { (void) engineInstance.commandQueue().push(c); }

    /// Render blocks and return the peak magnitude of the LAST one.
    float run(int blocks = 1)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            float* chans[2] = { l.data(), r.data() };
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            core::AudioBlock block(chans, 2, kBlock);
            engineInstance.processBlock(block);

            peak = 0.0f;
            for (std::size_t f = 0; f < kBlock; ++f)
                peak = std::max(peak, std::abs(l[f]));
        }
        return peak;
    }
};

core::PipeId pipe(std::uint8_t note, std::uint8_t division)
{
    return core::PipeId{ static_cast<std::uint16_t>(division), note, division };
}

/// A pool view over every bound voice of a rig, so planBlock can be asked what it
/// would do with the engine's real state. planBlock skips inactive voices itself,
/// so offering it all of them is the same set the engine would.
struct RigView
{
    std::vector<core::PipeId>  pipes;
    std::vector<std::uint16_t> indices;
    std::array<std::size_t, 2> offsets{};

    explicit RigView(const Rig& rig)
    {
        for (std::size_t i = 0; i < rig.ptrs.size(); ++i)
        {
            pipes.push_back(core::PipeId{});
            indices.push_back(static_cast<std::uint16_t>(i));
        }
        offsets = { 0, indices.size() };
    }

    [[nodiscard]] engine::VoicePoolView of(const Rig& rig) noexcept
    {
        engine::VoicePoolView v;
        v.voices        = rig.ptrs.data();
        v.pipes         = pipes.data();
        v.voiceCount    = rig.ptrs.size();
        v.activeIndices = indices.data();
        v.activeCount   = indices.size();
        v.kindOffsets   = offsets.data();
        v.numKinds      = 1;
        return v;
    }
};
} // namespace

// ===========================================================================
// The policy, on its own.
// ===========================================================================

TEST_CASE("A set that fits is not touched", "[engine][cpu][shedding]")
{
    engine::VoiceScheduler s;
    StubPool               pool;
    for (int i = 0; i < 10; ++i)
        pool.add(/*level*/ 0.5f, /*cost*/ 1.0f);

    const auto plan = s.planBlock(pool.view(), /*budget*/ 100.0f);
    CHECK(plan.demandUnits == Approx(10.0f));
    CHECK(plan.shedBelowLevel == 0.0f);
    CHECK(plan.shedCount == 0);
}

TEST_CASE("The quietest pipes are the ones given up", "[engine][cpu][shedding]")
{
    // Ten loud, ten quiet, and room for about half. The old policy spent the
    // budget in slot order, so which half survived was an accident of when the
    // notes were pressed.
    engine::VoiceScheduler s;
    StubPool               pool;
    for (int i = 0; i < 10; ++i)
        pool.add(/*level*/ 0.8f, 1.0f);
    for (int i = 0; i < 10; ++i)
        pool.add(/*level*/ 0.01f, 1.0f);

    const auto plan = s.planBlock(pool.view(), /*budget*/ 12.0f);
    REQUIRE(plan.demandUnits == Approx(20.0f));
    REQUIRE(plan.shedBelowLevel > 0.0f);

    CHECK(plan.shedBelowLevel <= 0.8f);  // the loud ones stay
    CHECK(plan.shedBelowLevel > 0.01f);  // the quiet ones go
    CHECK(plan.shedCount == 10);
}

TEST_CASE("An equally loud tutti is thinned, not silenced", "[engine][cpu][shedding]")
{
    // Every voice in one level bucket and a budget that affords a fraction of
    // them. Dropping the bucket the budget runs out in would silence the whole
    // instrument at once -- which is exactly the case a tutti presents.
    engine::VoiceScheduler s;
    StubPool               pool;
    for (int i = 0; i < 200; ++i)
        pool.add(/*level*/ 0.6f, 1.0f);

    const auto plan = s.planBlock(pool.view(), /*budget*/ 20.0f);
    REQUIRE(plan.demandUnits == Approx(200.0f));

    // Nothing is pre-shed: the histogram cannot rank inside one bucket, so the
    // budget check during the render is what trims it.
    CHECK(plan.shedCount == 0);
    CHECK(plan.shedBelowLevel < 0.6f);
}

TEST_CASE("Silent and inactive voices cost nothing and are shed first",
          "[engine][cpu][shedding]")
{
    engine::VoiceScheduler s;
    StubPool               pool;
    pool.add(0.9f, 1.0f);
    pool.add(0.9f, 1.0f);
    pool.add(0.0f, 1.0f);         // level exactly zero: bucket 0, never ilogb'd
    pool.add(1.0e-38f, 1.0f);     // denormal
    pool.at(1).active_ = false;   // and one that is not sounding at all

    const auto plan = s.planBlock(pool.view(), /*budget*/ 1.5f);
    CHECK(plan.demandUnits == Approx(3.0f)); // the inactive one is not counted
    CHECK(plan.shedCount == 2);              // the silent pair
    CHECK(plan.shedBelowLevel > 1.0e-38f);
    CHECK(plan.shedBelowLevel <= 0.9f);
}

TEST_CASE("A budget of zero governs nothing", "[engine][cpu][shedding]")
{
    // Which is what an engine with no budget set means, and what every test that
    // does not care about the governor relies on.
    engine::VoiceScheduler s;
    StubPool               pool;
    for (int i = 0; i < 10; ++i)
        pool.add(0.5f, 1.0f);

    CHECK(s.planBlock(pool.view(), 0.0f).shedBelowLevel == 0.0f);
    CHECK(s.planBlock(pool.view(), -1.0f).shedBelowLevel == 0.0f);
}

// ===========================================================================
// The same policy, through the whole engine.
// ===========================================================================

TEST_CASE("A shed pipe fades instead of vanishing for a block",
          "[engine][cpu][shedding][regression]")
{
    // The old code did noteOff() and then `continue`, so the block a voice was
    // shed in contained no samples from it at all and the next block resumed at
    // full envelope. Two discontinuities, on a sustained note, at block rate for
    // as long as the budget stayed tight.
    Rig rig;
    for (std::uint8_t k = 0; k < 8; ++k)
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 0),
                                                   100, core::DivisionId{ 0 }));
    const float settled = rig.run(20);
    REQUIRE(settled > 0.0f);

    // A budget far under what eight voices demand: everything sheds.
    rig.engineInstance.setBlockBudget(0.1f);
    const float duringShed = rig.run(1);

    INFO("settled peak " << settled << ", peak in the shed block " << duringShed);
    CHECK(duringShed > 0.0f);

    // And it is not a fraction of what it was -- one block of a release ramp
    // barely moves an organ envelope.
    CHECK(duringShed > settled * 0.5f);
}

TEST_CASE("The pipes behind a shut swell box are given up first",
          "[engine][cpu][shedding]")
{
    // The most musical statement this policy can make, and it needs levelEstimate
    // to include the expression gain -- which it did not. A shut box is thirteen
    // decibels down before the shutters' treble loss, so those pipes are two level
    // buckets below the ones in the open and go first by construction.
    Rig rig;

    // Division 0 wide open, division 1 shut.
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 0 }, 1.0f));
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 1 }, 0.0f));

    for (std::uint8_t k = 0; k < 6; ++k)
    {
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 0),
                                                   100, core::DivisionId{ 0 }));
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 1),
                                                   100, core::DivisionId{ 1 }));
    }
    // Long enough for the shoe to travel and the envelopes to settle.
    rig.run(200);
    REQUIRE(rig.engineInstance.activeVoiceCount() == 12);

    // Ask the scheduler directly what it would give up, at a budget that affords
    // roughly half the set.
    // The two divisions must NOT report the same audibility.
    float loudest = 0.0f, quietest = 1.0e9f;
    for (const auto& v : rig.voices)
        if (const float lv = v->levelEstimate(); lv > 0.0f)
        {
            loudest  = std::max(loudest, lv);
            quietest = std::min(quietest, lv);
        }

    INFO("loudest " << loudest << ", quietest " << quietest);
    CHECK(loudest > quietest * 2.0f); // at least 6 dB apart: different buckets

    engine::VoiceScheduler probe;
    RigView                view(rig);
    const auto plan = probe.planBlock(view.of(rig), /*budget*/ 6.0f * 0.58f + 0.1f);
    REQUIRE(plan.shedBelowLevel > 0.0f);
    CHECK(plan.shedBelowLevel > quietest);
    CHECK(plan.shedBelowLevel <= loudest);
    CHECK(plan.shedCount == 6); // exactly the boxed division
}

TEST_CASE("The engine acts on the plan, not only on the running total",
          "[engine][cpu][shedding]")
{
    // The whole path, end to end: planBlock decides once per block, the engine
    // carries the verdict into every slice's RenderContext, and renderBatch
    // applies it. Without the middle link the only thing that ever sheds is the
    // budget running out in iteration order -- which is the defect, restored.
    //
    // Division 1 is shut, so its six pipes are two level buckets below division
    // 0's. The budget affords roughly the open six and no more.
    Rig rig;
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 0 }, 1.0f));
    rig.send(engine::EngineCommand::makeExpression(core::DivisionId{ 1 }, 0.0f));

    for (std::uint8_t k = 0; k < 6; ++k)
    {
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 0),
                                                   100, core::DivisionId{ 0 }));
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 1),
                                                   100, core::DivisionId{ 1 }));
    }
    rig.run(200);
    REQUIRE(rig.engineInstance.activeVoiceCount() == 12);

    // Six AdditiveVoices of four partials cost 0.58 each: 3.48 together.
    constexpr float kBudget = 3.6f;

    // State plainly which mechanism this exercises: the PLAN names six voices
    // before a sample is rendered. Without that the budget check alone would trim
    // six in pool slot order, which alternates the two divisions and is exactly
    // the arbitrariness under test.
    {
        engine::VoiceScheduler probe;
        RigView                view(rig);
        REQUIRE(probe.planBlock(view.of(rig), kBudget).shedCount == 6);
    }

    rig.engineInstance.setBlockBudget(kBudget);
    rig.run(1);

    // Every voice of the boxed division is now releasing, and none of the open
    // one is. Envelope stage is the observable: a released voice's level falls.
    const float openLevel = rig.voices[0]->levelEstimate();
    rig.run(30);

    std::size_t stillFull = 0, fading = 0;
    for (const auto& v : rig.voices)
        if (const float lv = v->levelEstimate(); lv > 0.0f)
            (lv >= openLevel * 0.95f ? stillFull : fading) += 1;

    INFO("open level " << openLevel << ", full " << stillFull << ", fading " << fading);
    CHECK(stillFull == 6);
    CHECK(fading == 6);
}

TEST_CASE("A voice already releasing is given up before one still sounding",
          "[engine][cpu][shedding]")
{
    // The same mechanism seen from the envelope side: levelEstimate falls with
    // envGain_, so a pipe already on its way out is the cheapest thing to lose.
    Rig rig;
    for (std::uint8_t k = 0; k < 4; ++k)
        rig.send(engine::EngineCommand::makeNoteOn(pipe(static_cast<std::uint8_t>(55 + k), 0),
                                                   100, core::DivisionId{ 0 }));
    rig.run(40);

    // Release two of them and let their tails fall part-way.
    for (std::uint8_t k = 0; k < 2; ++k)
        rig.send(engine::EngineCommand::makeNoteOff(pipe(static_cast<std::uint8_t>(55 + k), 0),
                                                    core::DivisionId{ 0 }));
    rig.run(12);
    REQUIRE(rig.engineInstance.activeVoiceCount() == 4);

    engine::VoiceScheduler probe;
    RigView                view(rig);
    const auto plan = probe.planBlock(view.of(rig), /*budget*/ 2.0f * 0.58f + 0.1f);
    REQUIRE(plan.shedBelowLevel > 0.0f);

    // Whatever it decided, the two still holding must be above the line and the
    // two fading must be below it.
    std::size_t above = 0, below = 0;
    for (const auto& v : rig.voices)
        if (const float lv = v->levelEstimate(); lv > 0.0f)
            (lv >= plan.shedBelowLevel ? above : below) += 1;

    CHECK(above == 2);
    CHECK(below == 2);
}
