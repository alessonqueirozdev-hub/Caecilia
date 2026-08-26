// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The two axes of the wind response that nothing ever read.
//
// wind::WindResponseCurve carries four coefficients per tonal family, with
// per-family values and a paragraph each explaining what they are for, and the
// bank applied two of them. brightnessPerDeviation and attackPerDeviation were
// computed and consumed by nobody; PartialTrack::brightnessTrack, authored per
// partial by every recipe in the model, carried its own @todo saying so.
//
// So an organ under a tutti went flat and quiet as the reservoir gave way and did
// not go DULL, which is the part an organist actually notices -- a pipe's harmonic
// development tracks its jet velocity, and take the wind away and the upper
// partials go first. Nor did the pipes speak any more slowly, which is the other
// half of what losing the wind sounds like.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/core/IWindSupply.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/wind/WindResponseCurve.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace synth = caecilia::synth;
namespace wind  = caecilia::wind;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr double           kTwoPi = 6.283185307179586476925286766559;
constexpr double           kF0    = 220.0;

/// A reservoir pinned at whatever deviation a test wants. The real WindModel is a
/// differential equation; this is the one number the bank reads out of it.
class FixedWind final : public core::IWindSupply
{
public:
    explicit FixedWind(float deviation) noexcept : dev_(deviation) {}

    [[nodiscard]] float nominalPressurePa(core::WindchestId) const noexcept override
    {
        return 800.0f;
    }
    [[nodiscard]] float pressureAt(core::WindchestId, std::size_t) const noexcept override
    {
        return 800.0f * (1.0f + dev_);
    }
    [[nodiscard]] float pressureDeviation(core::WindchestId, std::size_t) const noexcept override
    {
        return dev_;
    }
    [[nodiscard]] core::WindchestId chestForPipe(core::PipeId) const noexcept override
    {
        return core::WindchestId{};
    }
    void registerDemand(core::WindchestId, float) noexcept override {}
    void step(std::size_t) noexcept override {}
    void setChestTremulantEnabled(core::WindchestId, bool) noexcept override {}
    void setChestTremulantShape(core::WindchestId, float, float) noexcept override {}

private:
    float dev_ = 0.0f;
};

/// A rank: a fundamental and harmonics, with the brightness tracking the model's
/// own recipes author (0.02 per harmonic, capped).
synth::SpectralModel rank(std::size_t harmonics, float trackScale = 0.02f)
{
    synth::SpectralModel m;
    for (std::size_t n = 1; n <= harmonics; ++n)
    {
        synth::PartialTrack t;
        t.ratioToF0       = static_cast<float>(n);
        t.ampDb           = -6.0f * std::log2(static_cast<float>(n));
        t.seed            = 0x4A10u + static_cast<std::uint32_t>(n);
        t.windSensitivity = 0.0f; // isolate the brightness axis from the pitch one
        t.brightnessTrack = std::min(0.4f, trackScale * static_cast<float>(n));
        m.partials.push_back(t);
    }
    m.fundamentalHz = static_cast<float>(kF0);
    return m;
}

void render(synth::PartialBank& bank, std::vector<float>& l, std::vector<float>& r,
            std::size_t frames, std::size_t blockSize)
{
    for (std::size_t pos = 0; pos < frames; pos += blockSize)
    {
        const std::size_t n = std::min(blockSize, frames - pos);
        float* chans[2] = { l.data() + pos, r.data() + pos };
        core::AudioBlock block(chans, 2, n);
        bank.renderAdd(block);
    }
}

/// Energy at one frequency, Hann-windowed so a partial forty decibels down is not
/// read as the fundamental's skirt.
double toneAt(const std::vector<float>& x, double hz, std::size_t from = 0)
{
    const std::size_t n = x.size() - from;
    if (n < 4)
        return 0.0;
    const double w    = kTwoPi * hz / kSr;
    const double coef = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i)
                                                 / static_cast<double>(n - 1)));
        const double s0 = static_cast<double>(x[from + i]) * win + coef * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coef * s1 * s2;
}

/// The steady-state spectrum of one rank at one wind deviation.
std::vector<float> renderAt(const synth::SpectralModel& model,
                            core::TonalFamily           family,
                            float                       deviation,
                            std::size_t                 frames = 24000,
                            core::MidiNote              note   = 57,
                            double                      f0     = kF0,
                            float                       maxBloom = 0.060f)
{
    static std::vector<FixedWind> winds; // must outlive the bank
    winds.emplace_back(deviation);

    synth::PartialBank bank;
    bank.setMaxPartials(32);
    bank.prepare(kSr, 512);
    bank.setWindCoupling(&winds.back(), core::WindchestId{},
                         wind::defaultResponseFor(family));
    // The per-note treble tilt is keyed on the same harmonic index the wind's
    // brightness axis is; turning it off isolates one from the other.
    bank.setLiveliness(0.0f, 0.0f, maxBloom, 20000.0f, /*trebleTiltDb*/ 0.0f);
    bank.seedFrom(model, 0.0f);
    bank.trigger(core::PipeId{ 0, static_cast<std::uint8_t>(note), 1 }, 100, f0);

    std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
    render(bank, l, r, frames, 512);
    return l;
}

/// Where the wind has put this family's pitch at a given deviation.
///
/// Not a nicety. The pitch axis moves a reed 90 cents per unit deviation, so a
/// ten percent sag lands the fundamental 9 cents flat and its sixteenth harmonic
/// 18 Hz off where an unshifted probe looks. Over a 16k Hann window that is six
/// bins into the skirt, and a probe that ignored it would be reading the pitch
/// axis and reporting it as the brightness axis.
double windPitchRatio(core::TonalFamily family, float deviation)
{
    return std::exp2(static_cast<double>(
               wind::defaultResponseFor(family).pitchCents(deviation)) / 1200.0);
}

/// Level of the n-th harmonic relative to the fundamental, in decibels.
double harmonicDb(const std::vector<float>& x, int n, double pitchRatio = 1.0)
{
    const double f = toneAt(x, kF0 * pitchRatio, 8000);
    const double h = toneAt(x, kF0 * n * pitchRatio, 8000);
    return 10.0 * std::log10((h + 1.0e-30) / (f + 1.0e-30));
}
} // namespace

TEST_CASE("A reservoir giving way dulls the tone, not just flattens it",
          "[synthesis][wind][regression]")
{
    // The whole point. Before this the wind moved pitch and level and left the
    // spectrum exactly where it was, so a tutti that pulled the reservoir down went
    // flat and quiet and stayed just as bright -- which is not what an organ under
    // load sounds like.
    const synth::SpectralModel m = rank(16);

    const std::vector<float> nominal = renderAt(m, core::TonalFamily::Reed, 0.0f);
    const std::vector<float> sagging = renderAt(m, core::TonalFamily::Reed, -0.10f);

    const double at16Nominal = harmonicDb(nominal, 16);
    const double at16Sagging =
        harmonicDb(sagging, 16, windPitchRatio(core::TonalFamily::Reed, -0.10f));

    INFO("16th harmonic: " << at16Nominal << " dB at nominal, " << at16Sagging
                           << " dB with the wind ten percent down");
    CHECK(at16Sagging < at16Nominal - 1.0);
}

TEST_CASE("The fundamental does not move on the brightness axis",
          "[synthesis][wind]")
{
    // Brightness is a TILT, not a fader. A pipe losing its wind gets duller; if
    // the fundamental moved with the rest, this would just be the level axis under
    // another name -- and the level axis already exists.
    const synth::SpectralModel m = rank(16);

    const std::vector<float> nominal = renderAt(m, core::TonalFamily::Reed, 0.0f);
    const std::vector<float> sagging = renderAt(m, core::TonalFamily::Reed, -0.10f);

    // Compare the fundamental's own energy, level axis and all: the reed's
    // dbPerDeviation moves it, the brightness axis must not add to that.
    const double f0Nominal = 10.0 * std::log10(toneAt(nominal, kF0, 8000) + 1.0e-30);
    const double f0Sagging = 10.0 * std::log10(
        toneAt(sagging, kF0 * windPitchRatio(core::TonalFamily::Reed, -0.10f), 8000)
        + 1.0e-30);

    // A reed's 6 dB per unit deviation puts 0.6 dB on the level axis at a ten
    // percent sag, and nothing else may land on the fundamental.
    INFO("fundamental " << f0Nominal << " -> " << f0Sagging << " dB");
    CHECK(f0Nominal - f0Sagging == Approx(0.6).margin(0.15));
}

TEST_CASE("A reed loses its brightness faster than a flute does",
          "[synthesis][wind]")
{
    // The family curves say a reed swings 1.20 on the brightness axis and a flute
    // 0.30. That difference is most of why the two sound like different instruments
    // when the wind moves, and it had no effect at all.
    const synth::SpectralModel m = rank(16);

    const auto lossFor = [&m](core::TonalFamily family)
    {
        return harmonicDb(renderAt(m, family, 0.0f), 16)
             - harmonicDb(renderAt(m, family, -0.10f), 16,
                          windPitchRatio(family, -0.10f));
    };

    const double reed  = lossFor(core::TonalFamily::Reed);
    const double flute = lossFor(core::TonalFamily::Flute);

    INFO("at a ten percent sag the 16th harmonic loses " << reed
         << " dB on a reed and " << flute << " dB on a flute");
    CHECK(reed > flute * 2.0);
}

TEST_CASE("A partial that tracks the wind harder dulls harder", "[synthesis][wind]")
{
    // PartialTrack::brightnessTrack, which every recipe in the model authors and
    // nothing read. It is the per-partial modifier on the family curve, exactly as
    // windSensitivity is on the pitch axis.
    const synth::SpectralModel gentle = rank(16, /*trackScale*/ 0.00f);
    const synth::SpectralModel keen   = rank(16, /*trackScale*/ 0.02f);

    const double shifted = windPitchRatio(core::TonalFamily::Reed, -0.10f);
    const double gentleLoss = harmonicDb(renderAt(gentle, core::TonalFamily::Reed, 0.0f), 16)
                            - harmonicDb(renderAt(gentle, core::TonalFamily::Reed, -0.10f), 16, shifted);
    const double keenLoss   = harmonicDb(renderAt(keen,   core::TonalFamily::Reed, 0.0f), 16)
                            - harmonicDb(renderAt(keen,   core::TonalFamily::Reed, -0.10f), 16, shifted);

    INFO("16th harmonic loses " << gentleLoss << " dB untracked, " << keenLoss
         << " dB at the tracking the model authors");
    CHECK(keenLoss > gentleLoss + 0.3);
}

TEST_CASE("Surplus wind brightens as surely as sag dulls", "[synthesis][wind]")
{
    // The axis is signed. A reservoir above nominal -- which is what the recovery
    // overshoot after a chord releases looks like -- has to brighten, or the model
    // is a one-way fade rather than a response.
    const synth::SpectralModel m = rank(16);

    const double low  = harmonicDb(renderAt(m, core::TonalFamily::Reed, -0.06f), 16,
                                   windPitchRatio(core::TonalFamily::Reed, -0.06f));
    const double high = harmonicDb(renderAt(m, core::TonalFamily::Reed, +0.06f), 16,
                                   windPitchRatio(core::TonalFamily::Reed, +0.06f));

    INFO("16th harmonic " << low << " dB under sag, " << high << " dB under surplus");
    CHECK(high > low + 1.0);
}

TEST_CASE("A pipe on slack wind speaks more slowly", "[synthesis][wind][regression]")
{
    // The fourth coefficient, attackPerDeviation, and the other one nothing read.
    // A pipe's jet takes longer to set the air column going when the pressure is
    // down; that lag is as much a part of losing the wind as the dulling is.
    //
    // ONE partial, because the brightness axis is a confound here and a lone
    // fundamental is exactly where it vanishes: windBrightExp is log2(1) times
    // anything, i.e. zero. Measured with sixteen partials instead, the slack render
    // loses its upper partials -- which are also the slowest to bloom -- so its
    // envelope reaches its own lower plateau sooner and the speech stretch came out
    // as 0.998, cancelled by the dulling this same change introduced.
    const synth::SpectralModel m = rank(1);

    // The ENVELOPE, not the waveform. A first version of this looked for the first
    // sample to cross 80% of the settled peak, and that is granular to a fraction
    // of a cycle -- so the wind's pitch axis alone, which moves where the waveform
    // happens to sit, shifted the answer by twelve samples and the test read that
    // as speech. It passed with the attack axis entirely absent.
    //
    // A sliding RMS is smooth, period-independent, and interpolates, so it can
    // resolve the few milliseconds this is actually about.
    // Measured high in the compass rather than low, and that is not a detail.
    // Reading an envelope needs a window of a few cycles, and it has to be short
    // against the attack it is measuring -- but the speech profile makes the attack
    // SHORTER as the pitch rises while the period shortens far faster, so the two
    // are 3.6 cycles apart at C2 and 19 apart at A5. At the bottom of the compass a
    // 5 ms window is a third of a cycle and reads phase, not envelope; measured
    // there, this returned 46.5 ms for a 39 ms attack.
    constexpr double kHighHz = 880.0; // A5

    const auto timeToSteady = [&m](float deviation)
    {
        // The bloom pinned at its 8 ms floor, so what is left to measure is the
        // bank's ENVELOPE attack -- 21 ms at this pitch. Left at 60 ms the bloom is
        // 34 ms here and swamps the envelope, and then the test cannot tell a
        // stretch that reached both from one that reached only the blooms.
        const std::vector<float> x =
            renderAt(m, core::TonalFamily::Reed, deviation, 12000, 81, kHighHz,
                     /*maxBloom*/ 0.0f);
        constexpr std::size_t kWin = 128; // 2.7 ms, two and a third cycles at 880 Hz

        std::vector<double> env;
        env.reserve(x.size() / kWin);
        for (std::size_t i = 0; i + kWin <= x.size(); i += kWin)
        {
            double e = 0.0;
            for (std::size_t k = 0; k < kWin; ++k)
                e += static_cast<double>(x[i + k]) * x[i + k];
            env.push_back(std::sqrt(e / kWin));
        }

        double settled = 0.0;
        for (std::size_t i = env.size() * 2 / 3; i < env.size(); ++i)
            settled = std::max(settled, env[i]);

        const double target = settled * 0.80;
        for (std::size_t i = 1; i < env.size(); ++i)
            if (env[i] >= target)
            {
                // Where between the two windows the envelope actually crossed.
                const double span = env[i] - env[i - 1];
                const double frac = span > 0.0 ? (target - env[i - 1]) / span : 0.0;
                return (static_cast<double>(i - 1) + frac) * kWin / kSr;
            }
        return 1.0e9;
    };

    const double firm  = timeToSteady(0.0f);
    const double slack = timeToSteady(-0.15f);

    // A reed's attackPerDeviation is 0.80, so fifteen percent off nominal stretches
    // the envelope attack by twelve percent -- and the observable moves 3.6, not
    // 12. That is not a bug and it is worth writing down: what crosses 80% is the
    // PRODUCT of the envelope and the bloom, both near their own knees, so the
    // bloom holds the crossing back while the envelope lengthens under it. 18.08 ms
    // becomes 18.74.
    //
    // The band is around that measurement rather than around the coefficient. The
    // coefficient is a hand-tuned default carrying a TODO -- the offline
    // wind-sensitivity analysis is meant to replace these per rank -- so when it
    // moves, this should be re-measured rather than adjusted to fit.
    //
    // The lower bound is where it is on purpose: with the stretch reaching only the
    // per-partial blooms and not the envelope, the same measurement reads 1.009 and
    // fails here. That was the first version of this change, and it cost three
    // quarters of the effect.
    INFO("speech reaches 80% after " << firm * 1000.0 << " ms on firm wind and "
         << slack * 1000.0 << " ms on slack (" << (slack / firm) << "x)");
    CHECK(slack / firm > 1.02);
    CHECK(slack / firm < 1.06);
}

TEST_CASE("A bank with no wind bound sounds exactly as it did", "[synthesis][wind]")
{
    // Every test, tool and offline render that never binds a wind supply must be
    // untouched by any of this -- and so must an instrument sitting at its nominal
    // pressure, which is the state a reservoir spends most of its life in.
    const synth::SpectralModel m = rank(16);

    synth::PartialBank unbound;
    unbound.setMaxPartials(32);
    unbound.prepare(kSr, 512);
    unbound.setLiveliness(0.0f, 0.0f, 0.060f, 20000.0f, 0.0f);
    unbound.seedFrom(m, 0.0f);
    unbound.trigger(core::PipeId{ 0, 57, 1 }, 100, kF0);

    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    render(unbound, l, r, 24000, 512);

    const std::vector<float> atNominal = renderAt(m, core::TonalFamily::Reed, 0.0f);

    double worst = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        worst = std::max(worst, std::abs(static_cast<double>(l[i] - atNominal[i])));
        peak  = std::max(peak, std::abs(static_cast<double>(atNominal[i])));
    }

    REQUIRE(peak > 1.0e-4);
    INFO("worst sample difference " << worst << " against a peak of " << peak);
    CHECK(worst < peak * 1.0e-5);
}
