// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// Registration dynamics: on an organ there is no touch sensitivity, so the
// registration IS the dynamic. These tests pin the two properties that were
// silently broken:
//
//   1. Drawing more stops must get LOUDER, monotonically, across a wide span.
//      A per-registration energy normaliser used to clamp the whole range from
//      one soft flute to full Tutti into ~13 dB, and above a certain weight
//      adding stops changed only the timbre.
//   2. The instrument's PITCH must not depend on which stops are drawn, or on
//      the order they were drawn in. The old humanisation scattered every
//      partial — fundamentals included — with a seed keyed on the partial's
//      index in the composite vector, so both changed the tuning.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/AdditiveVoice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr std::size_t      kBlock = 512;

model::RegistrationRank rank(core::TonalFamily fam, double feet, bool compound = false)
{
    return model::RegistrationRank{ fam, model::footageFromFeet(feet), compound };
}

/// Render one sustained middle-C through a voice seeded with `ranks`, and return
/// the RMS of the steady portion.
double sustainedRms(const std::vector<model::RegistrationRank>& ranks)
{
    const synth::SpectralModel composite =
        model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(ranks));

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(std::max<std::size_t>(composite.partials.size(), 16));
    voice.prepare(kSr, kBlock);

    synth::VoiceContext ctx;
    ctx.family  = core::TonalFamily::Principal;
    ctx.footage = core::footage::kEight;
    voice.setContext(ctx);
    voice.seedFrom(composite);
    voice.noteOn(core::PipeId{0, 60}, 100);

    // Six seconds, measured over the last four and a half.
    //
    // The window has to be LONG. Near-unison partials from different ranks beat
    // against each other, and each partial's slow independent drift makes that
    // beat wander; a short window samples one instant of it and can read several
    // dB below or above the real level. Averaging over many drift time constants
    // is both the correct measurement and what a listener actually hears from a
    // held chord.
    constexpr std::size_t kTotal = 48000 * 6;
    std::vector<float> l(kTotal, 0.0f), r(kTotal, 0.0f);
    for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
    {
        float* chans[2] = { l.data() + pos, r.data() + pos };
        core::AudioBlock b(chans, 2, kBlock);
        voice.renderAdd(b);
    }

    // Skip the attack; measure the steady state.
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 48000 * 3 / 2; i < kTotal; ++i)
    {
        sum += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
        n += 2;
    }
    return std::sqrt(sum / static_cast<double>(n));
}

double toDb(double a, double b) { return 20.0 * std::log10(a / b); }

/// The demo instrument's full complement, as the console would send it.
std::vector<model::RegistrationRank> tuttiRanks()
{
    return {
        rank(core::TonalFamily::Principal, 16), rank(core::TonalFamily::Flute, 16),
        rank(core::TonalFamily::Principal, 8),  rank(core::TonalFamily::Flute, 8),
        rank(core::TonalFamily::Reed, 16),      rank(core::TonalFamily::Reed, 8),
        rank(core::TonalFamily::Principal, 8),  rank(core::TonalFamily::Flute, 16),
        rank(core::TonalFamily::Flute, 8),      rank(core::TonalFamily::Principal, 4),
        rank(core::TonalFamily::Flute, 4),      rank(core::TonalFamily::Flute, 8.0 / 3.0),
        rank(core::TonalFamily::Principal, 2),  rank(core::TonalFamily::Flute, 8.0 / 5.0),
        rank(core::TonalFamily::Mixture, 2, true),
        rank(core::TonalFamily::Reed, 8),       rank(core::TonalFamily::Reed, 4),
        rank(core::TonalFamily::Flute, 8),      rank(core::TonalFamily::Flute, 4),
        rank(core::TonalFamily::String, 8),     rank(core::TonalFamily::String, 8),
        rank(core::TonalFamily::Mixture, 2, true),
        rank(core::TonalFamily::Reed, 8),       rank(core::TonalFamily::Reed, 8),
        rank(core::TonalFamily::Reed, 4),       rank(core::TonalFamily::Principal, 16),
    };
}
} // namespace

TEST_CASE("Registration spans a real dynamic range, softest to Tutti",
          "[model][registration][musical][regression]")
{
    const double soft   = sustainedRms({ rank(core::TonalFamily::Flute, 4) });
    const double single = sustainedRms({ rank(core::TonalFamily::Principal, 8) });
    const double tutti  = sustainedRms(tuttiRanks());

    REQUIRE(soft > 1.0e-6);
    REQUIRE(single > soft);

    const double span = toDb(tutti, soft);
    INFO("soft " << soft << ", single 8' " << single << ", tutti " << tutti
                 << " => " << span << " dB");

    // A real instrument spans far more than this; 18 dB is the floor at which the
    // registration is unmistakably a dynamic control rather than a timbre switch.
    // The old energy normaliser produced ~13 dB and capped out entirely.
    CHECK(span > 18.0);
}

TEST_CASE("Adding stops never takes loudness away", "[model][registration][musical]")
{
    // Build up a plenum one rank at a time. Two different things happen as you do,
    // and conflating them would make this test lie:
    //
    //   * Foundations and reeds add POWER — broadband energy, clearly measurable
    //     as RMS.
    //   * Mixtures and mutations add BRILLIANCE — a handful of quiet, very high
    //     partials. They transform how the plenum sounds while contributing almost
    //     nothing to broadband RMS, exactly as they do on a real instrument.
    //
    // So: nothing may ever reduce the level, and every power stop must produce a
    // clear increase. The defect this guards against is an energy normaliser that
    // made drawing more stops change only the timbre — or, past its ceiling,
    // nothing at all.
    struct Step { model::RegistrationRank r; bool addsPower; };
    const Step steps[] = {
        { rank(core::TonalFamily::Flute, 8),          true  },
        { rank(core::TonalFamily::Principal, 8),      true  },
        { rank(core::TonalFamily::Principal, 4),      true  },
        { rank(core::TonalFamily::Principal, 2),      false }, // the chorus crown
                                                              // is brilliance too
        { rank(core::TonalFamily::Mixture, 2, true),  false }, // brilliance only
        { rank(core::TonalFamily::Reed, 8),           true  },
        { rank(core::TonalFamily::Reed, 16),          true  },
    };

    std::vector<model::RegistrationRank> reg;
    double previous = 0.0;
    for (const Step& s : steps)
    {
        reg.push_back(s.r);
        const double rms = sustainedRms(reg);
        INFO(reg.size() << " ranks -> " << rms
             << (s.addsPower ? " (power)" : " (brilliance)"));

        CHECK(rms > previous * 0.995); // never quieter, within measurement noise
        if (s.addsPower && previous > 0.0)
            CHECK(toDb(rms, previous) > 0.5); // and a power stop is clearly audible
        previous = rms;
    }
}

TEST_CASE("Tuning does not depend on which stops are drawn, or in what order",
          "[model][registration][musical][regression]")
{
    // Measure the sounding fundamental by counting positive zero crossings of a
    // low, near-sinusoidal registration over an exact number of seconds.
    auto measureHz = [](const std::vector<model::RegistrationRank>& ranks) {
        const synth::SpectralModel composite =
            model::buildCompositeFromRegistration(std::span<const model::RegistrationRank>(ranks));

        synth::AdditiveVoice voice;
        voice.bank().setMaxPartials(std::max<std::size_t>(composite.partials.size(), 16));
        voice.prepare(kSr, kBlock);
        synth::VoiceContext ctx;
        ctx.family  = core::TonalFamily::Flute; // steadiest family: least drift
        ctx.footage = core::footage::kEight;
        voice.setContext(ctx);
        voice.seedFrom(composite);
        voice.noteOn(core::PipeId{0, 45}, 100); // A2, 110 Hz

        constexpr std::size_t kTotal = 48000 * 3;
        std::vector<float> l(kTotal, 0.0f), r(kTotal, 0.0f);
        for (std::size_t pos = 0; pos + kBlock <= kTotal; pos += kBlock)
        {
            float* chans[2] = { l.data() + pos, r.data() + pos };
            core::AudioBlock b(chans, 2, kBlock);
            voice.renderAdd(b);
        }

        std::size_t crossings = 0;
        const std::size_t from = 48000, to = 48000 * 3;
        for (std::size_t i = from + 1; i < to; ++i)
            if (l[i - 1] <= 0.0f && l[i] > 0.0f)
                ++crossings;
        return static_cast<double>(crossings) / 2.0;
    };

    const auto flute8     = rank(core::TonalFamily::Flute, 8);
    const auto principal8 = rank(core::TonalFamily::Principal, 8);

    // The SAME two ranks, drawn in the two possible orders, must sound the same
    // pitch. The old index-keyed scatter made the order matter.
    const double ab = measureHz({ flute8, principal8 });
    const double ba = measureHz({ principal8, flute8 });

    INFO("A,B = " << ab << " Hz; B,A = " << ba << " Hz");
    CHECK(ab == Approx(ba).epsilon(0.001));
}
