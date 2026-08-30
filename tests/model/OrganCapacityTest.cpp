// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief An organ larger than the instrument has to say so.
 *
 * Until an organ could come from a file, none of this was reachable: the only
 * organ was the one compiled into the binary, and it fits. Now that a user can
 * open their own, an eighty-stop cathedral organ is a document this instrument
 * will happily load -- and then silently be sixteen stops short of.
 *
 * The failure mode these guard is the quiet one. Not a crash, not a refusal: a
 * clean load, no diagnostic, and a Bombarde the organist can see in their own
 * file and can never draw.
 */

#include "caecilia/core/ParameterIds.h"
#include "caecilia/model/OrganDefinition.h"
#include "caecilia/registration/StopSet.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace caecilia;

namespace
{

/// A definition that validates clean at the given sizes, so that anything a test
/// sees reported is the capacity check and not incidental damage.
model::OrganDefinition organWith(std::size_t stops,
                                 std::size_t couplers = 0,
                                 std::size_t chests   = 1)
{
    model::OrganDefinition def;
    def.name = "Test";

    for (std::size_t i = 0; i < chests; ++i)
    {
        model::WindchestDef w;
        w.name = "chest" + std::to_string(i);
        def.windchests.push_back(w);
    }

    model::RankDef r;
    r.name      = "Montre";
    r.windchest = "chest0";
    def.ranks.push_back(r);

    for (const char* n : { "Grand-Orgue", "Recit" })
    {
        model::DivisionDef d;
        d.name = n;
        def.divisions.push_back(d);
    }

    for (std::size_t i = 0; i < stops; ++i)
    {
        model::StopDef s;
        s.name     = "Stop " + std::to_string(i);
        s.division = "Grand-Orgue";
        s.rank     = "Montre";
        def.stops.push_back(s);
    }

    for (std::size_t i = 0; i < couplers; ++i)
    {
        model::CouplerDef c;
        c.name = "Coupler " + std::to_string(i);
        c.from = "Recit";
        c.to   = "Grand-Orgue";
        def.couplers.push_back(c);
    }

    return def;
}

/// Does any diagnostic of this severity mention @p needle?
bool mentions(const model::LoadDiagnostics& diag,
              model::DiagnosticSeverity     severity,
              std::string_view              needle)
{
    for (const model::Diagnostic& d : diag.entries())
        if (d.severity == severity && d.message.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

TEST_CASE("An organ that fits the instrument is not complained about", "[model][capacity]")
{
    // The boundary itself, from below. A check that fires one stop early is as
    // wrong as one that never fires: it would refuse a legitimate organ.
    const model::LoadDiagnostics diag =
        organWith(core::params::kMaxStopParameters, core::params::kMaxCouplerParameters,
                  core::params::kMaxWindchests).validate();

    CHECK_FALSE(diag.hasErrors());
    CHECK_FALSE(mentions(diag, model::DiagnosticSeverity::Warning, "windchest"));
}

TEST_CASE("A stop past the registration word is an error, not a warning", "[model][capacity]")
{
    const model::LoadDiagnostics diag = organWith(core::params::kMaxStopParameters + 1).validate();

    // An error, because the stop cannot be reached from anywhere: not the mask,
    // not a host parameter, not the console. A warning would suggest degraded
    // operation, and this is absence.
    REQUIRE(diag.hasErrors());

    // And it names the first stop that is lost, because "65 stops, 64 addressable"
    // leaves the organist to count, and the point of the diagnostic is to spare
    // them exactly that.
    CHECK(mentions(diag, model::DiagnosticSeverity::Error,
                   "Stop " + std::to_string(core::params::kMaxStopParameters)));
}

TEST_CASE("The threshold is the width of the mask it protects", "[model][capacity]")
{
    // The number in validate() is not a policy anyone chose. It is the width of
    // the word the audio thread compares, and this is what ties the two together:
    // the last id the check permits is the last id a registration can hold.
    registration::StopSet fits;
    fits.insert(core::StopId{ static_cast<std::uint16_t>(core::params::kMaxStopParameters - 1) });
    CHECK(fits.fitsInMask());

    registration::StopSet past;
    past.insert(core::StopId{ static_cast<std::uint16_t>(core::params::kMaxStopParameters) });
    CHECK_FALSE(past.fitsInMask());
}

TEST_CASE("A coupler past the sixteenth is an error", "[model][capacity]")
{
    const model::LoadDiagnostics diag =
        organWith(4, core::params::kMaxCouplerParameters + 1).validate();

    // Sharper than the stops: the console refuses the index too, so unlike a
    // stop -- which at least exists in the organ's own tables -- there is no
    // surface anywhere that can engage this one.
    REQUIRE(diag.hasErrors());
    CHECK(mentions(diag, model::DiagnosticSeverity::Error,
                   "Coupler " + std::to_string(core::params::kMaxCouplerParameters)));
}

TEST_CASE("Too many windchests is a warning, because the organ still plays", "[model][capacity]")
{
    const model::LoadDiagnostics diag =
        organWith(4, 0, core::params::kMaxWindchests + 1).validate();

    // The distinction the severities carry: those ranks speak. What they lose is
    // whose wind they draw, which is a sag in the wrong place rather than a
    // missing stop -- so refusing the whole organ over it would be the worse
    // trade, and saying nothing would be the worse one still.
    CHECK_FALSE(diag.hasErrors());
    CHECK(mentions(diag, model::DiagnosticSeverity::Warning, "windchest"));
}
