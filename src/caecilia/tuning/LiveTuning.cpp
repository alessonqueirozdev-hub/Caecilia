// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/tuning/LiveTuning.h"

#include "caecilia/tuning/Temperament.h"
#include "caecilia/tuning/TemperamentLibrary.h"

#include <cmath>

namespace caecilia::tuning
{

LiveTuning::LiveTuning() noexcept
{
    // Equal temperament at A=440 until something says otherwise, which is also
    // what the host parameters default to.
    current_ = makeSnapshot(core::TemperamentId::Equal, 440.0);
}

void LiveTuning::publish(const TuningSnapshot& snapshot) noexcept
{
    pending_.write(snapshot);
}

void LiveTuning::adoptPending() noexcept
{
    // Asked before copied: on a block where nothing changed this is one relaxed
    // load and nothing else, and a temperament change is a once-in-a-piece event.
    if (pending_.hasFresh())
        current_ = pending_.read();
}

core::TemperamentId LiveTuning::temperament() const noexcept
{
    return current_.temperament;
}

double LiveTuning::referenceA4Hz() const noexcept
{
    return current_.a4Hz;
}

double LiveTuning::frequencyForNote(core::MidiNote note) const noexcept
{
    return static_cast<std::size_t>(note) < current_.unisonHz.size()
             ? current_.unisonHz[note]
             : 0.0;
}

double LiveTuning::frequencyForPipe(core::PipeId pipe, core::Footage footage) const noexcept
{
    if (static_cast<std::size_t>(pipe.midiNote) >= current_.unisonHz.size()
        || footage.num == 0)
        return 0.0;

    const double base = current_.unisonHz[pipe.midiNote];

    // The ratio to 8' unison straight from the rational: (8 * den) / num. A 4' rank
    // is 2, a 2 2/3' quint is 3, a 16' is 0.5.
    //
    // Measured, and worth recording because the obvious justification is wrong:
    // going through feet() instead -- 8.0 / (num/den) -- gives a BIT-IDENTICAL
    // result for every footage this organ has, down to 2/3'. Doubles carry these
    // small integers exactly and the round trip does not lose anything, so the
    // rational form is not rescuing a mutation from landing a few cents off its
    // harmonic. It is simply exact by construction rather than by luck, for any
    // footage anyone might later write down, and it costs one multiply.
    const double footageRatio =
        (8.0 * static_cast<double>(footage.den)) / static_cast<double>(footage.num);

    const double detuneCents = current_.detune.centsForPipe(pipe);

    return base * footageRatio * std::exp2(detuneCents / 1200.0);
}

TuningSnapshot makeSnapshot(core::TemperamentId temperament,
                            double              referenceA4Hz,
                            DetuneCurve         detune)
{
    TuningSnapshot out;
    out.temperament = temperament;
    out.a4Hz        = referenceA4Hz > 0.0 ? referenceA4Hz : 440.0;
    out.detune      = detune;

    const Temperament  t     = TemperamentLibrary::get(temperament);
    const TuningTable  table = TuningTable::build(t, out.a4Hz);
    out.unisonHz             = table.hz;

    return out;
}

} // namespace caecilia::tuning
