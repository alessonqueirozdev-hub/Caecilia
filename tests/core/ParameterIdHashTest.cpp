// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The parameter contract, pinned.
//
// A parameter ID is the most permanent string this plugin owns. A host keys its
// automation lanes and its saved sessions on the string, so renaming one does not
// migrate anything — the host looks for a parameter that is no longer there and
// drops the lane. Every project a user has ever saved is downstream of these
// spellings.
//
// Two different failures are worth catching, and only one of them is a count:
//
//   * the SET changing — a parameter added, removed, or the pool resized;
//   * a SPELLING changing while the count does not. Swap two IDs and the set is
//     still 97 entries; every session that automated either one is still broken.
//
// So the count is asserted, and the IDs at the edges of the set are pinned to a
// number.
//

#include "caecilia/core/ParameterIds.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace params = caecilia::core::params;

TEST_CASE("The published parameter set is exactly 97 IDs", "[core][params]")
{
    // 17 globals + 64 stops + 16 couplers. The pool was 256 and is 64 because that
    // is the width of the registration mask the audio thread compares; the EQ's six
    // sit inside the globals rather than after the pool, so resizing the pool never
    // moves them -- and the couplers sit AFTER it, which is safe for the opposite
    // reason: the pool's size is architectural now and cannot be resized again.
    STATIC_REQUIRE(params::kGlobalIds.size() == 17u);
    STATIC_REQUIRE(params::kMaxStopParameters == 64u);
    STATIC_REQUIRE(params::kMaxCouplerParameters == 16u);
    STATIC_REQUIRE(params::kParameterCount == 97u);

    std::vector<std::string> ids;
    for (const char* id : params::kGlobalIds)
        ids.emplace_back(id);
    for (std::size_t i = 0; i < params::kMaxStopParameters; ++i)
        ids.emplace_back(params::stopParamId(i).c_str());
    for (std::size_t i = 0; i < params::kMaxCouplerParameters; ++i)
        ids.emplace_back(params::couplerParamId(i).c_str());

    REQUIRE(ids.size() == 97u);

    // Every ID distinct as a STRING...
    const std::set<std::string> unique(ids.begin(), ids.end());
    INFO(ids.size() - unique.size() << " duplicate IDs");
    CHECK(unique.size() == ids.size());

    // ...and distinct as a HASH, which is what a host-side map would collide on.
    std::set<std::uint32_t> hashes;
    for (const std::string& id : ids)
        hashes.insert(params::parameterIdHash(id));
    INFO(ids.size() - hashes.size() << " hash collisions across the published set");
    CHECK(hashes.size() == ids.size());
}

TEST_CASE("The IDs at the edges of the set are what they have always been",
          "[core][params]")
{
    // Pinned to numbers rather than to spellings, because a test that compares a
    // string to itself passes however both are edited. Recompute one of these and
    // it means the contract moved — which is allowed exactly once, before the first
    // release, and never afterwards.
    STATIC_REQUIRE(params::parameterIdHash(params::kMasterGainDb) == 0x5FA85061u);
    STATIC_REQUIRE(params::parameterIdHash(params::kReverbMix)    == 0xD06E866Fu);
    STATIC_REQUIRE(params::parameterIdHash(params::kTemperament)  == 0x13295A5Eu);
    STATIC_REQUIRE(params::parameterIdHash(params::kEqAirDb)      == 0xA6579286u);

    // The first and last slot of the pool. `stop_255` used to be pinned here and no
    // longer exists: the pool shrank from 256 to 64.
    STATIC_REQUIRE(params::parameterIdHash(params::stopParamId(0).view())  == 0x663A2073u);
    STATIC_REQUIRE(params::parameterIdHash(params::stopParamId(63).view()) == 0x663A2130u);

    // The coupler pool, appended after the stops.
    STATIC_REQUIRE(params::parameterIdHash(params::couplerParamId(0).view())  == 0x1EC634ADu);
    STATIC_REQUIRE(params::parameterIdHash(params::couplerParamId(15).view()) == 0x1EC634D1u);
}

TEST_CASE("Stop IDs are formatted exactly as snprintf produced them",
          "[core][params]")
{
    // The old implementation was snprintf("stop_%03zu"), returning a std::string.
    // The constexpr replacement takes the index modulo 1000, which is identical for
    // every index below 1000 -- and the pool is 64, so identical everywhere it is
    // used. Asserted against the original rather than argued about.
    for (std::size_t i = 0; i < params::kMaxStopParameters; ++i)
    {
        char expected[16];
        std::snprintf(expected, sizeof(expected), "stop_%03zu", i);

        INFO("slot " << i);
        CHECK(std::string(params::stopParamId(i).c_str()) == std::string(expected));
    }

    // And the two disagree only where snprintf would have needed more digits, which
    // is past the pool and past any plausible organ.
    CHECK(std::string(params::stopParamId(999).c_str()) == "stop_999");
    CHECK(std::string(params::stopParamId(1000).c_str()) == "stop_000");

    for (std::size_t i = 0; i < params::kMaxCouplerParameters; ++i)
    {
        char expected[16];
        std::snprintf(expected, sizeof(expected), "coupler_%02zu", i);
        INFO("coupler slot " << i);
        CHECK(std::string(params::couplerParamId(i).c_str()) == std::string(expected));
    }
}

TEST_CASE("Every ID is well formed for a host", "[core][params]")
{
    // Hosts have been known to mangle anything that is not [a-z0-9_]: VST3 carries
    // the ID through a hashed 32-bit tag, but AU and the plugin's own state XML
    // carry the STRING, and an XML attribute name cannot start with a digit.
    std::vector<std::string> ids;
    for (const char* id : params::kGlobalIds)
        ids.emplace_back(id);
    for (std::size_t i = 0; i < params::kMaxStopParameters; ++i)
        ids.emplace_back(params::stopParamId(i).c_str());
    for (std::size_t i = 0; i < params::kMaxCouplerParameters; ++i)
        ids.emplace_back(params::couplerParamId(i).c_str());

    for (const std::string& id : ids)
    {
        INFO(id);
        REQUIRE(! id.empty());
        CHECK(id.size() <= 32u);                 // comfortably inside every format
        CHECK((id[0] >= 'a' && id[0] <= 'z'));   // never a digit, never punctuation
        for (const char c : id)
            CHECK(((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'));
    }
}
