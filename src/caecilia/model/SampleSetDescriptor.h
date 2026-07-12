/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Caecilia is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstdint>
#include <string>

namespace caecilia::model
{

/**
 * @brief Descriptor for a rank's recorded material, in Caecilia's OWN
 *        proprietary sample/voicing-memory format.
 *
 * This is a pure description handed to the shell's off-thread streaming loader;
 * it holds no audio. The @c resourceToken is an opaque identifier the shell
 * resolves to a stream (the pure core never touches the OS or the filesystem).
 *
 * Caecilia never reads a GPL'd GrandOrgue / Hauptwerk sample set: this format,
 * the offline analysis data and the @c SpectralModel it produces are owned IP.
 *
 * A descriptor is only meaningful for ranks whose attack layer draws on recorded
 * chiff/speech (see @c EngineKind::Sample, or a hybrid rank splicing a recorded
 * attack into a modeled sustain); modeled/additive ranks may leave it empty.
 */
struct SampleSetDescriptor
{
    std::string   resourceToken;              ///< Opaque, shell-resolved stream id (never a path here).
    double        sourceSampleRateHz = 48000; ///< Sample rate the material was captured/stored at.
    std::int32_t  channelCount       = 1;     ///< Channels per pipe recording (1 = mono, 2 = stereo).
    core::MidiNote baseNote          = 60;    ///< Note the reference recording sounds at.
    bool          streaming          = true;  ///< Stream from backing store vs. fully resident.
    std::uint64_t loopStartFrame     = 0;     ///< Loop begin (frames); 0 with loopEnd 0 = no loop.
    std::uint64_t loopEndFrame       = 0;     ///< Loop end (frames), exclusive.

    /// @return true if this descriptor names a resource (an attack/sample layer exists).
    [[nodiscard]] bool isValid() const noexcept { return !resourceToken.empty(); }

    /// @return true if a sustain loop is defined.
    [[nodiscard]] bool hasLoop() const noexcept { return loopEndFrame > loopStartFrame; }
};

} // namespace caecilia::model
