/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include <cstddef>
#include <vector>

/**
 * @file AnalysisTypes.h
 * @brief Plain-old-data result types produced by the offline analysis tools.
 *
 * These structures live in @c ceciliae::tools (NOT in @c ceciliae::core): they
 * are host-side, off-line artefacts written to disk by the command-line
 * toolchain and later re-ingested by the synthesis/model modules as the
 * proprietary SpectralModel / sample-set metadata. Nothing here ever touches
 * the audio thread, so unlike the RT core these types are free to own
 * heap-backed containers (@c std::vector).
 *
 * They intentionally reuse the shared core vocabulary (@ref ceciliae::core::PipeId,
 * @ref ceciliae::core::Footage, @ref ceciliae::core::MidiNote) so a partial bank
 * or loop analysis carries the exact same identity keys the engine will use.
 */

namespace ceciliae::tools
{

/**
 * @brief Estimated fundamental pitch of a recorded pipe sample.
 *
 * Produced by the sample-set analyzer; consumed by the tuning/model modules to
 * verify a recording matches its nominal note and to derive per-pipe detune.
 */
struct PitchEstimate
{
    double            frequencyHz          = 0.0; ///< Estimated fundamental (f0) in Hz.
    double            confidence           = 0.0; ///< Estimator confidence in [0, 1].
    double            centsFromEqualTemper = 0.0; ///< Deviation from nearest A440 ET note, in cents.
    core::MidiNote    nearestNote          = 60;  ///< Closest equal-tempered MIDI note.
};

/**
 * @brief The steady-state window of a recording, in sample frames.
 *
 * The region after the attack transient has decayed and before the release,
 * used both as the loop search space and as the spectral-analysis window.
 */
struct SustainRegion
{
    std::size_t startFrame = 0; ///< First frame of the steady region.
    std::size_t endFrame   = 0; ///< One-past-the-last frame of the steady region.
};

/**
 * @brief A candidate seamless sustain loop, expressed in sample frames.
 *
 * @c seamError is the residual discontinuity across the splice (lower is
 * better); the analyzer picks the lowest-error zero-crossing-aligned pair.
 */
struct LoopPoint
{
    std::size_t startFrame  = 0;     ///< Loop start frame.
    std::size_t endFrame    = 0;     ///< Loop end frame (wraps back to start).
    double      crossfadeMs = 0.0;   ///< Suggested equal-power crossfade length, ms.
    double      seamError   = 0.0;   ///< Normalised splice discontinuity metric.
    bool        valid       = false; ///< True once a usable loop has been found.
};

/**
 * @brief Complete result of analysing one pipe recording.
 */
struct SampleAnalysis
{
    core::PipeId     pipe{};              ///< Identity of the analysed pipe.
    core::SampleRate sampleRate  = 0.0;   ///< Recording sample rate in Hz.
    std::size_t      numFrames   = 0;     ///< Length of the recording in frames.
    unsigned         numChannels = 0;     ///< Channel count of the recording.
    PitchEstimate    pitch{};             ///< Estimated fundamental pitch.
    SustainRegion    sustain{};           ///< Detected steady-state region.
    LoopPoint        loop{};              ///< Best loop found within the sustain.
    double           attackEndSec = 0.0;  ///< Candidate attack/sustain splice point, seconds.
    double           peakDbfs     = 0.0;  ///< Peak sample level in dBFS.
};

/**
 * @brief One tracked additive partial, relative to the fundamental.
 *
 * Field layout mirrors the RT-side partial descriptor the additive/modal voice
 * tier will read, so a bank can be handed across the analysis boundary without
 * reshaping.
 */
struct ExtractedPartial
{
    float ratioToF0        = 1.0f;    ///< Frequency ratio to f0 (1 = fundamental).
    float ampDb            = -120.0f; ///< Steady amplitude in dBFS.
    float phase            = 0.0f;    ///< Phase at the analysis anchor, radians.
    float windSensitivity  = 0.0f;    ///< Level response per unit pressure deviation.
};

/**
 * @brief The extracted additive model of a pipe's steady spectrum.
 *
 * This is the off-line half of the analysis bridge that later lets a sampled
 * attack and a modeled sustain share one timbre.
 */
struct PartialBank
{
    float                         fundamentalHz = 0.0f;                 ///< Detected/assumed f0 in Hz.
    core::Footage                 footage       = core::footage::kEight;///< Sounding footage of the rank.
    std::vector<ExtractedPartial> partials;                             ///< Tracked partials, low to high.
};

/**
 * @brief Summary of an impulse response for the convolution reverb.
 */
struct ImpulseResponseInfo
{
    core::SampleRate sampleRate  = 0.0; ///< IR sample rate in Hz.
    unsigned         numChannels = 0;   ///< IR channel count.
    std::size_t      numFrames   = 0;   ///< IR length in frames.
    double           lengthSec   = 0.0; ///< IR length in seconds.
    double           rt60Sec     = 0.0; ///< Estimated RT60 decay time, seconds.
    double           peakDbfs    = 0.0; ///< Peak sample level in dBFS.
    std::size_t      onsetFrame  = 0;   ///< First frame carrying significant energy.
};

} // namespace ceciliae::tools
