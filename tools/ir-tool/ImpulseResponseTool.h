/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include "common/AnalysisTypes.h"
#include "common/WavFile.h"

#include <string>

namespace ceciliae::tools
{

/**
 * @brief The editing operation an @ref ImpulseResponseTool run performs.
 */
enum class IrOperation
{
    Info,       ///< Describe the IR only; do not modify it.
    Trim,       ///< Trim leading pre-delay and the inaudible tail.
    Normalise,  ///< Scale so the peak sits at a target level.
    Resample,   ///< Convert to a target sample rate.
    Deconvolve  ///< Recover an IR from a sine-sweep + inverse-filter pair.
};

/**
 * @brief Tunable parameters for the impulse-response utility.
 */
struct IrToolOptions
{
    IrOperation      op               = IrOperation::Info; ///< Operation to run.
    double           targetPeakDb     = -1.0;              ///< Normalise target, dBFS.
    double           trimThresholdDb  = -60.0;             ///< Trim onset/tail threshold, dBFS.
    core::SampleRate targetSampleRate = 0.0;               ///< Resample target Hz (0 = unchanged).
};

/**
 * @brief Prepares impulse responses for the convolution reverb.
 *
 * Convolution reverb wants a clean IR: pre-delay trimmed, tail truncated where
 * it drops below the noise floor, and a consistent peak level. This host-side
 * utility measures and conditions IRs off-line. It performs no console I/O; the
 * driver @c Main.cpp owns that.
 *
 * The convolution engine itself (partitioned FFT convolution) is written fresh
 * from public math in the @c dsp module and is NOT part of this tool.
 */
class ImpulseResponseTool
{
public:
    /// Construct with the given options.
    explicit ImpulseResponseTool(IrToolOptions options = {});

    /// Measure @p ir and return a summary; never mutates @p ir.
    [[nodiscard]] ImpulseResponseInfo describe(const WavData& ir) const;

    /**
     * @brief Apply the configured editing operation to @p ir in place.
     * @param ir    The impulse response to condition.
     * @param error Optional; receives a reason on failure.
     * @return true on success (Info is a successful no-op).
     */
    bool processInPlace(WavData& ir, std::string* error = nullptr) const;

private:
    /// Real helper: peak absolute level of @p ir, in dBFS (-144 for silence).
    [[nodiscard]] static double peakDbfs(const WavData& ir) noexcept;

    /// Real, simple operation: scale @p ir so its peak hits targetPeakDb.
    bool normaliseInPlace(WavData& ir) const noexcept;

    IrToolOptions options_;
};

} // namespace ceciliae::tools
