// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#include "caecilia/tuning/TuningModel.h"

#include <cmath>

#include "caecilia/tuning/TemperamentLibrary.h"

namespace caecilia::tuning
{

TuningModel::TuningModel() noexcept
{
    temperament_ = TemperamentLibrary::get(core::TemperamentId::Equal);
    rebuild();
}

void TuningModel::configure(const Temperament& temperament,
                            double referenceA4Hz,
                            const StretchTuning& stretch,
                            const DetuneCurve& detune) noexcept
{
    temperament_ = temperament;
    a4Hz_ = referenceA4Hz > 0.0 ? referenceA4Hz : 440.0;
    stretch_ = stretch;
    detune_ = detune;
    rebuild();
}

void TuningModel::setTemperament(const Temperament& temperament) noexcept
{
    temperament_ = temperament;
    rebuild();
}

void TuningModel::setReferenceA4Hz(double referenceA4Hz) noexcept
{
    a4Hz_ = referenceA4Hz > 0.0 ? referenceA4Hz : 440.0;
    rebuild();
}

void TuningModel::setStretch(const StretchTuning& stretch) noexcept
{
    stretch_ = stretch;
    rebuild();
}

void TuningModel::setDetune(const DetuneCurve& detune) noexcept
{
    // Per-pipe detune is evaluated live per query, so no table rebuild is needed.
    detune_ = detune;
}

void TuningModel::rebuild() noexcept
{
    const TuningTable table = TuningTable::build(temperament_, a4Hz_);
    for (std::size_t n = 0; n < unisonHz_.size(); ++n)
    {
        const double stretchCents = stretch_.centsForNote(static_cast<core::MidiNote>(n));
        unisonHz_[n] = table.hz[n] * std::exp2(stretchCents / 1200.0);
    }
}

core::TemperamentId TuningModel::temperament() const noexcept
{
    return temperament_.id;
}

double TuningModel::referenceA4Hz() const noexcept
{
    return a4Hz_;
}

double TuningModel::frequencyForNote(core::MidiNote note) const noexcept
{
    return static_cast<std::size_t>(note) < unisonHz_.size() ? unisonHz_[note] : 0.0;
}

double TuningModel::frequencyForPipe(core::PipeId pipe, core::Footage footage) const noexcept
{
    if (static_cast<std::size_t>(pipe.midiNote) >= unisonHz_.size() || footage.num == 0)
        return 0.0;

    const double base = unisonHz_[pipe.midiNote];

    // Exact sounding-pitch ratio from the rank's footage relative to 8' unison:
    // ratio = 8 / feet = (8 * den) / num. A 4' rank -> 2 (octave up), a 2 2/3'
    // quint -> 3 (a twelfth up), a 16' -> 0.5 (octave down). Because this is the
    // exact rational, a mutation lands precisely on its true harmonic.
    const double footageRatio =
        (8.0 * static_cast<double>(footage.den)) / static_cast<double>(footage.num);

    // Per-pipe deterministic detune scatter (0 when disabled).
    const double detuneCents = detune_.centsForPipe(pipe);

    return base * footageRatio * std::exp2(detuneCents / 1200.0);
}

} // namespace caecilia::tuning
