/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

//
// Core vocabulary + RT-plumbing tests.
//
// Footage is a stated correctness requirement (pitch-based selectors and
// build-plenum must never confuse a quint/tierce with an octave-related rank),
// so its reduction and classification are pinned here. The SpscRing is the sole
// sanctioned audio-thread command channel; its FIFO / full / empty contract is
// pinned too.
//

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/core/Version.h"
#include "ceciliae/engine/SpscRing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using Catch::Approx;
namespace core = ceciliae::core;

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
    ceciliae::core::engine::SpscRing<int, 4> ring; // usable capacity == 3

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
