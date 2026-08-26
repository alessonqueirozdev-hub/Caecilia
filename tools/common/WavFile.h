// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

#include "caecilia/core/EngineTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace caecilia::tools
{

/**
 * @brief In-memory, interleaved 32-bit-float audio buffer plus its format.
 *
 * The offline tools load a whole recording into one of these, analyse or edit
 * it, and optionally write it back. Samples are normalised to [-1, 1].
 *
 * This is a host-side container: allocation and standard-library use are fine
 * here (it never crosses the RT AudioEngine seam). When a tool needs to hand a
 * region to core DSP it wraps a @ref caecilia::core::AudioBlock over a
 * deinterleaved copy; @ref WavData itself stays interleaved for simple file I/O.
 */
struct WavData
{
    core::SampleRate   sampleRate  = 0.0; ///< Sample rate in Hz.
    unsigned           numChannels = 0;   ///< Channel count.
    std::size_t        numFrames   = 0;   ///< Frames (samples per channel).
    std::vector<float> interleaved;       ///< Size == numFrames * numChannels.

    /// @return Pointer to the first sample of frame @p frame (nullptr if OOB).
    [[nodiscard]] const float* frame(std::size_t frame) const noexcept
    {
        if (numChannels == 0 || frame >= numFrames)
            return nullptr;
        return interleaved.data() + frame * numChannels;
    }

    /// @return Total length in seconds (0 if the sample rate is unknown).
    [[nodiscard]] double lengthSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(numFrames) / sampleRate : 0.0;
    }
};

/**
 * @brief Minimal, self-contained RIFF/WAVE reader and writer.
 *
 * Reading understands canonical PCM (16/24/32-bit integer) and IEEE-float
 * (32-bit) @c WAVE files, including @c WAVE_FORMAT_EXTENSIBLE. Writing always
 * emits canonical 32-bit float. Assumes a little-endian host (all Caecilia
 * target architectures are). No third-party or GPL code is used — this is a
 * fresh implementation of the public RIFF layout.
 */
namespace WavFile
{
/**
 * @brief Load a WAV file into @p out.
 * @param path  Filesystem path to the source @c .wav.
 * @param out   Receives the decoded, interleaved float audio.
 * @param error Optional; receives a human-readable reason on failure.
 * @return true on success, false otherwise (with @p error set when provided).
 */
bool read(const std::string& path, WavData& out, std::string* error = nullptr);

/**
 * @brief Write @p data to @p path as a canonical 32-bit-float WAV.
 * @param path  Destination path.
 * @param data  Audio to serialise.
 * @param error Optional; receives a human-readable reason on failure.
 * @return true on success, false otherwise (with @p error set when provided).
 */
bool write(const std::string& path, const WavData& data, std::string* error = nullptr);
} // namespace WavFile

} // namespace caecilia::tools
