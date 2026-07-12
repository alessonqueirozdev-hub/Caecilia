/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

#include "ceciliae/core/EngineTypes.h"

#include "common/AnalysisTypes.h"
#include "common/WavFile.h"

namespace ceciliae::tools
{

/**
 * @brief Tunable parameters for the sample-set analyzer.
 */
struct SampleSetAnalyzerOptions
{
    core::PipeId pipe{};                    ///< Identity to stamp on the result.
    double       minLoopMs         = 150.0; ///< Shortest acceptable loop length, ms.
    double       crossfadeMs       = 40.0;  ///< Suggested loop crossfade length, ms.
    double       silenceThreshold  = -60.0; ///< dBFS below which audio is "silence".
    double       f0Hint            = 0.0;   ///< Optional expected f0 in Hz (0 = auto).
};

/**
 * @brief Extracts loop points and pitch from a single recorded pipe sample.
 *
 * The analyzer is a pure off-line transform: it takes decoded @ref WavData and
 * returns a @ref SampleAnalysis. It performs no file or console I/O (the driver
 * @c Main.cpp owns that), so it stays trivially unit-testable against synthetic
 * buffers.
 *
 * Pipeline (phased):
 *  1. Level scan  -> peak, attack end, silence bounds  (partially implemented).
 *  2. Pitch estimate -> f0 via autocorrelation / YIN    (TODO phase04).
 *  3. Sustain region -> steady window between attack and release (TODO phase04).
 *  4. Loop search -> lowest-seam zero-crossing-aligned pair       (TODO phase04).
 */
class SampleSetAnalyzer
{
public:
    /// Construct with the given options (defaults are sensible for 8' flues).
    explicit SampleSetAnalyzer(SampleSetAnalyzerOptions options = {});

    /// Analyse one decoded recording and return its extracted metadata.
    [[nodiscard]] SampleAnalysis analyze(const WavData& sample) const;

private:
    /// Real, cheap helper: peak absolute level of the whole recording, in dBFS.
    [[nodiscard]] static double peakDbfs(const WavData& sample) noexcept;

    /// TODO(phase04): autocorrelation / YIN pitch detection over the sustain.
    [[nodiscard]] PitchEstimate estimatePitch(const WavData& sample) const;

    /// TODO(phase04): locate the steady window between attack decay and release.
    [[nodiscard]] SustainRegion findSustainRegion(const WavData& sample) const;

    /// TODO(phase04): search @p region for the lowest-discontinuity loop.
    [[nodiscard]] LoopPoint findLoop(const WavData& sample, SustainRegion region) const;

    SampleSetAnalyzerOptions options_;
};

} // namespace ceciliae::tools
