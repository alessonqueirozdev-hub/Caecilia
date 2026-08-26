// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Core vocabulary + RT-plumbing tests.
//
// Footage is a stated correctness requirement (pitch-based selectors and
// build-plenum must never confuse a quint/tierce with an octave-related rank),
// so its reduction and classification are pinned here. The SpscRing is the sole
// sanctioned audio-thread command channel; its FIFO / full / empty contract is
// pinned too.
//

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/Version.h"
#include "caecilia/engine/SpscRing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using Catch::Approx;
namespace core = caecilia::core;

TEST_CASE("Footage stores an exact reduced rational", "[core][footage]")
{
    // Construction reduces to lowest terms with a positive denominator.
    const core::Footage half{8, 2};
    CHECK(half.num == 4);
    CHECK(half.den == 1);

    const core::Footage quint{16, 6}; // 8/3 after reduction
    CHECK(quint.num == 8);
    CHECK(quint.den == 3);

    // Equal fractions compare equal regardless of the raw terms given.
    CHECK(core::Footage{16, 6} == core::Footage{8, 3});
    CHECK(core::Footage{8, 1} == core::footage::kEight);
}

TEST_CASE("Footage::feet returns decimal length", "[core][footage]")
{
    CHECK(core::footage::kEight.feet() == Approx(8.0));
    CHECK(core::footage::kFour.feet() == Approx(4.0));
    CHECK(core::footage::kTwoAndTwoThird.feet() == Approx(2.6666666667).epsilon(1e-9));
    CHECK(core::footage::kOneAndThreeFifth.feet() == Approx(1.6));
}

TEST_CASE("Footage distinguishes octave-related pitches from mutations", "[core][footage]")
{
    // Octave-related footages: NOT mutations.
    CHECK_FALSE(core::footage::kThirtyTwo.isMutation());
    CHECK_FALSE(core::footage::kSixteen.isMutation());
    CHECK_FALSE(core::footage::kEight.isMutation());
    CHECK_FALSE(core::footage::kFour.isMutation());
    CHECK_FALSE(core::footage::kTwo.isMutation());
    CHECK_FALSE(core::footage::kOne.isMutation());

    // Quints and tierces ARE mutations.
    CHECK(core::footage::kFiveAndThird.isMutation());     // 5 1/3'
    CHECK(core::footage::kTwoAndTwoThird.isMutation());   // 2 2/3'
    CHECK(core::footage::kOneAndThreeFifth.isMutation()); // 1 3/5'
    CHECK(core::footage::kOneAndThird.isMutation());      // 1 1/3'
}

TEST_CASE("Footage::octaveClassFrom8 counts octaves from unison", "[core][footage]")
{
    CHECK(core::footage::kEight.octaveClassFrom8() == 0);
    CHECK(core::footage::kFour.octaveClassFrom8() == 1);
    CHECK(core::footage::kTwo.octaveClassFrom8() == 2);
    CHECK(core::footage::kOne.octaveClassFrom8() == 3);
    CHECK(core::footage::kSixteen.octaveClassFrom8() == -1);
    CHECK(core::footage::kThirtyTwo.octaveClassFrom8() == -2);

    // Mutations have no octave class and report 0 (must be filtered via
    // isMutation() before use).
    CHECK(core::footage::kTwoAndTwoThird.octaveClassFrom8() == 0);
}

TEST_CASE("SpscRing is a bounded FIFO with one reserved slot", "[core][spsc]")
{
    caecilia::core::engine::SpscRing<int, 4> ring; // usable capacity == 3

    CHECK(ring.capacity() == 3);
    CHECK(ring.empty());

    REQUIRE(ring.push(10));
    REQUIRE(ring.push(20));
    REQUIRE(ring.push(30));
    CHECK_FALSE(ring.empty());
    CHECK_FALSE(ring.push(40)); // full: item is dropped, not overwritten

    int out = 0;
    REQUIRE(ring.pop(out));
    CHECK(out == 10); // FIFO order
    REQUIRE(ring.pop(out));
    CHECK(out == 20);

    // Wrapping: freeing slots lets the producer enqueue again.
    REQUIRE(ring.push(40));
    REQUIRE(ring.push(50));
    CHECK_FALSE(ring.push(60)); // full again (holds 30, 40, 50)

    REQUIRE(ring.pop(out));
    CHECK(out == 30);
    REQUIRE(ring.pop(out));
    CHECK(out == 40);
    REQUIRE(ring.pop(out));
    CHECK(out == 50);

    CHECK(ring.empty());
    CHECK_FALSE(ring.pop(out)); // draining an empty ring reports failure
}

TEST_CASE("Version reports the scaffold revision", "[core][version]")
{
    CHECK(std::string(core::versionString()) == "0.0.1");
    CHECK(core::Version::major == 0);
    CHECK(core::Version::minor == 0);
    CHECK(core::Version::patch == 1);
}


TEST_CASE("SpscRing::peek reads without consuming, and drop consumes without reading",
          "[core][spsc]")
{
    // The audio thread needs to read an event's timestamp, decide it belongs to a
    // later slice of the block, and leave it queued. pop() cannot express that,
    // and getting the pair wrong is the kind of bug that hands the engine the same
    // command forever or silently loses one.
    caecilia::core::engine::SpscRing<int, 4> ring; // usable capacity == 3

    CHECK(ring.peek() == nullptr);   // nothing to look at
    CHECK_FALSE(ring.drop());        // and dropping is a no-op, not a corruption
    CHECK(ring.empty());

    REQUIRE(ring.push(11));
    REQUIRE(ring.push(22));

    // Two consecutive peeks must agree: a peek that consumed would return 22 here.
    const int* a = ring.peek();
    const int* b = ring.peek();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(*a == 11);
    CHECK(*b == 11);
    CHECK(a == b);
    CHECK_FALSE(ring.empty());

    CHECK(ring.drop());
    const int* c = ring.peek();
    REQUIRE(c != nullptr);
    CHECK(*c == 22);

    CHECK(ring.drop());
    CHECK(ring.peek() == nullptr);
    CHECK(ring.empty());

    // An unpaired drop on an empty ring must not advance the read index past the
    // write index. If it did, the ring would report Capacity-1 phantom items and
    // hand out stale commands from then on.
    CHECK_FALSE(ring.drop());
    CHECK(ring.empty());

    // Usable capacity is unchanged after a peek/drop drain.
    CHECK(ring.push(1));
    CHECK(ring.push(2));
    CHECK(ring.push(3));
    CHECK_FALSE(ring.push(4));       // still exactly Capacity - 1

    // peek and pop agree on what is next.
    const int* front = ring.peek();
    REQUIRE(front != nullptr);
    int popped = 0;
    REQUIRE(ring.pop(popped));
    CHECK(popped == 1);
    CHECK(*front == 1);              // the slot is producer-unreachable until now
}
