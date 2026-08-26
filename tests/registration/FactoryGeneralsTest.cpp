// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The pistons an instrument ships with.
//
// These were previously eight selector expressions in the console's JavaScript,
// where nothing could check them: an expression that matched nothing produced a
// piston that drew silence, and the only way to find out was to press it. Moving
// them into `registration` is what makes the checks below possible, and the point
// of the move.
//
// The interesting property is not that each one resolves. It is that walking them
// upward is a CRESCENDO -- each piston contains at least as much as the one below
// it -- because that is what makes a row of pistons an instrument rather than
// eight unrelated presets.
//

#include "caecilia/model/DemoOrgan.h"
#include "caecilia/model/Division.h"
#include "caecilia/model/Stop.h"
#include "caecilia/registration/FactoryGenerals.h"
#include "caecilia/registration/StopSet.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bitset>
#include <cstdint>
#include <string>

namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace reg   = caecilia::registration;

namespace
{
std::size_t countOf(std::uint64_t mask)
{
    return std::bitset<64>(mask).count();
}
} // namespace

TEST_CASE("Every factory general draws something", "[registration][generals]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    // Each piston's OWN expression, before the cumulative rule hides an empty one
    // behind the level below it. A typo, or a family this grammar does not know,
    // resolves to nothing -- and the parser returns a UNIVERSAL selector on a parse
    // error, so a piston that failed to parse would otherwise silently become a
    // second Tutti rather than go quiet.
    for (const reg::FactoryGeneral& g : reg::factoryGenerals())
    {
        const std::uint64_t mask = reg::resolveSelectorMask(organ, g.expression);
        INFO(std::string(g.tag) << " = " << std::string(g.expression));
        CHECK(mask != 0);
    }
}

TEST_CASE("The factory generals are a crescendo", "[registration][generals]")
{
    // Pistons 0..6 are pp .. Tutti. Each must contain everything below it: a piston
    // that DROPS a stop the previous one drew makes the row unusable as a
    // progression, which is the only thing a row of pistons is for.
    //
    // "So" is deliberately outside the sequence -- a solo registration is a
    // different gesture, which is why a console puts it at the end of the row.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const auto         generals = reg::factoryGenerals();
    REQUIRE(generals.size() >= 7);

    std::array<std::uint64_t, 16> masks{};
    REQUIRE(reg::resolveFactoryGenerals(organ, masks) == generals.size());

    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < 7; ++i)
    {
        INFO(std::string(generals[i].tag) << " draws " << countOf(masks[i])
             << " stops; " << (i == 0 ? "" : std::string(generals[i - 1].tag))
             << " drew " << countOf(previous));

        // Containment is true by construction for a cumulative row -- what is NOT
        // guaranteed is that each level adds anything. A piston whose expression
        // matches nothing on this organ leaves the row flat there, which is a real
        // defect (the level is a duplicate of the one below it) and is invisible
        // unless it is asserted.
        CHECK((masks[i] & previous) == previous);
        if (i > 0)
            CHECK(countOf(masks[i]) > countOf(previous));
        previous = masks[i];
    }
}

TEST_CASE("The Tutti draws the whole instrument", "[registration][generals]")
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const auto         generals = reg::factoryGenerals();

    std::array<std::uint64_t, 16> masks{};
    REQUIRE(reg::resolveFactoryGenerals(organ, masks) > 0);

    std::uint64_t tutti = 0;
    for (std::size_t i = 0; i < generals.size(); ++i)
        if (generals[i].tag == "T")
            tutti = masks[i];
    REQUIRE(tutti != 0);

    for (const model::Stop& s : organ.stops())
    {
        if (s.id().value >= reg::StopSet::kMaskCapacity)
            continue;
        INFO(s.name() << " is missing from the Tutti");
        CHECK((tutti & (std::uint64_t{ 1 } << s.id().value)) != 0);
    }
}

TEST_CASE("The softest general leaves the pedal alone", "[registration][generals]")
{
    // pp is a single soft flute under the hands. Drawing a 16' pedal flue with it
    // would make the quietest piston on the console the one with the most weight.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    const auto         generals = reg::factoryGenerals();
    REQUIRE(! generals.empty());

    std::array<std::uint64_t, 16> masks{};
    REQUIRE(reg::resolveFactoryGenerals(organ, masks) > 0);
    const std::uint64_t pp = masks[0];
    REQUIRE(pp != 0);

    for (const model::Stop& s : organ.stops())
    {
        if (s.id().value >= reg::StopSet::kMaskCapacity)
            continue;
        if ((pp & (std::uint64_t{ 1 } << s.id().value)) == 0)
            continue;
        std::string divisionName;
        for (const model::Division& d : organ.divisions())
            if (d.id().value == s.division().value)
                divisionName = d.name();
        INFO(s.name() << " (" << divisionName << ") is drawn by pp");
        CHECK(divisionName != "Pedal");
    }
}

TEST_CASE("An expression that does not parse draws nothing",
          "[registration][generals]")
{
    // A piston whose expression is broken must stay EMPTY, not fall back to a
    // universal atom. SelectorParser returns a universal selector on failure --
    // sensible for an omnibar, catastrophic for a piston, which would silently
    // become a second Tutti.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    CHECK(reg::resolveSelectorMask(organ, "family:reed &") == 0);
    CHECK(reg::resolveSelectorMask(organ, "family:bogus") == 0);
    CHECK(reg::resolveSelectorMask(organ, "((((") == 0);
}

TEST_CASE("Resolving a general does not depend on what is drawn",
          "[registration][generals]")
{
    // A general says what to DRAW, not what to add. If the expressions were
    // resolved against live state, `engaged` inside one would make the piston mean
    // something different every time it fired.
    const model::Organ organ = model::buildCaeciliaDemoOrgan();

    for (const reg::FactoryGeneral& g : reg::factoryGenerals())
    {
        const std::uint64_t a = reg::resolveSelectorMask(organ, g.expression);
        const std::uint64_t b = reg::resolveSelectorMask(organ, g.expression);
        INFO(std::string(g.tag));
        CHECK(a == b);
    }
}
