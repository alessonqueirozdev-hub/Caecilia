// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include <array>
#include <cstddef>

#include "caecilia/core/EngineTypes.h"
#include "caecilia/core/ITuning.h"
#include "caecilia/tuning/DetuneCurve.h"
#include "caecilia/tuning/StretchTuning.h"
#include "caecilia/tuning/Temperament.h"
#include "caecilia/tuning/TuningTable.h"

namespace caecilia::tuning
{

/**
 * @brief Concrete @c core::ITuning: the sounding-frequency oracle the voices read.
 *
 * A TuningModel composes the four layers of the module into a single query
 * surface:
 *   1. a @ref Temperament (interval character),
 *   2. a reference A4 pitch (absolute anchoring, via @ref TuningTable),
 *   3. a @ref StretchTuning curve (per-key octave stretch), and
 *   4. a @ref DetuneCurve (per-pipe deterministic scatter).
 *
 * Layers 1–3 are baked once into a 128-entry unison table by @ref configure /
 * @ref rebuild (off the audio thread). Layer 4 is a pure per-pipe hash evaluated
 * on the fly. The @c ITuning query methods are therefore cheap, RT-safe lookups
 * and never mutate live state.
 *
 * ## Real-time contract
 * - @ref configure and the @c set* mutators rebuild the table and are NOT
 *   RT-safe. A model must therefore never be BOUND to the audio thread and then
 *   reconfigured; that is why the plugin binds a @c LiveTuning instead and uses
 *   @c tuning::makeSnapshot to get a rebuilt table across the boundary as data.
 * - @ref frequencyForNote and @ref frequencyForPipe are @c noexcept, allocation-
 *   free and lock-free, safe to call while rendering (against a model that is not
 *   concurrently being reconfigured).
 */
class TuningModel final : public core::ITuning
{
public:
    /// Constructs an equal-tempered, A4 = 440 Hz model with no stretch or detune.
    TuningModel() noexcept;

    /**
     * @brief Rebuild the model from a full set of tuning layers. NOT RT-safe.
     * @param temperament   Interval character to realise.
     * @param referenceA4Hz Absolute pitch of A4 in Hz (> 0).
     * @param stretch       Octave-stretch curve (default: none).
     * @param detune        Per-pipe scatter (default: none).
     */
    void configure(const Temperament& temperament,
                   double referenceA4Hz,
                   const StretchTuning& stretch = StretchTuning::none(),
                   const DetuneCurve& detune = DetuneCurve::none()) noexcept;

    /// Replace only the temperament and rebuild. NOT RT-safe.
    void setTemperament(const Temperament& temperament) noexcept;

    /// Replace only the reference pitch and rebuild. NOT RT-safe.
    void setReferenceA4Hz(double referenceA4Hz) noexcept;

    /// Replace only the stretch curve and rebuild. NOT RT-safe.
    void setStretch(const StretchTuning& stretch) noexcept;

    /// Replace only the per-pipe detune (no table rebuild needed). NOT RT-safe.
    void setDetune(const DetuneCurve& detune) noexcept;

    /// @return The stretch curve currently in effect.
    [[nodiscard]] const StretchTuning& stretch() const noexcept { return stretch_; }

    /// @return The per-pipe detune curve currently in effect.
    [[nodiscard]] const DetuneCurve& detune() const noexcept { return detune_; }

    // --- core::ITuning ------------------------------------------------------

    [[nodiscard]] core::TemperamentId temperament() const noexcept override;
    [[nodiscard]] double referenceA4Hz() const noexcept override;
    [[nodiscard]] double frequencyForNote(core::MidiNote note) const noexcept override;
    [[nodiscard]] double frequencyForPipe(core::PipeId pipe, core::Footage footage) const noexcept override;

private:
    /// Recompute @c unisonHz_ from temperament + A4 + stretch. NOT RT-safe.
    void rebuild() noexcept;

    Temperament   temperament_{};
    double        a4Hz_ = 440.0;
    StretchTuning stretch_{};
    DetuneCurve   detune_{};

    /// Precomputed 8' unison Hz per MIDI note (temperament AND stretch baked in).
    std::array<double, TuningTable::kNoteCount> unisonHz_{};
};

} // namespace caecilia::tuning
