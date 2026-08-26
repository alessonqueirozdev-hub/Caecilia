// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// How a mixture breaks back, which is the whole of what makes a plenum work in
// the treble -- and which this instrument was getting wrong in the top two
// octaves of the compass.
//
// Every rank folded on its own, halving whenever its own pitch crossed a 6 kHz
// ceiling, with nothing but `ratio > 1.5` to stop it. Measured on the demo
// organ's Fourniture IV (2' + 1 1/3' + 1' + 2/3'):
//
//     note  f0        2'   1 1/3'      1'    2/3'   distinct
//       81   880.0   4.00     6.00    4.00    6.00      2/4
//       90  1480.0   4.00     3.00    4.00    3.00      2/4
//       96  2093.0   2.00     1.50    2.00    1.50      2/4
//
// Half the stop gone above A5, two pairs of ranks landing on the same pitch, and
// at the top of the compass a stop nominally at 2' sounding a 4' and a 5 1/3'.
// A quint BELOW the unison, inside a mixture: no builder has ever done that, and
// it muddies the very chorus the stop exists to crown.
//
// A real break moves the whole composition one step down the organ series
// (1, 2, 3, 4, 6, 8, 12, 16), which is why 15-19-22-26 becomes 12-15-19-22
// becomes 8-12-15-19: the ranks stay distinct, the interval shape survives, and
// the window stops at the unison.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/model/DemoOrgan.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <span>
#include <vector>

using Catch::Approx;
namespace core  = caecilia::core;
namespace model = caecilia::model;
namespace synth = caecilia::synth;

namespace
{
constexpr core::SampleRate kSr    = 48000.0;
constexpr double           kTwoPi = 6.283185307179586476925286766559;

/// The 6 kHz ceiling PartialBank breaks at; not exported, so it is restated here
/// and any drift between the two shows up as a failing test rather than silence.
constexpr double kCeilingHz = 6000.0;

namespace ft = core::footage;

/// Sounding pitch of a footage relative to 8' unison. DemoOrgan.cpp has the same
/// three lines file-locally; restating them here keeps the test off a private
/// helper rather than widening one for a test's sake.
float ratioToUnison(core::Footage f) noexcept
{
    return f.num == 0 ? 0.0f
                      : (8.0f * static_cast<float>(f.den)) / static_cast<float>(f.num);
}

/// The Grand-Orgue Fourniture IV: 15-19-22-26, i.e. series indices 3..6.
std::vector<core::Footage> fournitureIV()
{
    return { ft::kTwo, ft::kOneAndThird, ft::kOne, core::Footage{ 2, 3 } };
}

/// The Récit Plein Jeu III: 15-19-22, indices 3..5.
std::vector<core::Footage> pleinJeuIII()
{
    return { ft::kTwo, ft::kOneAndThird, ft::kOne };
}

double equalTemperedHz(int note)
{
    return 440.0 * std::exp2(static_cast<double>(note - 69) / 12.0);
}

/// A bank seeded with a mixture, ready to be asked about its breaks.
synth::PartialBank bankFor(const std::vector<core::Footage>& ranks)
{
    synth::PartialBank bank;
    bank.setMaxPartials(32);
    bank.prepare(kSr, 512);
    bank.seedFrom(model::makeSpectralMixture(std::span<const core::Footage>(ranks)), 0.0f);
    return bank;
}

/// The series indices a composition occupies, lowest first.
std::vector<int> rankIndices(const std::vector<core::Footage>& ranks)
{
    std::vector<int> out;
    for (const core::Footage f : ranks)
        out.push_back(synth::mixtureSeriesIndex(ratioToUnison(f)));
    std::sort(out.begin(), out.end());
    return out;
}

/// What the ranks of @p composition sound at @p note, as ratios to 8' unison.
std::vector<float> soundingRatios(const std::vector<core::Footage>& ranks, int note)
{
    synth::PartialBank bank  = bankFor(ranks);
    const int          shift = bank.breakShiftForNote(static_cast<core::MidiNote>(note));

    std::vector<float> out;
    for (const int idx : rankIndices(ranks))
    {
        REQUIRE(idx >= 0);
        REQUIRE(idx - shift >= 0);
        out.push_back(synth::kMixtureSeries[static_cast<std::size_t>(idx - shift)]);
    }
    return out;
}

/// Energy at one frequency, by Goertzel over a Hann-windowed buffer. A mixture's
/// ranks sit tens of decibels apart, so an unwindowed probe reads the loudest
/// rank's leakage at every pitch it is asked about.
double toneAt(const std::vector<float>& x, double hz)
{
    const std::size_t n = x.size();
    if (n < 4)
        return 0.0;
    const double w    = kTwoPi * hz / kSr;
    const double coef = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i)
                                                 / static_cast<double>(n - 1)));
        const double s0 = static_cast<double>(x[i]) * win + coef * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coef * s1 * s2;
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

/// The compass the demo organ gives its manual stops.
constexpr int kLowest  = 36;
constexpr int kHighest = 96;
} // namespace

TEST_CASE("A mixture keeps every one of its ranks, all the way up",
          "[synthesis][mixture][regression]")
{
    // THE defect. Above A5 the Fourniture had two distinct pitches where it should
    // have four, because ranks folding independently converged onto each other.
    for (const auto& comp : { fournitureIV(), pleinJeuIII() })
    {
        const std::size_t ranks = comp.size();
        for (int note = kLowest; note <= kHighest; ++note)
        {
            const std::vector<float> sounding = soundingRatios(comp, note);
            const std::set<float>    distinct(sounding.begin(), sounding.end());

            INFO("note " << note << ", " << distinct.size() << " of " << ranks
                         << " ranks distinct");
            CHECK(distinct.size() == ranks);
        }
    }
}

TEST_CASE("A mixture never sounds below its unison", "[synthesis][mixture][regression]")
{
    // A 2' Fourniture reached a 5 1/3' at the top of the compass. A quint under the
    // unison does not crown a chorus, it fogs it -- and it is not a thing any
    // instrument does, so it can be asserted flatly.
    for (const auto& comp : { fournitureIV(), pleinJeuIII() })
        for (int note = kLowest; note <= kHighest; ++note)
            for (const float ratio : soundingRatios(comp, note))
            {
                INFO("note " << note << " sounds a rank at ratio " << ratio);
                CHECK(ratio >= 1.0f);
            }
}

TEST_CASE("The whole stop breaks at once, at a C or an F sharp, one row at a time",
          "[synthesis][mixture]")
{
    // A break an organist can hear as a step, at a note they expect it, and never
    // skipping a row of the scheme. Deciding it per note from the played pitch
    // would put it wherever the ceiling happened to be crossed and the crown would
    // seem to wobble; deciding it once an OCTAVE forces every break to be a double
    // step, because a series step is a fourth or a fifth and an octave is neither.
    for (const auto& comp : { fournitureIV(), pleinJeuIII() })
    {
        synth::PartialBank bank = bankFor(comp);

        int lastShift = -1;
        int breaks    = 0;
        for (int note = kLowest; note <= kHighest; ++note)
        {
            const int shift = bank.breakShiftForNote(static_cast<core::MidiNote>(note));
            if (lastShift >= 0 && shift != lastShift)
            {
                INFO("break between " << (note - 1) << " and " << note);
                CHECK(note % 6 == 0);             // a C or an F sharp
                CHECK(shift == lastShift + 1);    // one row, never two at once
                ++breaks;
            }
            lastShift = shift;
        }

        // And it did break, more than once: a Fourniture that never breaks is the
        // bug, not the fix.
        INFO(breaks << " breaks across the compass");
        CHECK(breaks >= 2);
    }
}

TEST_CASE("A break keeps the mixture's interval shape", "[synthesis][mixture]")
{
    // The rows of a real break scheme are consecutive members of the organ series:
    // 15-19-22-26 becomes 12-15-19-22 becomes 8-12-15-19. That is what makes a
    // broken mixture still sound like the same stop.
    const std::vector<core::Footage> comp = fournitureIV();
    const std::vector<int>           base = rankIndices(comp);

    synth::PartialBank bank = bankFor(comp);
    for (int note = kLowest; note <= kHighest; ++note)
    {
        const int shift = bank.breakShiftForNote(static_cast<core::MidiNote>(note));
        std::vector<int> got;
        for (const int idx : base)
            got.push_back(idx - shift);

        INFO("note " << note << " shift " << shift);
        for (std::size_t i = 1; i < got.size(); ++i)
            CHECK(got[i] == got[i - 1] + 1); // still a run of consecutive entries
    }
}

TEST_CASE("The crown stays in the band the ceiling names", "[synthesis][mixture]")
{
    // The point of breaking at all: the top rank must not climb out of the range
    // where it reinforces the chorus. It is allowed to overshoot only where the
    // window has bottomed out at the unison and there is nowhere left to break to.
    const std::vector<core::Footage> comp  = fournitureIV();
    synth::PartialBank               bank  = bankFor(comp);
    const int                        floor = bank.maxBreakShift();
    const std::vector<int>           idx   = rankIndices(comp);

    for (int note = kLowest; note <= kHighest; ++note)
    {
        const int   shift = bank.breakShiftForNote(static_cast<core::MidiNote>(note));
        const auto  top   = static_cast<std::size_t>(idx.back() - shift);
        const double hz   = equalTemperedHz(note)
                          * static_cast<double>(synth::kMixtureSeries[top]);

        INFO("note " << note << " crowns at " << hz << " Hz, shift " << shift);
        CHECK((hz <= kCeilingHz || shift == floor));
    }
}

TEST_CASE("A break is a fact about the compass, not about the tuning",
          "[synthesis][mixture]")
{
    // Two banks seeded from the same composition break identically whatever
    // frequency they are triggered at. If the break law read the tuned pitch, a
    // meantone organ or an A=415 session would break at a different note from an
    // equal-tempered A=440 one -- an instrument whose mixtures move when you
    // retune it.
    synth::PartialBank a = bankFor(fournitureIV());
    synth::PartialBank b = bankFor(fournitureIV());

    for (int note = kLowest; note <= kHighest; ++note)
        CHECK(a.breakShiftForNote(static_cast<core::MidiNote>(note))
              == b.breakShiftForNote(static_cast<core::MidiNote>(note)));
}

TEST_CASE("A broken rank really sounds where the break says",
          "[synthesis][mixture]")
{
    // The law above, measured in the audio: at the top of the compass the
    // Fourniture must have energy at its BROKEN pitches and none to speak of at
    // the pitches it would have sounded unbroken.
    constexpr int         kNote   = 88;      // E6, above both breaks
    constexpr std::size_t kFrames = 16384;

    const std::vector<core::Footage> comp = fournitureIV();
    const double                     f0   = equalTemperedHz(kNote);

    synth::PartialBank bank = bankFor(comp);
    const int shift = bank.breakShiftForNote(static_cast<core::MidiNote>(kNote));
    REQUIRE(shift > 0);

    bank.trigger(core::PipeId{ 0, static_cast<std::uint8_t>(kNote), 1 }, 100, f0);
    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    render(bank, l, r, kFrames, 512);

    for (const int idx : rankIndices(comp))
    {
        const double broken   = f0 * static_cast<double>(
            synth::kMixtureSeries[static_cast<std::size_t>(idx - shift)]);
        const double unbroken = f0 * static_cast<double>(
            synth::kMixtureSeries[static_cast<std::size_t>(idx)]);

        // Only worth asking about pitches the render can actually carry.
        if (unbroken >= kSr * 0.45)
            continue;

        const double atBroken   = toneAt(l, broken);
        const double atUnbroken = toneAt(l, unbroken);

        INFO("rank " << idx << ": " << broken << " Hz -> " << atBroken
                     << ", " << unbroken << " Hz -> " << atUnbroken);
        CHECK(atBroken > atUnbroken * 4.0);
    }
}

TEST_CASE("Every partial of a compound stop names the rank it belongs to",
          "[synthesis][mixture]")
{
    // The wiring the break law rests on. A partial at ratio 8 might be a 1' rank or
    // the second harmonic of a 2', and those two move differently when the stop
    // breaks -- so the spectrum has to say which, and say it for every partial.
    for (const auto& comp : { fournitureIV(), pleinJeuIII() })
    {
        const synth::SpectralModel m =
            model::makeSpectralMixture(std::span<const core::Footage>(comp));

        std::vector<float> pitches;
        for (const core::Footage f : comp)
            pitches.push_back(ratioToUnison(f));

        std::size_t belonging = 0;
        for (const synth::PartialTrack& p : m.partials)
        {
            if (p.rankRatioToF0 <= 0.0f)
                continue; // the faint grounding unison, which is not a rank
            ++belonging;

            const bool known = std::any_of(pitches.begin(), pitches.end(),
                [&](float r) { return p.rankRatioToF0 == Approx(r); });
            INFO("partial at " << p.ratioToF0 << " names rank " << p.rankRatioToF0);
            CHECK(known);

            // And it is that rank's own fundamental or one of its harmonics --
            // never some other rank's pitch that happens to be nearby.
            const float h = p.ratioToF0 / p.rankRatioToF0;
            CHECK(h == Approx(std::round(h)).margin(1.0e-4));
            CHECK(h >= 1.0f);
        }
        CHECK(belonging == comp.size() * 2); // each rank, and each rank's octave
    }
}

TEST_CASE("Every partial of a mutation names its one rank", "[synthesis][mixture]")
{
    // Same wiring, and the case that made it necessary: a Tierce is one pipe, so
    // its fundamental at 5 and its harmonics at 10 and 15 all belong to the rank at
    // 5. Letting the harmonics claim their own pitches is what pulled the rank
    // apart in the treble.
    for (const core::Footage f : { ft::kTwoAndTwoThird, ft::kOneAndThreeFifth })
    {
        const synth::SpectralModel m   = model::makeSpectralMutation(f, 1.0f);
        const float                pitch = ratioToUnison(f);

        std::size_t belonging = 0;
        for (const synth::PartialTrack& p : m.partials)
        {
            if (p.rankRatioToF0 <= 0.0f)
                continue;
            ++belonging;
            INFO("partial at " << p.ratioToF0 << " names rank " << p.rankRatioToF0);
            CHECK(p.rankRatioToF0 == Approx(pitch));
        }
        CHECK(belonging == 3); // the rank and its second and third harmonics
    }
}

TEST_CASE("A rank's harmonics land where the rank went, not where they would have",
          "[synthesis][mixture][regression]")
{
    // Measured rather than reasoned, because the two rules agree almost everywhere
    // and diverge only near the bottom of the series -- where the entries are 1, 2,
    // 3 rather than a clean doubling. So the probe is a two-rank compound driven
    // all the way to its floor.
    //
    // A stop of {2', 1 1/3'} at MIDI 96 has broken three rows: its ranks sound at
    // 8' and 4', and their second harmonics therefore at 4' and 2' -- ratios 2 and
    // 4. Had each harmonic broken on its own index it would sit at ratio 3, which
    // nothing else in this stop occupies. So ratio 3 is the tell.
    const std::vector<core::Footage> comp{ ft::kTwo, ft::kOneAndThird };
    constexpr int         kNote   = 96;
    constexpr std::size_t kFrames = 16384;

    synth::PartialBank bank = bankFor(comp);
    REQUIRE(bank.breakMode() == synth::PartialBank::BreakMode::Series);
    REQUIRE(bank.breakShiftForNote(static_cast<core::MidiNote>(kNote)) == 3);

    const double f0 = equalTemperedHz(kNote);
    bank.trigger(core::PipeId{ 0, static_cast<std::uint8_t>(kNote), 1 }, 100, f0);

    std::vector<float> l(kFrames, 0.0f), r(kFrames, 0.0f);
    render(bank, l, r, kFrames, 512);

    const double atRank      = toneAt(l, f0 * 1.0);  // the 2' rank, broken to 8'
    const double atItsOctave = toneAt(l, f0 * 2.0);  // that rank's second harmonic
    const double atStray     = toneAt(l, f0 * 3.0);  // where a lone fold would put it

    // Measured: nothing sounds at ratio 3 when the ranks move as ranks, so the
    // stray reading is windowed leakage and sits twelve orders of magnitude down.
    // Let each harmonic break on its own index instead and it becomes a real
    // partial only 6.6 dB under its neighbour -- which is why the margin here is
    // 20 dB and not the 4 dB that first looked like enough.
    INFO("rank " << atRank << ", its octave " << atItsOctave << ", stray " << atStray);
    REQUIRE(atRank > 0.0);
    CHECK(atItsOctave > atStray * 100.0);
}

TEST_CASE("A mutation stays a harmonic series when it breaks",
          "[synthesis][mixture][regression]")
{
    // The Tierce 1 3/5' is ONE rank: a pipe at ratio 5 plus its own second and
    // third harmonics at 10 and 15. Those three folded independently, and at the
    // top of the compass they landed at 2.5, 2.5 and 1.875 -- two partials on one
    // pitch and a third BELOW the fundamental. The rank had stopped being a
    // harmonic series at all, which is not a break, it is a different sound.
    const synth::SpectralModel tierce =
        model::makeSpectralMutation(ft::kOneAndThreeFifth, 1.0f);

    synth::PartialBank bank;
    bank.setMaxPartials(16);
    bank.prepare(kSr, 512);
    bank.seedFrom(tierce, 0.0f);

    // One rank, so it breaks by octaves. Walking the series would put a 1 3/5'
    // Tierce on a 1 1/3' or a 2', and a tierce that is not a third is not a tierce.
    REQUIRE(bank.breakMode() == synth::PartialBank::BreakMode::Octave);

    for (int note = kLowest; note <= kHighest; ++note)
    {
        const int shift = bank.breakShiftForNote(static_cast<core::MidiNote>(note));

        // What the rank sounds at, and what each of its partials therefore does.
        const float rank  = bank.brokenRankRatio(5.0f, synth::mixtureSeriesIndex(5.0f), shift);
        const float scale = rank / 5.0f;

        INFO("note " << note << " shift " << shift << " rank at " << rank);
        CHECK(rank >= 1.0f);                       // never below the unison

        // 5, 10, 15 all scaled by the same factor is still 1 : 2 : 3.
        const float p1 = 5.0f * scale, p2 = 10.0f * scale, p3 = 15.0f * scale;
        CHECK(p2 == Approx(2.0f * p1));
        CHECK(p3 == Approx(3.0f * p1));

        // And a tierce is still a tierce: an octave break keeps the ratio's
        // relationship to the unison, which walking the series would not.
        const float octaves = std::log2(5.0f / rank);
        CHECK(octaves == Approx(std::round(octaves)).margin(1.0e-4));
    }

    // It really does break, or the assertions above are about nothing.
    CHECK(bank.breakShiftForNote(static_cast<core::MidiNote>(kHighest)) > 0);
}

TEST_CASE("A lone rank breaks by an octave; a composition walks the series",
          "[synthesis][mixture]")
{
    // The two rules, side by side. A Nazard 2 2/3' is a ratio of 3, which IS a
    // series entry -- so nothing but the rank COUNT distinguishes the two cases,
    // and getting it from the pitch would break a lone Nazard down to a 4'.
    const synth::SpectralModel nazard =
        model::makeSpectralMutation(ft::kTwoAndTwoThird, 1.0f);

    synth::PartialBank lone;
    lone.setMaxPartials(16);
    lone.prepare(kSr, 512);
    lone.seedFrom(nazard, 0.0f);
    REQUIRE(lone.breakMode() == synth::PartialBank::BreakMode::Octave);
    CHECK(lone.brokenRankRatio(3.0f, synth::mixtureSeriesIndex(3.0f), 1)
          == Approx(1.5f));   // an octave down: still a twelfth above ITS unison

    synth::PartialBank compound = bankFor(fournitureIV());
    REQUIRE(compound.breakMode() == synth::PartialBank::BreakMode::Series);
    CHECK(compound.brokenRankRatio(4.0f, synth::mixtureSeriesIndex(4.0f), 1)
          == Approx(3.0f));   // one row down the series: 15th becomes 12th
}

TEST_CASE("A composition off the series does not break", "[synthesis][mixture]")
{
    // A hand-written composition with a pitch that is not a series member is a
    // mistake, and the safe response is to leave that rank where it was rather
    // than move it somewhere arbitrary. Nothing in the shipping organ does this;
    // the loader and the tools accept authored compositions, and they will.
    CHECK(synth::mixtureSeriesIndex(4.0f) == 3);
    CHECK(synth::mixtureSeriesIndex(12.0f) == 6);
    CHECK(synth::mixtureSeriesIndex(5.0f) == -1);   // a 1 3/5' tierce rank
    CHECK(synth::mixtureSeriesIndex(0.0f) == -1);

    synth::SpectralModel m;
    synth::PartialTrack  t;
    t.ratioToF0     = 5.0f;
    t.rankRatioToF0 = 0.0f;   // not a breaking rank at all
    m.partials.push_back(t);
    m.fundamentalHz = 261.6f;

    synth::PartialBank bank;
    bank.setMaxPartials(4);
    bank.prepare(kSr, 512);
    bank.seedFrom(m, 0.0f);

    CHECK(bank.breakMode() == synth::PartialBank::BreakMode::None);
    CHECK(bank.breakShiftForNote(static_cast<core::MidiNote>(96)) == 0);
}
