/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

//
// Tuning tests: the sounding-frequency oracle the voices read. Cover equal-
// temperament reference pitches, exact-rational Footage scaling (a 2 2/3' quint
// really sounds a twelfth, not "close to" one), A4 anchoring across every built-in
// historical temperament, pure octaves, and the interval CHARACTER that
// distinguishes a temperament (quarter-comma meantone's near-pure major third).
//

#include "ceciliae/core/EngineTypes.h"
#include "ceciliae/tuning/Temperament.h"
#include "ceciliae/tuning/TemperamentLibrary.h"
#include "ceciliae/tuning/TuningModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using Catch::Approx;
namespace core   = ceciliae::core;
namespace tuning = ceciliae::tuning;

TEST_CASE("Equal temperament yields standard reference pitches", "[tuning][equal]")
{
    tuning::TuningModel model; // defaults: equal, A4 = 440

    CHECK(model.temperament() == core::TemperamentId::Equal);
    CHECK(model.referenceA4Hz() == Approx(440.0));

    CHECK(model.frequencyForNote(69) == Approx(440.0));  // A4
    CHECK(model.frequencyForNote(81) == Approx(880.0));  // A5
    CHECK(model.frequencyForNote(57) == Approx(220.0));  // A3
    CHECK(model.frequencyForNote(60) == Approx(261.6255653).epsilon(1e-6)); // middle C
}

TEST_CASE("Footage scales pipe frequency by the exact sounding ratio", "[tuning][footage]")
{
    tuning::TuningModel model; // A4 = 440
    const core::PipeId pipeA4{/*rankId*/ 0, /*midiNote*/ 69};

    // ratio to 8' unison = 8 / feet.
    CHECK(model.frequencyForPipe(pipeA4, core::footage::kEight) == Approx(440.0));   // 8'  -> x1
    CHECK(model.frequencyForPipe(pipeA4, core::footage::kFour) == Approx(880.0));    // 4'  -> x2 (octave)
    CHECK(model.frequencyForPipe(pipeA4, core::footage::kSixteen) == Approx(220.0)); // 16' -> x1/2
    CHECK(model.frequencyForPipe(pipeA4, core::footage::kTwo) == Approx(1760.0));    // 2'  -> x4
    // A 2 2/3' quint sounds a perfect twelfth (x3) above the 8' unison — exactly,
    // because the footage is an exact rational and lands on the true harmonic.
    CHECK(model.frequencyForPipe(pipeA4, core::footage::kTwoAndTwoThird) == Approx(1320.0));
}

TEST_CASE("Reference pitch retunes the whole instrument", "[tuning][pitch]")
{
    tuning::TuningModel model;
    model.setReferenceA4Hz(415.0); // baroque pitch

    CHECK(model.referenceA4Hz() == Approx(415.0));
    CHECK(model.frequencyForNote(69) == Approx(415.0));
    CHECK(model.frequencyForNote(81) == Approx(830.0));
}

TEST_CASE("Every built-in temperament anchors A4 and keeps pure octaves", "[tuning][temperament]")
{
    for (const core::TemperamentId id : tuning::TemperamentLibrary::builtInIds())
    {
        const tuning::Temperament temperament = tuning::TemperamentLibrary::get(id);

        tuning::TuningModel model;
        model.configure(temperament, 440.0);

        INFO("temperament id = " << static_cast<int>(id));
        // A4 is pinned exactly to the reference regardless of the temperament.
        CHECK(model.frequencyForNote(69) == Approx(440.0).epsilon(1e-9));
        // Octaves are always exactly 2:1 (a pitch class repeats every 12 notes).
        CHECK(model.frequencyForNote(81) / model.frequencyForNote(69) == Approx(2.0).epsilon(1e-9));
        CHECK(model.frequencyForNote(57) / model.frequencyForNote(69) == Approx(0.5).epsilon(1e-9));
    }
}

TEST_CASE("Quarter-comma meantone has a near-pure major third", "[tuning][temperament]")
{
    tuning::TuningModel meantone;
    meantone.configure(tuning::TemperamentLibrary::get(core::TemperamentId::QuarterMeantone), 440.0);

    // C4 (60) -> E4 (64): meantone's defining feature is a just 5:4 third.
    const double meantoneThird = meantone.frequencyForNote(64) / meantone.frequencyForNote(60);
    CHECK(meantoneThird == Approx(1.25).margin(0.01));

    // Equal temperament's third is wider (the 400-cent tempered third).
    tuning::TuningModel equal;
    const double equalThird = equal.frequencyForNote(64) / equal.frequencyForNote(60);
    CHECK(equalThird == Approx(1.259921).epsilon(1e-4));
    CHECK(meantoneThird < equalThird);
}

TEST_CASE("A flat custom temperament matches equal temperament", "[tuning][custom]")
{
    std::array<double, tuning::Temperament::kPitchClasses> zeros{}; // all 0 cents
    const tuning::Temperament flat = tuning::Temperament::custom(zeros, "Flat");

    tuning::TuningModel model;
    model.configure(flat, 440.0);

    CHECK(model.temperament() == core::TemperamentId::Custom);
    CHECK(model.frequencyForNote(69) == Approx(440.0));
    CHECK(model.frequencyForNote(60) == Approx(261.6255653).epsilon(1e-6));
}
