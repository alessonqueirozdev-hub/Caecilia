// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/synthesis/PartialBank.h"
#include "caecilia/synthesis/SpectralModel.h"

#include <cstddef>

/**
 * @file RankVoicing.h
 * @brief Everything one rank needs to be a voice of its own.
 *
 * This lives in `synth` rather than beside its builder in `model` because of the
 * dependency direction: `model` already includes `synth` (it builds spectral
 * recipes), so a voice cannot reach back the other way. The DATA is a synthesis
 * concern; only deriving it from an @c Organ is a model one.
 */

namespace caecilia::synth
{

/**
 * @brief One rank's voicing: what it sounds like, how fast it speaks, where it is.
 *
 * The spectrum is 8'-referenced and carries the same fixed calibration the
 * composite builder applies, so summing every engaged rank's spectrum reproduces
 * the composite exactly. That is not a coincidence to be preserved carefully —
 * it falls out of the calibration being registration-independent — and it is what
 * lets the one-voice-per-rank migration prove it did not change the instrument.
 */
struct RankVoicing
{
    /// Partial storage a per-rank voice reserves.
    ///
    /// Every voice reserves this much whatever rank it adopts, so it is a memory
    /// figure multiplied by the whole pool -- and one that is TOUCHED per block,
    /// which makes it a cache figure too. Measured: the largest rank this organ
    /// builds is 32 partials, and reserving 96 made a ten-note Tutti walk three
    /// times more memory than it used. The guard rail is the `fits the storage`
    /// case in PerRankDynamicsTest; grow this the day it fires, not before.
    static constexpr std::size_t kMaxPartials = 48;

    core::StopId      stop{};
    core::DivisionId  division{};
    core::WindchestId chest{};
    core::TonalFamily family  = core::TonalFamily::Principal;
    core::Footage     footage = core::footage::kEight;

    SpectralModel     spectrum;   ///< 8'-referenced and calibrated.
    SpeechProfile     speech{};   ///< How fast this family and length speaks.
};

} // namespace caecilia::synth
