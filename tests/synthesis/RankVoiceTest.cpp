// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// One voice per (rank, note).
//
// The load-bearing case here is the allocation counter. A voice taken off the free
// list has no idea which rank it is about to sound, so every note-on adopts one —
// and a Tutti chord is 260 of those inside a single audio callback. If adopting a
// rank allocates even once, the instrument has a malloc on its hot path per note
// per stop, which is not a performance problem so much as an unbounded-latency
// one: an allocator that decides to take a lock or ask the OS for a page does it
// while the callback is waiting.
//
// Counting is the only way to know. Reading the code is not: seedFrom copies into
// storage reserved at prepare time, and whether that reservation is large enough
// for every rank of this instrument is a question about the instrument.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/engine/AudioEngine.h"
#include "caecilia/engine/EngagedRankTable.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Stop.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/RankVoicing.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

namespace core   = caecilia::core;
namespace engine = caecilia::core::engine;
namespace model  = caecilia::model;
namespace synth  = caecilia::synth;

// ---------------------------------------------------------------------------
// A counting allocator. Global, because that is the only kind that catches an
// allocation nobody expected -- inside a container, inside a library, anywhere.
// ---------------------------------------------------------------------------
namespace
{
std::atomic<long> g_allocations{ 0 };
std::atomic<bool> g_counting{ false };

/// Count allocations across a scope, and only there: the test framework itself
/// allocates constantly, and counting that would drown the signal.
struct AllocationScope
{
    AllocationScope()
    {
        g_allocations.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_relaxed);
    }
    ~AllocationScope() { g_counting.store(false, std::memory_order_relaxed); }
    [[nodiscard]] long count() const { return g_allocations.load(std::memory_order_relaxed); }
};
} // namespace

void* operator new(std::size_t size)
{
    if (g_counting.load(std::memory_order_relaxed))
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size != 0 ? size : 1);
    if (p == nullptr)
        throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t size) { return operator new(size); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 256;

double renderEnergy(synth::AdditiveVoice& v, int blocks)
{
    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    double e = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* chans[2] = { l.data(), r.data() };
        core::AudioBlock block(chans, 2, kBlock);
        v.renderAdd(block);
        for (std::size_t i = 0; i < kBlock; ++i)
            e += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
    }
    return e;
}
} // namespace

TEST_CASE("Adopting a rank and starting a note allocates nothing",
          "[synthesis][rankvoice][regression]")
{
    // The whole hot path of a per-rank note-on, measured. A Tutti chord is 260 of
    // these inside one callback.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<synth::RankVoicing> voicings;
    for (const model::Stop& s : organ.stops())
        voicings.push_back(model::buildRankVoicing(organ, s.id()));
    REQUIRE(voicings.size() >= 8);

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
    voice.prepare(kSr, kBlock);

    // Warm it up outside the counted scope: the first render touches lazily-sized
    // state, and that is a prepare-time cost rather than a per-note one.
    voice.adoptRank(&voicings[0]);
    voice.noteOn(core::PipeId{ 0, 60, 1 }, 100);
    (void) renderEnergy(voice, 2);
    voice.silence();

    long allocations = 0;
    {
        AllocationScope scope;
        // Every rank of the instrument, in turn, on the same voice -- which is
        // exactly what a slot coming off the free list does over a session.
        for (std::size_t i = 0; i < voicings.size(); ++i)
        {
            voice.adoptRank(&voicings[i]);
            voice.noteOn(core::PipeId{ static_cast<std::uint16_t>(i), 60, 1 }, 100);
            voice.noteOff();
            voice.silence();
        }
        allocations = scope.count();
    }

    INFO(allocations << " allocations across " << voicings.size() << " rank adoptions");
    CHECK(allocations == 0);
}

TEST_CASE("Rendering a per-rank voice allocates nothing",
          "[synthesis][rankvoice][regression]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const synth::RankVoicing v = model::buildRankVoicing(organ, core::StopId{ 6 });
    REQUIRE_FALSE(v.spectrum.partials.empty());

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
    voice.prepare(kSr, kBlock);
    voice.adoptRank(&v);
    voice.noteOn(core::PipeId{ 6, 60, 1 }, 100);
    (void) renderEnergy(voice, 2); // warm-up

    // The buffers are allocated OUTSIDE the counted scope. They are the host's,
    // handed to the callback; a helper that allocated them inside would have been
    // counting itself, which is what the first run of this test did.
    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    float* chans[2] = { l.data(), r.data() };

    long allocations = 0;
    {
        AllocationScope scope;
        for (int b = 0; b < 20; ++b)
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            core::AudioBlock block(chans, 2, kBlock);
            voice.renderAdd(block);
        }
        allocations = scope.count();
    }

    INFO(allocations << " allocations across 20 rendered blocks");
    CHECK(allocations == 0);
}

TEST_CASE("A voice really becomes the rank it adopts", "[synthesis][rankvoice]")
{
    // Two ranks of genuinely different character must not render the same thing.
    // If adoptRank silently failed -- a null handle, a wrong cast, a bank too small
    // -- everything would still sound, and it would all sound like whatever the
    // voice was seeded with.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    const model::Stop* flue = nullptr;
    const model::Stop* reed = nullptr;
    for (const model::Stop& s : organ.stops())
    {
        if (flue == nullptr && s.family() == core::TonalFamily::Principal
            && s.footage() == core::footage::kEight)
            flue = &s;
        if (reed == nullptr && s.family() == core::TonalFamily::Reed
            && s.footage() == core::footage::kEight)
            reed = &s;
    }
    REQUIRE(flue != nullptr);
    REQUIRE(reed != nullptr);

    const auto render = [&](core::StopId id)
    {
        const synth::RankVoicing v = model::buildRankVoicing(organ, id);
        synth::AdditiveVoice voice;
        voice.bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        voice.prepare(kSr, kBlock);
        voice.adoptRank(&v);
        voice.noteOn(core::PipeId{ static_cast<std::uint16_t>(id.value), 60, 1 }, 100);

        std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
        for (int b = 0; b < 30; ++b) // settle past the attack
        {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* chans[2] = { l.data(), r.data() };
            core::AudioBlock block(chans, 2, kBlock);
            voice.renderAdd(block);
        }
        return l;
    };

    const std::vector<float> a = render(flue->id());
    const std::vector<float> b = render(reed->id());

    double ea = 0.0, eb = 0.0, diff = 0.0;
    for (std::size_t i = 0; i < kBlock; ++i)
    {
        ea += static_cast<double>(a[i]) * a[i];
        eb += static_cast<double>(b[i]) * b[i];
        diff += std::abs(static_cast<double>(a[i] - b[i]));
    }
    REQUIRE(ea > 0.0);
    REQUIRE(eb > 0.0);
    CHECK(diff > 0.0); // a Principal and a Trompette are not the same signal
}

TEST_CASE("A key sounds one voice per engaged rank", "[synthesis][rankvoice][engine]")
{
    // The fan-out itself: one key event, several voices, one per drawn rank.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<synth::RankVoicing> voicings;
    for (const model::Stop& s : organ.stops())
        if (s.division().value == 1) // one manual's worth
            voicings.push_back(model::buildRankVoicing(organ, s.id()));
    REQUIRE(voicings.size() >= 4);

    engine::AudioEngine eng;
    eng.prepare(kSr, kBlock, 2, 1);

    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*> ptrs;
    for (std::size_t i = 0; i < 64; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        v->prepare(kSr, kBlock);
        ptrs.push_back(v.get());
        voices.push_back(std::move(v));
    }
    eng.bindVoices(ptrs.data(), ptrs.size());

    engine::EngagedRankTable table;
    for (const synth::RankVoicing& v : voicings)
        table.ranks[table.count++] = engine::EngagedRank{ &v, v.stop, v.division };
    eng.setEngagedRanks(table);

    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    const auto render = [&]
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* chans[2] = { l.data(), r.data() };
        core::AudioBlock block(chans, 2, kBlock);
        eng.processBlock(block);
    };

    render(); // picks up the table
    (void) eng.commandQueue().push(
        engine::EngineCommand::makeNoteOn(core::PipeId{ 0, 60, 1 }, 100,
                                          core::DivisionId{ 1 }, 0));
    render();

    INFO(eng.activeVoiceCount() << " voices for " << voicings.size() << " ranks");
    CHECK(eng.activeVoiceCount() == voicings.size());

    // And one note-off releases all of them, because it matches the KEY.
    (void) eng.commandQueue().push(
        engine::EngineCommand::makeNoteOff(core::PipeId{ 0, 60, 1 },
                                           core::DivisionId{ 1 }, 0));
    for (int b = 0; b < 80; ++b)
        render();
    CHECK(eng.activeVoiceCount() == 0);
}

TEST_CASE("A key either speaks in full or not at all",
          "[synthesis][rankvoice][engine][regression]")
{
    // Group admission. A chord where some ranks speak and others do not sounds
    // like a broken instrument -- a Tutti missing its mixtures -- and the player
    // has no way to tell what happened. A key that does not speak sounds like a
    // key that was not pressed.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    std::vector<synth::RankVoicing> voicings;
    for (const model::Stop& s : organ.stops())
        if (s.division().value == 1)
            voicings.push_back(model::buildRankVoicing(organ, s.id()));
    const std::size_t rankCount = voicings.size();
    REQUIRE(rankCount >= 4);

    engine::AudioEngine eng;
    eng.prepare(kSr, kBlock, 2, 1);

    // Deliberately too few slots for three whole keys.
    const std::size_t slots = rankCount * 2 + rankCount / 2;
    std::vector<std::unique_ptr<synth::AdditiveVoice>> voices;
    std::vector<core::IVoice*> ptrs;
    for (std::size_t i = 0; i < slots; ++i)
    {
        auto v = std::make_unique<synth::AdditiveVoice>();
        v->bank().setMaxPartials(synth::RankVoicing::kMaxPartials);
        v->prepare(kSr, kBlock);
        ptrs.push_back(v.get());
        voices.push_back(std::move(v));
    }
    eng.bindVoices(ptrs.data(), ptrs.size());

    engine::EngagedRankTable table;
    for (const synth::RankVoicing& v : voicings)
        table.ranks[table.count++] = engine::EngagedRank{ &v, v.stop, v.division };
    eng.setEngagedRanks(table);

    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    const auto render = [&]
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* chans[2] = { l.data(), r.data() };
        core::AudioBlock block(chans, 2, kBlock);
        eng.processBlock(block);
    };
    render();

    // Four keys into room for two and a half.
    for (std::uint8_t k = 0; k < 4; ++k)
    {
        (void) eng.commandQueue().push(engine::EngineCommand::makeNoteOn(
            core::PipeId{ 0, static_cast<std::uint8_t>(60 + k), 1 }, 100,
            core::DivisionId{ 1 }, 0));
        render();
    }

    // However many keys survived, each is complete: the active count must be a
    // whole multiple of the rank count.
    const std::size_t active = eng.activeVoiceCount();
    INFO(active << " active voices, rank count " << rankCount
                << ", slots " << slots);
    CHECK(active % rankCount == 0);
    CHECK(active > 0);
    CHECK(active <= slots);
}
