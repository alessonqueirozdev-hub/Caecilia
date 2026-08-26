// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// TripleBuffer — the wait-free handoff the audio thread publishes meters through,
// and which had no test at all.
//
// It replaced a two-slot flip, and the reason is the last case in this file: with
// two slots the writer eventually wraps back onto the slot the reader is halfway
// through copying, and the reader gets half of one snapshot and half of another.
// For a meter that is a flickering VU. For anything carrying related fields — a
// peak beside the frame position it belongs to — it is a value that never existed.
//
// That failure is a race, so it cannot be caught by calling methods in order. The
// last case runs a real writer thread against a real reader and checks a property
// only an intact snapshot can have.
//

#include "caecilia/core/TripleBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>

namespace core = caecilia::core;

namespace
{
/// Three fields that a writer always sets to the same value. Any read that finds
/// them disagreeing saw two different publications spliced together.
struct Triple
{
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};
} // namespace

TEST_CASE("A TripleBuffer starts empty and reads a default", "[core][triplebuffer]")
{
    core::TripleBuffer<int> tb;
    CHECK_FALSE(tb.hasFresh());
    CHECK(tb.read() == 0);
}

TEST_CASE("A TripleBuffer hands over the value that was written", "[core][triplebuffer]")
{
    core::TripleBuffer<int> tb;
    tb.write(42);
    CHECK(tb.hasFresh());
    CHECK(tb.read() == 42);
}

TEST_CASE("Reading twice without a write repeats the last value",
          "[core][triplebuffer]")
{
    // The console polls at its own rate, which is slower than the audio thread
    // publishes. A poll that found nothing new must return the last complete
    // frame, not a default-constructed one — a meter that blinked to zero between
    // updates would be worse than no meter.
    core::TripleBuffer<int> tb;
    tb.write(7);
    CHECK(tb.read() == 7);
    CHECK_FALSE(tb.hasFresh());
    CHECK(tb.read() == 7);
    CHECK(tb.read() == 7);
}

TEST_CASE("A reader that falls behind gets the newest value, not a queue",
          "[core][triplebuffer]")
{
    // This is a handoff, not a channel. The audio thread publishes every block and
    // the UI reads thirty times a second; the intervening frames are meant to be
    // dropped, not accumulated.
    core::TripleBuffer<int> tb;
    for (int i = 1; i <= 100; ++i)
        tb.write(i);

    CHECK(tb.hasFresh());
    CHECK(tb.read() == 100);
    CHECK_FALSE(tb.hasFresh());
}

TEST_CASE("hasFresh reports exactly whether a read would find something new",
          "[core][triplebuffer]")
{
    // The audio thread uses this to decide whether a read is worth doing at all,
    // so a false positive costs work and a false negative loses a publication.
    core::TripleBuffer<int> tb;
    CHECK_FALSE(tb.hasFresh());

    tb.write(1);
    CHECK(tb.hasFresh());
    CHECK(tb.hasFresh()); // asking does not consume

    (void) tb.read();
    CHECK_FALSE(tb.hasFresh());

    tb.write(2);
    CHECK(tb.hasFresh());
    (void) tb.read();
    CHECK_FALSE(tb.hasFresh());
}

TEST_CASE("A concurrent reader never sees a spliced snapshot",
          "[core][triplebuffer][regression]")
{
    // The whole reason this class exists. With two slots the writer wraps back
    // onto the slot the reader is mid-copy on, and the reader assembles half of
    // one publication and half of another — a value that was never published.
    //
    // Nothing here can be verified by ordering calls; it needs two real threads
    // and a property only an intact snapshot has. The writer always sets all three
    // fields to the same number, so any disagreement is a splice.
    core::TripleBuffer<Triple> tb;
    tb.write(Triple{ 1, 1, 1 });

    std::atomic<bool> stop{ false };
    std::atomic<std::uint64_t> reads{ 0 };
    std::atomic<std::uint64_t> spliced{ 0 };

    std::thread writer([&]
    {
        for (std::uint32_t i = 2; !stop.load(std::memory_order_relaxed); ++i)
            tb.write(Triple{ i, i, i });
    });

    std::thread reader([&]
    {
        while (!stop.load(std::memory_order_relaxed))
        {
            const Triple t = tb.read();
            if (t.a != t.b || t.b != t.c)
                spliced.fetch_add(1, std::memory_order_relaxed);
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    reader.join();

    INFO(reads.load() << " reads, " << spliced.load() << " spliced");
    REQUIRE(reads.load() > 1000); // the test actually exercised something
    CHECK(spliced.load() == 0);
}
