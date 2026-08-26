// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

//
// The temperament, connected.
//
// Everything below was already true of Temperament and TuningTable in isolation
// and was true of nothing the instrument played. The host's Temperament and
// Tuning A4 parameters were exposed, automatable and saved in the document; the
// library was implemented and unit-tested; the voices already consulted ITuning
// at note-on -- and the instrument played equal temperament at A=440 whatever any
// of that said, because nothing could get a rebuilt table across the thread
// boundary safely.
//
// So these do not test the temperaments. They test that changing one changes the
// PITCH A PIPE SOUNDS, which is a different claim and the one that was false.
//

#include "caecilia/core/AudioBlock.h"
#include "caecilia/synthesis/AdditiveVoice.h"
#include "caecilia/synthesis/SpectralModel.h"
#include "caecilia/synthesis/VoiceContext.h"
#include "caecilia/tuning/LiveTuning.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
namespace core   = caecilia::core;
namespace synth  = caecilia::synth;
namespace tuning = caecilia::tuning;

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

/// Cents between two frequencies.
double centsBetween(double a, double b)
{
    return 1200.0 * std::log2(a / b);
}

/// Energy at one frequency, by Goertzel.
double bandPower(const std::vector<float>& x, double hz, double sr)
{
    double total = 0.0;
    for (int k = -2; k <= 2; ++k)
    {
        const double w    = kTwoPi * (hz + static_cast<double>(k) * 0.5) / sr;
        const double coef = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        for (const float v : x)
        {
            const double s0 = static_cast<double>(v) + coef * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        total += s1 * s1 + s2 * s2 - coef * s1 * s2;
    }
    return total;
}

/// A single-partial voice, rendered through @p tune, returned as samples.
std::vector<float> renderSine(const core::ITuning& tune, core::MidiNote note)
{
    constexpr core::SampleRate kSr    = 48000.0;
    constexpr std::size_t      kBlock = 512;

    synth::SpectralModel model;
    synth::PartialTrack  p;
    p.ratioToF0 = 1.0f;   // one partial: the pitch is unambiguous
    p.ampDb     = 0.0f;
    model.partials.push_back(p);
    model.fundamentalHz = 130.81f;

    synth::AdditiveVoice voice;
    voice.bank().setMaxPartials(4);
    voice.prepare(kSr, kBlock);

    synth::VoiceContext ctx;
    ctx.tuning  = &tune;
    ctx.family  = core::TonalFamily::Flute;
    ctx.footage = core::footage::kEight;
    voice.setContext(ctx);
    voice.seedFrom(model);
    voice.noteOn(core::PipeId{ 0, note, 1 }, 100);

    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    float* ch[2] = { l.data(), r.data() };

    for (int b = 0; b < 96; ++b) // past the speech
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        core::AudioBlock blk(ch, 2, kBlock);
        voice.renderAdd(blk);
    }

    std::vector<float> tail;
    tail.reserve(kBlock * 32);
    for (int b = 0; b < 32; ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        core::AudioBlock blk(ch, 2, kBlock);
        voice.renderAdd(blk);
        tail.insert(tail.end(), l.begin(), l.end());
    }
    return tail;
}
} // namespace

TEST_CASE("A snapshot pins A4 exactly and realises the temperament",
          "[tuning][live]")
{
    // The ITuning contract: whatever the temperament does to the intervals, A4
    // sounds at exactly the requested reference. A temperament that moved A as well
    // would make "A = 415" mean something different in every one of them.
    for (const core::TemperamentId id : { core::TemperamentId::Equal,
                                          core::TemperamentId::QuarterMeantone,
                                          core::TemperamentId::Werckmeister3,
                                          core::TemperamentId::Kirnberger3,
                                          core::TemperamentId::Pythagorean,
                                          core::TemperamentId::Young })
    {
        const tuning::TuningSnapshot s = tuning::makeSnapshot(id, 415.0);
        INFO("temperament " << static_cast<int>(id));
        CHECK(s.unisonHz[69] == Approx(415.0).epsilon(1e-9));
        CHECK(s.a4Hz == Approx(415.0));
        CHECK(s.temperament == id);
    }
}

TEST_CASE("An unequal temperament is actually unequal", "[tuning][live]")
{
    // Quarter-comma meantone's whole point is that its major thirds are pure and
    // its fifths pay for it. If the snapshot came out identical to equal
    // temperament, everything downstream would still "work" and sound like nothing.
    const tuning::TuningSnapshot equal =
        tuning::makeSnapshot(core::TemperamentId::Equal, 440.0);
    const tuning::TuningSnapshot meantone =
        tuning::makeSnapshot(core::TemperamentId::QuarterMeantone, 440.0);

    // C4 = 60, E4 = 64. A pure major third is 386.31 cents; equal gives 400.
    const double equalThird = centsBetween(equal.unisonHz[64], equal.unisonHz[60]);
    const double meanThird  = centsBetween(meantone.unisonHz[64], meantone.unisonHz[60]);

    INFO("major third: equal " << equalThird << " cents, meantone " << meanThird);
    CHECK(equalThird == Approx(400.0).margin(0.05));
    CHECK(meanThird  == Approx(386.31).margin(1.0));

    // And they are not the same table.
    std::size_t differing = 0;
    for (std::size_t n = 0; n < equal.unisonHz.size(); ++n)
        if (std::abs(centsBetween(equal.unisonHz[n], meantone.unisonHz[n])) > 0.5)
            ++differing;
    INFO(differing << " of 128 notes differ by more than half a cent");
    CHECK(differing > 90);
}

TEST_CASE("The reference pitch moves everything together", "[tuning][live]")
{
    // A=415 is a semitone below A=440 in name only: it is a ratio, and every note
    // has to move by the same ratio or the instrument is not transposed, it is
    // out of tune with itself.
    const tuning::TuningSnapshot a440 =
        tuning::makeSnapshot(core::TemperamentId::Werckmeister3, 440.0);
    const tuning::TuningSnapshot a415 =
        tuning::makeSnapshot(core::TemperamentId::Werckmeister3, 415.0);

    const double expected = centsBetween(415.0, 440.0);
    for (std::size_t n = 24; n < 108; ++n)
    {
        INFO("note " << n);
        CHECK(centsBetween(a415.unisonHz[n], a440.unisonHz[n])
              == Approx(expected).margin(1e-6));
    }
}

TEST_CASE("A published tuning does not take effect until the block boundary",
          "[tuning][live][regression]")
{
    // The whole reason a snapshot exists. Rebuilding writes 128 doubles, and a
    // note-on reading them mid-rebuild is a data race -- not a wrong pitch, undefined
    // behaviour. So the audio thread adopts at a point it chooses, and a block
    // sounds in ONE tuning throughout rather than changing pitch partway because a
    // rebuild happened to land between two commands.
    tuning::LiveTuning live;

    CHECK(live.temperament() == core::TemperamentId::Equal);
    CHECK(live.referenceA4Hz() == Approx(440.0));
    const double before = live.frequencyForNote(60);

    live.publish(tuning::makeSnapshot(core::TemperamentId::QuarterMeantone, 415.0));

    // Published, not adopted: nothing has changed for anyone reading it.
    INFO("after publish, before adopt");
    CHECK(live.temperament() == core::TemperamentId::Equal);
    CHECK(live.frequencyForNote(60) == Approx(before));

    live.adoptPending();

    CHECK(live.temperament() == core::TemperamentId::QuarterMeantone);
    CHECK(live.referenceA4Hz() == Approx(415.0));
    CHECK(live.frequencyForNote(60) != Approx(before));

    // And adopting again with nothing published is a no-op rather than a revert.
    const double after = live.frequencyForNote(60);
    live.adoptPending();
    CHECK(live.frequencyForNote(60) == Approx(after));
}

TEST_CASE("A rank sounds at its footage's exact rational ratio", "[tuning][live]")
{
    // The reason the footage is a rational and not a float. A 2 2/3' quint has to
    // land precisely on the third harmonic of the unison, or it beats against the
    // rank it exists to reinforce.
    tuning::LiveTuning live;
    const core::PipeId pipe{ 0, 60, 1 };

    const double unison = live.frequencyForPipe(pipe, core::footage::kEight);
    REQUIRE(unison > 0.0);

    CHECK(live.frequencyForPipe(pipe, core::footage::kSixteen) == Approx(unison * 0.5));
    CHECK(live.frequencyForPipe(pipe, core::footage::kFour)    == Approx(unison * 2.0));
    CHECK(live.frequencyForPipe(pipe, core::footage::kTwo)     == Approx(unison * 4.0));
    CHECK(live.frequencyForPipe(pipe, core::footage::kTwoAndTwoThird)
          == Approx(unison * 3.0));

    // A footage of nothing is silence, not a division by zero.
    CHECK(live.frequencyForPipe(pipe, core::Footage{ 0, 1 }) == Approx(0.0));
}

TEST_CASE("Changing the temperament changes the pitch a pipe sounds",
          "[tuning][live][regression]")
{
    // The audible claim, and the one that was false. Everything above could pass
    // with the table never reaching a voice -- which is exactly the state this was
    // in: parameters exposed and saved, library implemented and tested, and the
    // instrument playing equal temperament at A=440 regardless.
    //
    // Rendered through a real voice and measured off the audio, because a voice
    // reads the tuning at note-on and that is the step nothing was exercising.
    constexpr core::SampleRate kSr = 48000.0;

    // G#4 (68): meantone's wolf territory, where it departs from equal the most.
    constexpr core::MidiNote kNote = 68;

    tuning::LiveTuning equal;
    const double equalHz = equal.frequencyForNote(kNote);

    tuning::LiveTuning meantone;
    meantone.publish(tuning::makeSnapshot(core::TemperamentId::QuarterMeantone, 440.0));
    meantone.adoptPending();
    const double meanHz = meantone.frequencyForNote(kNote);

    const double expectedCents = centsBetween(meanHz, equalHz);
    INFO("G#4: equal " << equalHz << " Hz, meantone " << meanHz << " Hz ("
         << expectedCents << " cents apart)");
    REQUIRE(std::abs(expectedCents) > 5.0); // the two really are distinguishable

    const std::vector<float> underEqual    = renderSine(equal, kNote);
    const std::vector<float> underMeantone = renderSine(meantone, kNote);

    // Each render must have its energy where ITS OWN tuning says, and not where the
    // other one does. If the voice ignored the tuning, both would peak at the same
    // place and the second check of each pair would fail.
    CHECK(bandPower(underEqual, equalHz, kSr) > bandPower(underEqual, meanHz, kSr) * 4.0);
    CHECK(bandPower(underMeantone, meanHz, kSr)
          > bandPower(underMeantone, equalHz, kSr) * 4.0);
}
