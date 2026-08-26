// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

namespace caecilia::synth
{

/**
 * @brief How a recorded attack layer is to be spliced into a modeled, loop-free
 *        sustain.
 *
 * This is the intra-note realism a monolithic engine cannot express: a recorded
 * chiff/speech attack cross-faded, phase- and spectrum-continuously, into a
 * MODELED sustain seeded from the same @ref SpectralModel. When
 * @ref useRecordedAttack is false the voice degenerates to a pure-modeled attack
 * (the safe, fully-shippable fallback); when true, the splice window and match
 * amounts below would govern the hand-off.
 *
 * @todo Design descriptor only. No voice reads an AttackSpliceConfig, no attack
 *       layer exists, and every voice runs the pure-modeled path.
 */
struct AttackSpliceConfig
{
    bool  useRecordedAttack   = true;  ///< false => pure modeled attack (fallback tier).
    float crossfadeMs         = 18.0f; ///< Equal-power cross-fade length, milliseconds.
    float phaseAlignSeconds   = 0.0f;  ///< Time into the recording used for phase alignment.
    float spectralMatchAmount = 1.0f;  ///< [0,1] how hard the sustain matches the attack's end spectrum.
    float gainMatchAmount     = 1.0f;  ///< [0,1] level continuity across the splice.
    float spliceOffsetSeconds = 0.0f;  ///< Where in the recording the sustain hand-off begins.
};

} // namespace caecilia::synth
