// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

/**
 * @file
 * @brief The organ file's windSensitivity, which used to reach nothing.
 *
 * `voicing.windSensitivity` has been in the organ-file format since the format
 * existed. It is parsed, held on RankVoicingSpec, written back out on a save, and
 * documented -- by me, an hour before this was written -- as controlling how
 * strongly a rank's pitch and level follow the wind. Nothing read it. A rank that
 * asked to breathe twice as hard breathed exactly as much as one that asked not to
 * breathe at all.
 *
 * It now scales the whole per-family wind response. The default of 0.5 leaves the
 * family curve unchanged, so an organ that never mentions it sounds exactly as it
 * did; 1 is twice as responsive and 0 is a rank that does not notice the wind.
 *
 * Measured against a fixed deviation rather than the real reservoir, because the
 * sag a real registration produces is a fraction of a percent and the pitch that
 * follows from it is below the noise floor of any measurement taken through the
 * master chain. Five percent is a wind failure, not a chord -- and it is what makes
 * the WIRING measurable, which is what is being tested.
 */

#include "caecilia/core/IWindSupply.h"
#include "caecilia/model/OrganLoader.h"

#include "support/Spectrum.h"

#include <catch2/catch_test_macros.hpp>

using namespace caecilia;
using namespace caecilia::testing;

namespace
{

/// A chest whose pressure is wherever the test puts it.
class FixedWind final : public core::IWindSupply
{
public:
    explicit FixedWind(float deviation) : deviation_(deviation) {}

    [[nodiscard]] float nominalPressurePa(core::WindchestId) const noexcept override
    {
        return 812.0f;
    }
    [[nodiscard]] float pressureAt(core::WindchestId, std::size_t) const noexcept override
    {
        return 812.0f * (1.0f + deviation_);
    }
    [[nodiscard]] float pressureDeviation(core::WindchestId, std::size_t) const noexcept override
    {
        return deviation_;
    }
    [[nodiscard]] core::WindchestId chestForPipe(core::PipeId) const noexcept override
    {
        return core::WindchestId{ 0 };
    }
    void registerDemand(core::WindchestId, float) noexcept override {}
    void step(std::size_t) noexcept override {}
    void setChestTremulantEnabled(core::WindchestId, bool) noexcept override {}
    void setChestTremulantShape(core::WindchestId, float, float) noexcept override {}

private:
    float deviation_ = 0.0f;
};

constexpr int kMiddleC = 60;

/// The pitch a Montre sounds at, with @p sensitivity, on wind @p deviation off
/// nominal.
double pitchAt(float sensitivity, float deviation)
{
    const model::Organ organ = model::buildCaeciliaDemoOrgan();
    synth::RankVoicing v     = voicingNamed(organ, "Montre 8");
    REQUIRE_FALSE(v.spectrum.partials.empty());
    v.windSensitivity = sensitivity;

    FixedWind wind(deviation);

    synth::VoiceContext ctx;
    ctx.wind    = &wind;
    ctx.footage = v.footage;
    ctx.family  = v.family;

    const std::vector<float> audio = renderRankNote(v, kMiddleC, 0.5, 48000.0, &ctx);
    return estimateHz(audio, noteHz(kMiddleC));
}

} // namespace

TEST_CASE("Losing wind takes a pipe flat", "[synthesis][wind]")
{
    // The direction, first: a pipe on less pressure has a slower jet and speaks
    // flatter. An organ whose pitch rose as the wind failed would be modelling
    // something, but not an organ.
    const double nominal = pitchAt(0.5f,  0.0f);
    const double starved = pitchAt(0.5f, -0.05f);

    INFO("five percent down: " << centsBetween(nominal, starved) << " cents");
    CHECK(centsBetween(nominal, starved) < -0.3);
}

TEST_CASE("A rank that asks to breathe harder does", "[synthesis][wind]")
{
    // What the organ file could not do until now. The same rank, the same wind,
    // three different answers to how much it should care.
    const double deaf   = centsBetween(pitchAt(0.0f, 0.0f), pitchAt(0.0f, -0.05f));
    const double normal = centsBetween(pitchAt(0.5f, 0.0f), pitchAt(0.5f, -0.05f));
    const double eager  = centsBetween(pitchAt(1.0f, 0.0f), pitchAt(1.0f, -0.05f));

    INFO("wind sensitivity 0 / 0.5 / 1 moves the pitch by "
         << deaf << " / " << normal << " / " << eager << " cents");

    // Zero means deaf to the wind: the pipe holds its pitch through a wind failure.
    CHECK(std::abs(deaf) < 0.1);

    // And one is twice the default, not merely more -- because the default has to
    // stay exactly where it was, so 0.5 maps to the family curve unchanged and the
    // scale is linear from there.
    CHECK(eager < normal * 1.7);
    CHECK(eager > normal * 2.3);
}

TEST_CASE("The organ file is where the number comes from", "[synthesis][wind]")
{
    // The tests above set the sensitivity straight onto the voicing, which proves
    // the mechanism and not the path. This one writes it into a DOCUMENT, reads
    // the document back, and renders what comes out -- because a key that works
    // in C++ and not in the file format is the thing this whole session keeps
    // finding.
    const auto pitchFromDocument = [](float sensitivity, float deviation)
    {
        model::OrganDefinition def;
        def.name = "Breathing";

        model::WindchestDef w;
        w.name = "Chest";
        def.windchests.push_back(w);

        model::RankDef r;
        r.name                     = "Montre 8";
        r.windchest                = "Chest";
        r.voicing.windSensitivity  = sensitivity;
        def.ranks.push_back(r);

        model::DivisionDef d;
        d.name = "Manual";
        def.divisions.push_back(d);

        model::StopDef s;
        s.name     = "Montre 8";
        s.division = "Manual";
        s.rank     = "Montre 8";
        def.stops.push_back(s);

        model::CompileResult compiled =
            model::OrganLoader::load(model::OrganLoader::serialize(def));
        REQUIRE(compiled.ok());

        const synth::RankVoicing v =
            model::buildRankVoicing(*compiled.organ, core::StopId{ 0 });

        FixedWind wind(deviation);
        synth::VoiceContext ctx;
        ctx.wind    = &wind;
        ctx.footage = v.footage;
        ctx.family  = v.family;

        return estimateHz(renderRankNote(v, kMiddleC, 0.5, 48000.0, &ctx), noteHz(kMiddleC));
    };

    const double deaf  = centsBetween(pitchFromDocument(0.0f, 0.0f),
                                      pitchFromDocument(0.0f, -0.05f));
    const double eager = centsBetween(pitchFromDocument(1.0f, 0.0f),
                                      pitchFromDocument(1.0f, -0.05f));

    INFO("from a document: 0 moves " << deaf << " cents, 1 moves " << eager);

    CHECK(std::abs(deaf) < 0.1);
    CHECK(eager < -0.5);
}
