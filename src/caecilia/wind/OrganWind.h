// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"
#include "caecilia/wind/WindTypes.h"

namespace caecilia::model { class Organ; }

/**
 * @file OrganWind.h
 * @brief Compiling a loaded organ's windchests into a runnable wind supply.
 *
 * The wind model has been implemented and unit-tested since the module was
 * written, and was connected to nothing: @c AudioEngine::setWindSupply had no
 * caller anywhere in the tree, so @c RenderContext::wind was always null, every
 * partial's wind coupling read a deviation of zero, and the tremulant was a
 * switch that moved on screen and nowhere else.
 *
 * This is the missing half — the translation from what an organ IS (a list of
 * windchests, each with a nominal pressure and possibly a tremulant) to what the
 * ODE needs. It lives in `wind` rather than in the plugin so it can be tested
 * without a host.
 */
namespace caecilia::wind
{

/**
 * @brief Build a wind configuration for a loaded organ.
 *
 * One reservoir feeds every chest, which is how a house organ is actually
 * plumbed: a single blower and regulator, from which each division's trunk drops
 * its own pressure under its own load. That shared reservoir is why a heavy pedal
 * chord makes the Positif flinch — the effect the whole wind model exists to
 * produce, and one that per-chest reservoirs would abolish.
 *
 * Chests are emitted in the organ's own order and keep their @c WindchestId, so a
 * rank's chest binding resolves without a translation table. A chest whose spec
 * says @c hasTremulant gets one; the others get none, and asking to engage a
 * tremulant they do not have is ignored rather than an error.
 *
 * @param organ The loaded instrument.
 * @return A configuration ready for @c WindModel::configure. An organ with no
 *         windchests yields a single default chest, so an instrument is never
 *         left with no wind at all.
 *
 * Off the audio thread: allocates.
 */
[[nodiscard]] WindModelConfig configFromOrgan(const model::Organ& organ);

/**
 * @brief The flow one sounding voice draws, in the model's arbitrary units.
 *
 * Calibrated from the sag the reservoir produces: sag in pascals is
 * `demand * (1/feedConductance + 1/trunkConductance)`, which with the defaults
 * (4 and 50) is `demand * 0.27`.
 *
 * This is the figure for ONE pipe at the reference — middle C on an 8' rank. Every
 * sounding pipe scales it by @ref rankWindFlow for its rank's length and, in the
 * engine, by its own note: a bass 16' pipe draws about eighty times what a treble
 * mutation does, which is measured and asserted in OrganWindTest.
 *
 * Measured with the scaling in place: a full-organ chord across the compass is 168
 * pipes and sags the reservoir 1.4%, which is a well-winded instrument.
 */
inline constexpr float kFlowPerVoice = 0.3f;

/**
 * @brief What one pipe of a rank draws at middle C, relative to an 8' rank.
 *
 * @param footage The rank's footage.
 * @return A scale for @ref kFlowPerVoice: 2.0 for a 16', 1.0 for an 8', 0.25 for a
 *         2'. An unknown footage answers 1.0 rather than zero, so a rank with no
 *         declared length still draws wind.
 *
 * Volumetric flow scales with the pipe, and a pipe scales with the wavelength it
 * sounds, so this is simply the rank's length in eight-foot units. The NOTE's half
 * of the figure is applied by the engine, which knows the note.
 *
 * Lives here, in core, rather than in the plugin that publishes the rank table --
 * the plugin and the test rig both need it, and a formula written twice is a
 * formula that will eventually be two formulas.
 */
[[nodiscard]] constexpr float rankWindFlow(core::Footage footage) noexcept
{
    const double feet = footage.feet();
    return feet > 0.0 ? static_cast<float>(feet / 8.0) : 1.0f;
}

} // namespace caecilia::wind
