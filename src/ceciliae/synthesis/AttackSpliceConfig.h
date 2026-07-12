/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

namespace ceciliae::synth
{

/**
 * @brief How a recorded attack layer is spliced into a modeled, loop-free
 *        sustain.
 *
 * This is the intra-note realism a monolithic engine cannot express: a recorded
 * chiff/speech attack is cross-faded, phase- and spectrum-continuously, into a
 * MODELED sustain seeded from the same @ref SpectralModel. When
 * @ref useRecordedAttack is false the voice degenerates to a pure-modeled attack
 * (the safe, fully-shippable fallback); when true, the splice window and match
 * amounts below govern the hand-off.
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

} // namespace ceciliae::synth
